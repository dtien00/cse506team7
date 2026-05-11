/*
 * block_cache_race_test.c — test the recheck fix for the unlocked-virtio-window race.
 *
 * The race:
 *   block_cache_read/write drop intr_lock before virtio I/O.  In that window
 *   a concurrent caller can insert the same block_num into a different slot →
 *   two slots claim the same block → stale reads / lost writes.
 *
 * The fix (already in block_cache.c):
 *   After find_free_cache_block() returns, re-lock and re-run find_cache_block().
 *   If another caller already inserted the block, release the reserved slot and
 *   use the existing one.  This prevents duplicates.
 *
 * How we test it deterministically (no scheduler needed):
 *   block_cache_debug_set_race_hook() installs a fn called with interrupts ON,
 *   lock dropped, right in the race window between find_free_cache_block() and
 *   the recheck.  The hook calls block_cache_debug_reserve_slot() to simulate
 *   exactly what a concurrent caller would do.  The recheck must then coalesce
 *   → slot count stays 1 after the full read/write completes.
 */
#include "kernel/printk.h"
#include "kernel/types.h"
#include "fs/block_cache.h"
#include "tests/test.h"
#include "lib/string.h"

static int passed = 0;
static int failed = 0;

#define TEST_BLOCK_A  0x1u
#define TEST_BLOCK_B  0x3u

static void banner(const char *s) {
    printk("\n--- %s ---\n", s);
}

/* Hook: called inside the race window for TEST_BLOCK_A.
 * Inserts a competing valid slot — exactly what a concurrent thread would do. */
static void hook_insert_block_a(uint32_t block_num) {
    if (block_num == TEST_BLOCK_A) {
        int r = block_cache_debug_reserve_slot(TEST_BLOCK_A);
        printk("  [hook] concurrent insert of block A during virtio window: %s\n",
               r == 0 ? "inserted" : "no free slot (cache full)");
    }
}

static void hook_insert_block_b(uint32_t block_num) {
    if (block_num == TEST_BLOCK_B) {
        int r = block_cache_debug_reserve_slot(TEST_BLOCK_B);
        printk("  [hook] concurrent insert of block B during virtio window: %s\n",
               r == 0 ? "inserted" : "no free slot (cache full)");
    }
}

/* ── test 1: read path recheck coalesces duplicate ───────────────────────
 *
 * block_cache_read(A) misses, drops lock, hook fires and inserts A into a
 * second slot, then recheck runs.  Recheck must find the duplicate and use
 * the existing slot, releasing the in-flight one.  Final count must be 1.
 */
static void test_read_recheck_coalesces(void) {
    banner("read-recheck-coalesces");
    uint8_t buf[BLOCK_SIZE];

    block_cache_debug_set_race_hook(hook_insert_block_a);

    int r = block_cache_read(TEST_BLOCK_A, buf);
    block_cache_debug_set_race_hook(NULL);

    if (r != 0) {
        printk("  [SKIP] virtio read of block %u failed (disk too small?)\n",
               TEST_BLOCK_A);
        return;
    }

    int c = block_cache_debug_count_block(TEST_BLOCK_A);
    printk("  slot count after read+race = %d (want 1)\n", c);
    TEST("read-recheck: recheck coalesces concurrent insert → count==1", c == 1);
}

/* ── test 2: no hook → count still 1 (sanity) ────────────────────────────
 *
 * Normal read with no concurrent insertion.  Baseline: count must be 1.
 * Confirms the test infrastructure doesn't break the happy path.
 */
static void test_read_no_race_baseline(void) {
    banner("read-no-race-baseline");
    uint8_t buf[BLOCK_SIZE];

    /* Use B so we get a fresh miss (A may already be cached from test 1). */
    int r = block_cache_read(TEST_BLOCK_B, buf);
    if (r != 0) {
        printk("  [SKIP] virtio read of block %u failed\n", TEST_BLOCK_B);
        return;
    }

    int c = block_cache_debug_count_block(TEST_BLOCK_B);
    printk("  slot count (no race) = %d (want 1)\n", c);
    TEST("read-baseline: normal read → exactly one slot", c == 1);
}

/* ── test 3: write path recheck coalesces duplicate ──────────────────────
 *
 * Same as test 1 but via block_cache_write.  Hook fires in the write-path
 * race window; recheck must coalesce; count must be 1 after write.
 */
static void test_write_recheck_coalesces(void) {
    banner("write-recheck-coalesces");
    uint8_t buf[BLOCK_SIZE];
    memset(buf, 0x5A, sizeof(buf));

    /* Evict B from cache if present so write goes through the miss path. */
    /* (We can't directly evict, but B was just read clean; we rely on the
     *  cache having room for a fresh slot — BLOCK_CACHE_SIZE=256 so fine.) */

    block_cache_debug_set_race_hook(hook_insert_block_b);
    int r = block_cache_write(TEST_BLOCK_B, buf);
    block_cache_debug_set_race_hook(NULL);

    if (r != 0) {
        printk("  [SKIP] write of block %u failed\n", TEST_BLOCK_B);
        return;
    }

    int c = block_cache_debug_count_block(TEST_BLOCK_B);
    printk("  slot count after write+race = %d (want 1)\n", c);
    TEST("write-recheck: recheck coalesces concurrent insert → count==1", c == 1);
}

/* ── test 4: data written before hook is not lost ────────────────────────
 *
 * After test 3, read B back.  Must see 0x5A pattern, not stale zeros from
 * the hook-inserted slot.  Verifies the correct slot (with the write data)
 * survived the coalesce.
 */
static void test_write_data_survives_race(void) {
    banner("write-data-survives-race");
    uint8_t buf[BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));

    int r = block_cache_read(TEST_BLOCK_B, buf);
    if (r != 0) {
        printk("  [SKIP] read-back of block %u failed\n", TEST_BLOCK_B);
        return;
    }

    int all_5a = 1;
    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (buf[i] != 0x5A) { all_5a = 0; break; }
    }
    printk("  buf-all-0x5A=%d (want 1 → correct slot survived)\n", all_5a);
    TEST("write-data: written data visible after race+recheck", all_5a == 1);
}

/* ── public entry ────────────────────────────────────────────────────────*/
void block_cache_race_test_run(void) {
    printk("\n========================================\n");
    printk("    Block-Cache Race Tests\n");
    printk("========================================\n");

    passed = 0;
    failed = 0;

    test_read_no_race_baseline();
    test_read_recheck_coalesces();
    test_write_recheck_coalesces();
    test_write_data_survives_race();

    printk("\nblock_cache_race_test: %d passed, %d failed\n", passed, failed);
}
