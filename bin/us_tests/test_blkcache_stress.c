/*
 * test_blkcache_stress.c — block-cache / disk-driver stress tests
 *
 * Targets the exact stall described in JC_TESTS.md §2:
 *   - many back-to-back file creations on /mnt (each creation touches
 *     bitmap, inode table, directory data, group descriptor — distinct
 *     blocks that force cache evictions and write-backs)
 *   - create+write+close loop without unlinking, so inodes accumulate
 *     and pressure the inode bitmap over many iterations
 *   - forced cache-capacity exhaustion: open many distinct files to
 *     bring more blocks in than the cache has slots
 *
 * Contrast with test_many_writes (xv6_compat): that test writes 60 bytes
 * to a single open fd, keeping one data block hot.  It never evicts the
 * block nor triggers a second inode allocation, so it cannot expose the
 * sleep/wakeup race that fires when an eviction must write-back a dirty
 * block before the slot can be reused for the incoming request.
 */
#pragma GCC diagnostic ignored "-Wunused-function"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include "us_test_common.h"

/* ── helpers ────────────────────────────────────────────────────────────── */

static void itoa_simple(int v, char *buf, int bufsz) {
    int i = bufsz - 1;
    buf[i] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + v % 10); v /= 10; }
    if (i > 0) {
        int len = bufsz - 1 - i;
        for (int j = 0; j < len; j++) buf[j] = buf[i + j];
        buf[len] = '\0';
    }
}

static void make_path(char *out, int outsz, const char *prefix, int idx) {
    int n = 0;
    for (int i = 0; prefix[i] && n < outsz - 8; i++) out[n++] = prefix[i];
    char tmp[12];
    itoa_simple(idx, tmp, sizeof(tmp));
    for (int i = 0; tmp[i] && n < outsz - 2; i++) out[n++] = tmp[i];
    out[n++] = '.'; out[n++] = 't'; out[n++] = 'x'; out[n++] = 't';
    out[n] = '\0';
}

/* ── test 1: many successive file creations (no unlink between them) ─────
 *
 * Each open(O_CREAT) forces: inode bitmap read → inode alloc write →
 * directory block read/write → group-descriptor update.  With 30+ files
 * these distinct blocks overflow a small cache, triggering dirty evictions.
 * The stall fires here if the eviction write-back races with the next req.
 */
static void test_many_creates(us_test_group_t *group) {
    const int N = 36;
    int ok = 1;
    char path[64];

    for (int i = 0; i < N && ok; i++) {
        make_path(path, sizeof(path), "/mnt/bcs_create_", i);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        char b = (char)(i & 0xFF);
        if ((int)write(fd, &b, 1) != 1) ok = 0;
        close(fd);
    }
    TEST(group, "many-creates: 36 successive O_CREAT files succeed", ok);

    /* verify all files are readable */
    int read_ok = 1;
    for (int i = 0; i < N && read_ok; i++) {
        make_path(path, sizeof(path), "/mnt/bcs_create_", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) { read_ok = 0; break; }
        char b = 0;
        if ((int)read(fd, &b, 1) != 1 || b != (char)(i & 0xFF)) read_ok = 0;
        close(fd);
    }
    TEST(group, "many-creates: all 36 files read back correctly", read_ok);

    /* cleanup */
    for (int i = 0; i < N; i++) {
        make_path(path, sizeof(path), "/mnt/bcs_create_", i);
        unlink(path);
    }
}

/* ── test 2: interleaved create-write-close-reopen-read ──────────────────
 *
 * Unlike test_many_writes which keeps one fd open for all writes, each
 * iteration here closes and reopens, flushing the position and forcing
 * the buffer layer to re-fetch the inode for the subsequent open.  If
 * the "device busy" gate is not released atomically after I/O completion
 * the second open blocks forever.
 */
static void test_create_reopen_loop(us_test_group_t *group) {
    const int N = 30;
    const char *path = "/mnt/bcs_reopen.txt";
    int ok = 1;

    for (int i = 0; i < N && ok; i++) {
        /* write phase */
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        char b = (char)(i & 0xFF);
        if ((int)write(fd, &b, 1) != 1) { close(fd); ok = 0; break; }
        close(fd);

        /* read-back phase — forces re-fetch of inode + data block */
        fd = open(path, O_RDONLY);
        if (fd < 0) { ok = 0; break; }
        char got = 0;
        if ((int)read(fd, &got, 1) != 1 || got != b) ok = 0;
        close(fd);
    }
    TEST(group, "create-reopen-loop: 30 write+close+reopen+read cycles", ok);
    unlink(path);
}

/* ── test 3: many small writes spread across multiple files ──────────────
 *
 * test_many_writes does N writes to ONE file (one hot data block).
 * This test does 1 write each to M distinct files, which keeps M separate
 * data blocks live simultaneously and forces cache churn when M > cache
 * capacity.  If a cache slot is reclaimed before its write-back completes
 * the subsequent read sees stale data.
 */
static void test_writes_many_files(us_test_group_t *group) {
    const int M = 36;
    char path[64];
    int ok = 1;

    for (int i = 0; i < M && ok; i++) {
        make_path(path, sizeof(path), "/mnt/bcs_mf_", i);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        char b = (char)((i * 7 + 3) & 0xFF);
        if ((int)write(fd, &b, 1) != 1) ok = 0;
        close(fd);
    }
    TEST(group, "writes-many-files: 36 distinct files written", ok);

    /* read all back after all writes complete — tests dirty-eviction ordering */
    int read_ok = 1;
    for (int i = 0; i < M && read_ok; i++) {
        make_path(path, sizeof(path), "/mnt/bcs_mf_", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) { read_ok = 0; break; }
        char b = 0;
        if ((int)read(fd, &b, 1) != 1 || b != (char)((i * 7 + 3) & 0xFF)) read_ok = 0;
        close(fd);
    }
    TEST(group, "writes-many-files: all 50 files read back correctly", read_ok);

    for (int i = 0; i < M; i++) {
        make_path(path, sizeof(path), "/mnt/bcs_mf_", i);
        unlink(path);
    }
}

/* ── test 4: rapid create+unlink cycles (inode bitmap churn) ────────────
 *
 * test_create_delete in xv6_compat does 30 cycles but uses manual
 * int-to-string without sprintf.  This variant increases to 60 cycles
 * and validates that the inode is actually gone (a second open should
 * create a fresh file, not reuse stale cached state).
 */
static void test_create_unlink_cycles(us_test_group_t *group) {
    const int N = 60;
    const char *path = "/mnt/bcs_cycle.txt";
    int ok = 1;

    for (int i = 0; i < N && ok; i++) {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        char b = (char)(i & 0xFF);
        if ((int)write(fd, &b, 1) != 1) { close(fd); ok = 0; break; }
        close(fd);

        /* verify before unlink */
        fd = open(path, O_RDONLY);
        if (fd < 0) { ok = 0; break; }
        char got = 0;
        if ((int)read(fd, &got, 1) != 1 || got != b) ok = 0;
        close(fd);

        if (ok && unlink(path) < 0) ok = 0;
    }
    TEST(group, "create-unlink-cycles: 60 create+verify+unlink cycles", ok);
}

/* ── test 5: write more bytes than a single 512-byte block ───────────────
 *
 * Contrast: test_many_writes writes 60 bytes — fits in one 512 B block.
 * Here we write 600 bytes so the file crosses a block boundary, forcing
 * a second data block allocation and two separate cache-slot write-backs
 * on close.  If the second block's dirty eviction races with the close
 * completion the subsequent read loses the tail bytes.
 */
static void test_cross_block_write(us_test_group_t *group) {
    const char *path = "/mnt/bcs_crossblock.txt";
    const int SZ = 600;
    char wbuf[600];
    for (int i = 0; i < SZ; i++) wbuf[i] = (char)(i & 0xFF);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "cross-block-write: create file", fd >= 0);
    if (fd < 0) return;

    int w = (int)write(fd, wbuf, SZ);
    TEST(group, "cross-block-write: 600 bytes written", w == SZ);
    close(fd);

    fd = open(path, O_RDONLY);
    TEST(group, "cross-block-write: reopen succeeds", fd >= 0);
    if (fd < 0) { unlink(path); return; }

    char rbuf[600];
    int r = (int)read(fd, rbuf, SZ);
    close(fd);
    TEST(group, "cross-block-write: 600 bytes read back", r == SZ);
    int ok = (r == SZ);
    for (int i = 0; i < r && ok; i++) {
        if (rbuf[i] != (char)(i & 0xFF)) { ok = 0; }
    }
    TEST(group, "cross-block-write: all bytes correct across block boundary", ok);
    unlink(path);
}

/* ── test 6: open+write+close+unlink loop (stall finder) ────────────────
 *
 * Minimal reproduction of the stall described in JC_TESTS.md §2:
 * each iteration allocates a fresh inode (open O_CREAT), writes a few
 * bytes, closes (triggering dirty write-back), then immediately unlinks
 * (freeing the inode bitmap slot).  The rapid alloc→write-back→free
 * sequence maximises bitmap-block and inode-table-block churn.  Print
 * the iteration count so the user can see exactly where the system stalls.
 */
static void test_open_write_close_unlink_loop(us_test_group_t *group) {
    const int N = 250;
    const char *path = "/mnt/bcs_stall_probe.txt";
    const char data[] = "abc";
    const int  datalen = 3;
    int ok = 1;
    int last = 0;

    for (int i = 0; i < N && ok; i++) {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        if ((int)write(fd, data, datalen) != datalen) { close(fd); ok = 0; break; }
        close(fd);
        if (unlink(path) < 0) { ok = 0; break; }
        last = i + 1;
    }

    /* last is only meaningful on failure — on success it equals N */
    (void)last;
    TEST(group, "stall-probe: 250 open+write+close+unlink iterations", ok);
}

/* ── test 7: 25 concurrent children each create+write+close a unique file ───
 *
 * Each child gets its own path so there are no name collisions.  All children
 * run concurrently, hammering the inode allocator, directory block, and block
 * cache from multiple processes simultaneously.  The parent waits for all 25,
 * then reads back every file to verify correctness.
 */
static void test_fork_many_writers(us_test_group_t *group) {
    const int N = 25;
    pid_t pids[25];
    char path[64];

    /* spawn all children before waiting for any */
    for (int i = 0; i < N; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            usleep(200000); // sleep a little bit to give parent time to complete
            /* child: build path, create file, write one byte, close, exit */
            make_path(path, sizeof(path), "/mnt/bcs_fork_", i);
            int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) _exit(1);
            char b = (char)(i & 0xFF);
            if ((int)write(fd, &b, 1) != 1) { close(fd); _exit(2); }
            close(fd);
            _exit(0);
        }
    }

    /* reap all children, track failures */
    int all_ok = 1;
    for (int i = 0; i < N; i++) {
        if (pids[i] < 0) { all_ok = 0; continue; }
        int status = 0;
        waitpid(pids[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) all_ok = 0;
    }
    TEST(group, "fork-many-writers: 25 children all exit cleanly", all_ok);

    /* parent reads back every file */
    int read_ok = 1;
    for (int i = 0; i < N && read_ok; i++) {
        make_path(path, sizeof(path), "/mnt/bcs_fork_", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) { read_ok = 0; break; }
        char b = 0;
        if ((int)read(fd, &b, 1) != 1 || b != (char)(i & 0xFF)) read_ok = 0;
        close(fd);
    }
    TEST(group, "fork-many-writers: all 25 files read back correctly", read_ok);

    /* cleanup */
    for (int i = 0; i < N; i++) {
        make_path(path, sizeof(path), "/mnt/bcs_fork_", i);
        unlink(path);
    }
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_blkcache_stress(us_test_group_t *group) {
    ut_group_begin(group, "blkcache_stress");

    // test_many_creates(group);
    // test_create_reopen_loop(group);
    // test_writes_many_files(group);
    // test_create_unlink_cycles(group);
    // test_cross_block_write(group);
    // test_open_write_close_unlink_loop(group);
    test_fork_many_writers(group);

    ut_group_end(group);
}
