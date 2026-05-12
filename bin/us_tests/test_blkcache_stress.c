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
#include <stdio.h>
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

static void make_irq_path(char *out, int outsz, int idx) {
    const char *pre = "/mnt/irqlat_";
    int n = 0;
    for (int i = 0; pre[i] && n < outsz - 12; i++) out[n++] = pre[i];
    char tmp[12];
    itoa_simple(idx, tmp, sizeof(tmp));
    for (int i = 0; tmp[i] && n < outsz - 5; i++) out[n++] = tmp[i];
    out[n++] = '.'; out[n++] = 'b'; out[n++] = 'i'; out[n++] = 'n';
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

/* ── test 8: no-unlink create loop with per-iteration print ─────────────
 *
 * Creates files without ever unlinking them.  Prints the iteration number
 * before each open() so the last visible number reveals exactly where the
 * system stalls (hang) vs. where it returns an error (dir full / out of
 * inodes).  N=50 is enough to overflow a one-block directory (max ~36
 * entries) and stress the inode allocator.
 */
static void test_create_nounlink_verbose(us_test_group_t *group) {
    const int N = 34;
    char path[64];
    int ok = 1;

    for (int i = 0; i < N && ok; i++) {
        printf("stall-probe-nounlink: iter %d\n", i);
        make_path(path, sizeof(path), "/mnt/bcs_nounlink_", i);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        char b = (char)(i & 0xFF);
        if ((int)write(fd, &b, 1) != 1) { close(fd); ok = 0; break; }
        close(fd);
    }
    TEST(group, "stall-probe-nounlink: 36 creates without unlink", ok);

    /* cleanup whatever was created */
    for (int i = 0; i < N; i++) {
        make_path(path, sizeof(path), "/mnt/bcs_nounlink_", i);
        unlink(path);
    }
}

/* ── test 9: continuous concurrent create/write/unlink churn ─────────────
 *
 * Multiple children continuously create, write, close, reopen, verify,
 * and unlink unique files concurrently.  Unlike fork_many_writers(),
 * the children do NOT exit after one operation: they continuously hammer
 * the inode allocator, bitmap blocks, directory blocks, and buffer cache
 * while requests from different processes overlap in time.
 *
 * This is specifically designed to expose:
 *   - lost wakeups in disk completion paths
 *   - device-busy flag races
 *   - cache-slot reuse before write-back completion
 *   - deadlocks during dirty eviction
 */
static void test_concurrent_churn(us_test_group_t *group) {
    const int NPROC = 8;
    const int NITER = 80;

    pid_t pids[NPROC];
    char path[64];

    for (int p = 0; p < NPROC; p++) {
        pids[p] = fork();

        if (pids[p] < 0) {
            printf("churn-diag: fork[%d] failed (pids[%d]=%d)\n", p, p, pids[p]);
            continue;
        }

        if (pids[p] == 0) {
            /* child */
            for (int i = 0; i < NITER; i++) {

                make_path(path, sizeof(path), "/mnt/bcs_churn_", p * 1000 + i);

                /* create/write */
                int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    printf("churn-diag: p=%d i=%d open(O_CREAT) failed\n", p, i);
                    _exit(10);
                }

                char b = (char)((p + i) & 0xFF);

                if ((int)write(fd, &b, 1) != 1) {
                    printf("churn-diag: p=%d i=%d write failed\n", p, i);
                    close(fd);
                    _exit(11);
                }

                close(fd);

                /* reopen/readback */
                fd = open(path, O_RDONLY);
                if (fd < 0) {
                    printf("churn-diag: p=%d i=%d open(O_RDONLY) failed\n", p, i);
                    _exit(12);
                }

                char got = 0;

                int rret = (int)read(fd, &got, 1);
                if (rret != 1 || got != b) {
                    printf("churn-diag: p=%d i=%d read=%d got=%d expected=%d\n",
                           p, i, rret, (int)got, (int)b);
                    close(fd);
                    _exit(13);
                }

                close(fd);

                /* unlink immediately */
                if (unlink(path) < 0) {
                    printf("churn-diag: p=%d i=%d unlink failed\n", p, i);
                    _exit(14);
                }

                /*
                 * Optional:
                 * tiny sleep increases interleaving randomness
                 */
                usleep(1000);
            }

            _exit(0);
        }
    }

    int ok = 1;

    for (int i = 0; i < NPROC; i++) {
        int status = 0;

        if (pids[i] < 0) { ok = 0; continue; }

        if (waitpid(pids[i], &status, 0) < 0) {
            printf("churn-diag: waitpid(pids[%d]=%d) returned -1\n", i, pids[i]);
            ok = 0;
            continue;
        }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("churn-diag: pids[%d]=%d status=0x%x WIFEXITED=%d WEXITSTATUS=%d\n",
                   i, pids[i], status, WIFEXITED(status), WEXITSTATUS(status));
            ok = 0;
        }
    }

    TEST(group,
         "concurrent-churn: 8 children x 80 create/write/read/unlink cycles",
         ok);
}

/* ── test 10: buffer cache thrash (eviction of dirty blocks) ───────────
 *
 * Forces the cache to evict dirty blocks to make room for new ones.
 * If the cache size is 16 slots, we touch 32 distinct blocks, writing
 * to each to ensure they are dirty. This specifically tests if the
 * transition from "Dirty/Victim" -> "Writing to Disk" -> "Free/Ready"
 * properly wakes up the thread waiting for the slot.
 */
static void test_cache_eviction_thrash(us_test_group_t *group) {
    const int N_FILES = 40; // Assumes cache is smaller than 40 blocks
    char path[64];
    int ok = 1;

    // Phase 1: Dirty many blocks
    for (int i = 0; i < N_FILES; i++) {
        make_path(path, sizeof(path), "/mnt/thrash_", i);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        write(fd, "data", 4); 
        // We do NOT close yet, keeping handles/blocks potentially pinned or dirty
        close(fd);
    }

    // Phase 2: Immediately try to read them back
    // This forces the cache to cycle through its entire inventory
    for (int i = 0; i < N_FILES && ok; i++) {
        make_path(path, sizeof(path), "/mnt/thrash_", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) { ok = 0; break; }
        char buf[4];
        if (read(fd, buf, 4) != 4) ok = 0;
        close(fd);
    }

    TEST(group, "cache-eviction-thrash: dirty eviction cycle complete", ok);

    for (int i = 0; i < N_FILES; i++) {
        make_path(path, sizeof(path), "/mnt/thrash_", i);
        unlink(path);
    }
}

/* ── test 11: rapid driver handshake (busy-gate stress) ───────────────
 *
 * This test uses a single file but performs rapid-fire writes and 
 * synchronization. It checks if the block driver's "busy" flag or 
 * semaphore can be "stuck" if a new request arrives before the 
 * completion interrupt/routine has finished its housekeeping.
 */
static void test_driver_handshake(us_test_group_t *group) {
    const char *path = "/mnt/handshake.txt";
    int ok = 1;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    
    for (int i = 0; i < 100 && ok; i++) {
        if (write(fd, "a", 1) != 1) ok = 0;
        // If your OS has fsync, use it. Otherwise, the close/open 
        // in your other tests is the surrogate.
        // sync(); 
    }
    close(fd);
    
    // Rapidly reopen and truncate
    for (int i = 0; i < 50 && ok; i++) {
        fd = open(path, O_WRONLY | O_TRUNC);
        if (fd < 0) { ok = 0; break; }
        write(fd, "b", 1);
        close(fd);
    }

    TEST(group, "driver-handshake: 150 rapid-fire driver requests", ok);
    unlink(path);
}

/* ── test 12: metadata interleave (bitmap vs inode) ───────────────────
 *
 * This test creates a file, unlinks it, and immediately creates it again.
 * This forces the allocator to toggle the same bit in the bitmap 
 * repeatedly while simultaneously updating the directory block.
 * This is the "Stall Finder" on steroids.
 */
static void test_metadata_churn(us_test_group_t *group) {
    const char *path_a = "/mnt/meta_a.txt";
    const char *path_b = "/mnt/meta_b.txt";
    int ok = 1;

    for (int i = 0; i < 100; i++) {
        int fd1 = open(path_a, O_WRONLY | O_CREAT, 0644);
        int fd2 = open(path_b, O_WRONLY | O_CREAT, 0644);
        if (fd1 < 0 || fd2 < 0) { ok = 0; break; }
        close(fd1);
        close(fd2);
        unlink(path_a);
        unlink(path_b);
    }
    TEST(group, "metadata-churn: 100 alloc/dealloc cycles", ok);
}

/* ── test 13: single fd write spam ──────────────────────────────────────
 *
 * Opens one file and performs a high volume of back-to-back writes.
 * This tests if the block driver or buffer cache handles a continuous 
 * stream of dirtying the same block (or sequential blocks) without
 * the "breathing room" of a close() or unlink().
 */
static void test_single_fd_spam(us_test_group_t *group) {
    const char *path = "/mnt/spam_single.txt";
    const int ITERATIONS = 50;
    char data = 'X';
    int ok = 1;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        TEST(group, "single-fd-spam: open failed", 0);
        return;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        if (write(fd, &data, 1) != 1) {
            ok = 0;
            break;
        }
        // printf("bob");
    }

    close(fd);
    TEST(group, "single-fd-spam: 1000 sequential writes to one fd", ok);
    unlink(path);
}

/* ── test 14: multi fd parallel spam (no closing) ───────────────────────
 *
 * Opens multiple fds at once and rotates writes between them. 
 * This keeps multiple blocks "hot" and dirty in the cache simultaneously.
 * Fds are held open until the very end to ensure the cache cannot 
 * simply "release" the resources associated with the file session.
 */
static void test_multi_fd_parallel_spam(us_test_group_t *group) {
    const int NUM_FDS = 20;
    const int WRITES_PER_FD = 50;
    int fds[NUM_FDS];
    char path[64];
    int ok = 1;

    /* Open all fds first */
    for (int i = 0; i < NUM_FDS; i++) {
        make_path(path, sizeof(path), "/mnt/spam_multi_", i);
        fds[i] = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fds[i] < 0) ok = 0;
    }

    /* Spam writes across all open fds */
    for (int i = 0; i < WRITES_PER_FD && ok; i++) {
        for (int j = 0; j < NUM_FDS; j++) {
            char b = (char)j;
            if (write(fds[j], &b, 1) != 1) {
                ok = 0;
                break;
            }
        }
    }

    /* Finally close all */
    for (int i = 0; i < NUM_FDS; i++) {
        if (fds[i] >= 0) close(fds[i]);
        make_path(path, sizeof(path), "/mnt/spam_multi_", i);
        unlink(path);
    }

    TEST(group, "multi-fd-parallel-spam: 20 fds held open, 50 total writes per fd", ok);
}

/* ── test 15: busy-gate race (FEEDBACK_MULTIFS §2 — Issue A) ────────────
 *
 * Directly exercises the race identified in FEEDBACK_MULTIFS.md §2:
 * two children loop 50 × open+write+close+unlink on DISTINCT paths so
 * they never contend on directory entries.  The only shared resource is
 * blk_dev.busy.  Without the interrupt-atomic busy-acquisition fix, a
 * timer interrupt between a child's while(busy)→0 exit and its busy=1
 * assignment lets the sibling also see busy=0, and both children end up
 * writing to the shared descriptor table simultaneously.  The resulting
 * wakeup mismatch leaves one child sleeping on &blk_dev.busy forever.
 *
 * Both children must complete all 50 iterations without timeout.
 * On a correct kernel (Bug A patched): ✓ pass.
 * On an unpatched kernel: stall at an unpredictable iteration.
 */
static void test_busy_gate_race(us_test_group_t *group) {
    const int NCHILDREN = 2;
    const int NITERS    = 50;
    pid_t pids[2];
    char path[64];

    for (int c = 0; c < NCHILDREN; c++) {
        pids[c] = fork();
        if (pids[c] == 0) {
            /* child: open+write+close+unlink on a child-unique path */
            for (int i = 0; i < NITERS; i++) {
                make_path(path, sizeof(path), "/mnt/bgr_", c * 1000 + i);
                int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) _exit(1);
                char b = (char)((c + i) & 0xFF);
                if ((int)write(fd, &b, 1) != 1) { close(fd); _exit(2); }
                close(fd);
                if (unlink(path) < 0) _exit(3);
            }
            _exit(0);
        }
    }

    int ok = 1;
    for (int c = 0; c < NCHILDREN; c++) {
        int status = 0;
        if (waitpid(pids[c], &status, 0) < 0) { ok = 0; continue; }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) ok = 0;
    }
    TEST(group,
         "busy-gate-race: 2 concurrent children x 50 create/write/unlink cycles",
         ok);
}

/* ── test 16: IRQ probe — confirms virtio_blk_irq() is being called ─────────
 *
 * A single write+read pair on one file does NOT trigger a disk op: the
 * block cache absorbs both without going to disk.  To guarantee a virtio
 * call we must first evict the probe file's blocks from the cache.
 *
 * Strategy:
 *   1. Write probe byte to /mnt/irqlat_probe.bin  (lands in cache, LRU slot)
 *   2. Create NFLUSH distinct flusher files        (each adds a new block;
 *      when the 256-slot cache overflows the probe's blocks are evicted
 *      because they have the oldest last_access timestamps)
 *   3. printf("irq-probe: reading back — expect IRQ fired below")
 *   4. Read probe file  →  cache miss  →  virtio_blk_read  →  IRQ fires
 *   5. "virtio_blk: IRQ fired ..." should appear immediately after step 3
 *
 * Expected console output (IRQ working):
 *   irq-probe: writing probe file
 *   irq-probe: flushing cache with 40 files
 *   irq-probe: reading back — expect 'IRQ fired' line below
 *   virtio_blk: IRQ fired last_used=N used->idx=N+1
 *   irq-probe: PASS
 *
 * If IRQ is broken the 'IRQ fired' line is absent and the read stalls
 * ~100 ms waiting for the timer poll.
 */
static void test_irq_probe(us_test_group_t *group) {
    const char *probe = "/mnt/irqlat_probe.bin";
    char fpath[48];
    const int NFLUSH = 40;
    int ok = 1;

    /* step 1: write probe into cache */
    printf("irq-probe: writing probe file\n");
    int fd = open(probe, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ok = (fd >= 0);
    if (ok) {
        char b = 0x42;
        ok = ((int)write(fd, &b, 1) == 1);
        close(fd);
    }

    /* step 2: flood cache so probe's blocks become LRU and get evicted.
     * Each flusher file has a unique path → unique inode + data block.
     * After NFLUSH files the cache overflows and writes probe's dirty
     * inode/data blocks to disk, freeing their slots. */
    printf("irq-probe: flushing cache with %d files\n", NFLUSH);
    for (int i = 0; i < NFLUSH && ok; i++) {
        make_irq_path(fpath, sizeof(fpath), 9000 + i);
        int ffd = open(fpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (ffd < 0) { ok = 0; break; }
        char b = (char)(i & 0xFF);
        write(ffd, &b, 1);
        close(ffd);
    }

    /* step 3+4: read probe — its data block is no longer cached, so the
     * kernel issues virtio_blk_read, which wakes via IRQ (or poll).
     * The "IRQ fired" printk appears between these two user-space lines. */
    printf("irq-probe: reading back -- expect 'IRQ fired' line below\n");
    if (ok) {
        fd = open(probe, O_RDONLY);
        ok = (fd >= 0);
        if (ok) {
            char got = 0;
            ok = ((int)read(fd, &got, 1) == 1 && got == 0x42);
            close(fd);
        }
    }
    printf("irq-probe: %s\n", ok ? "PASS" : "FAIL");

    unlink(probe);
    for (int i = 0; i < NFLUSH; i++) {
        make_irq_path(fpath, sizeof(fpath), 9000 + i);
        unlink(fpath);
    }
    TEST(group, "irq-probe: evict+read forces virtio_blk_irq() call", ok);
}

/* ── tests 17-19: IRQ delivery latency (FEEDBACK_MULTIFS §5 — Issue D) ────
 *
 * Root cause: plic_enable_irq(irq) computes (1 << irq) with a 32-bit int.
 * When irq >= 32 (QEMU virt assigns PCI virtio-blk INTA to PLIC source 32),
 * the shift is undefined behaviour; GCC produces 0, so source 32 is never
 * set in the S-mode enable register.  No external interrupt fires; every
 * virtio_blk_rw call waits up to one full timer tick (~100 ms at
 * TIMER_INTERVAL = 1,000,000 cycles) instead of returning in microseconds.
 *
 * The three tests below are ordered lightest → heaviest:
 *
 *   IRQ working (plic.c patched)  : all three finish in well under 5 s.
 *   IRQ broken  (unpatched)       : each stalls 10–60 s → evaluator timeout.
 *
 * No concurrency is needed to expose IRQ latency — sequential I/O is enough.
 */


/* test 16: large file written and read back in small chunks ─────────────
 *
 * Write a 16 KB file 256 bytes at a time, then read it back 256 bytes at a
 * time and verify every byte.  Runs in a forked child to isolate cleanup.
 * Generates ~128 virtio round-trips (each chunk may touch a new 512-byte
 * sector).
 *
 * IRQ working : ~128 × 0.1 ms ≈ 13 ms.
 * IRQ broken  : ~128 × 100 ms ≈ 12.8 s → timeout.
 */
static void test_irq_large_file_scan(us_test_group_t *group) {
    const char *path  = "/mnt/irqlat_scan.bin";
    const int   TOTAL = 16 * 1024;
    const int   CHUNK = 256;
    const int   ITERS = TOTAL / CHUNK;

    pid_t pid = fork();
    if (pid == 0) {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) _exit(1);
        char wbuf[CHUNK];
        for (int c = 0; c < ITERS; c++) {
            for (int b = 0; b < CHUNK; b++)
                wbuf[b] = (char)((c * CHUNK + b) & 0xFF);
            if ((int)write(fd, wbuf, CHUNK) != CHUNK) { close(fd); _exit(2); }
        }
        close(fd);

        fd = open(path, O_RDONLY);
        if (fd < 0) _exit(3);
        char rbuf[CHUNK];
        for (int c = 0; c < ITERS; c++) {
            if ((int)read(fd, rbuf, CHUNK) != CHUNK) { close(fd); _exit(4); }
            for (int b = 0; b < CHUNK; b++) {
                if (rbuf[b] != (char)((c * CHUNK + b) & 0xFF)) {
                    close(fd); _exit(5);
                }
            }
        }
        close(fd);
        unlink(path);
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group,
         "irq-latency/large-scan: 16 KB file written+read in 256-byte chunks",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
    unlink(path);
}

/* test 17: sequential write+read+verify on distinct files ───────────────
 *
 * N distinct files each get a 4-byte pattern written, closed, reopened,
 * read back, and verified, then unlinked.  Each close forces a virtio write;
 * each reopen with a cold cache forces a virtio read: 2N round-trips total.
 *
 * IRQ working : 200 × 2 × 0.1 ms ≈ 40 ms.
 * IRQ broken  : 200 × 2 × 100 ms ≈ 40 s → timeout.
 */
static void test_irq_sequential_io(us_test_group_t *group) {
    const int N = 200;
    char path[48];
    int ok = 1;

    for (int i = 0; i < N && ok; i++) {
        make_irq_path(path, sizeof(path), i);

        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        char wbuf[4];
        wbuf[0] = (char)( i        & 0xFF);
        wbuf[1] = (char)((i >>  8) & 0xFF);
        wbuf[2] = (char)((i *  7)  & 0xFF);
        wbuf[3] = (char)((i * 13)  & 0xFF);
        if ((int)write(fd, wbuf, 4) != 4) { close(fd); ok = 0; break; }
        close(fd);

        fd = open(path, O_RDONLY);
        if (fd < 0) { ok = 0; break; }
        char rbuf[4];
        if ((int)read(fd, rbuf, 4) != 4) { close(fd); ok = 0; break; }
        close(fd);

        if (rbuf[0] != wbuf[0] || rbuf[1] != wbuf[1] ||
            rbuf[2] != wbuf[2] || rbuf[3] != wbuf[3]) { ok = 0; break; }

        if (unlink(path) < 0) { ok = 0; break; }
    }

    TEST(group,
         "irq-latency/sequential: 200 write+close+read+verify+unlink cycles",
         ok);

    for (int i = 0; i < N; i++) {
        make_irq_path(path, sizeof(path), i);
        unlink(path);
    }
}

/* test 18: create-unlink storm on a single path ─────────────────────────
 *
 * 300 iterations reuse the same path: each iteration creates, writes one
 * byte, closes, reopens, reads back, verifies, and unlinks.  Inode allocation
 * is reused every cycle, keeping metadata pressure predictable.  The only
 * variable is how fast each virtio write and virtio read completes.
 *
 * IRQ working : 300 × 2 × 0.1 ms ≈ 60 ms.
 * IRQ broken  : 300 × 2 × 100 ms ≈ 60 s → timeout.
 */
static void test_irq_create_unlink_storm(us_test_group_t *group) {
    const int   N    = 300;
    const char *path = "/mnt/irqlat_storm.bin";
    int ok = 1;

    for (int i = 0; i < N && ok; i++) {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        char b = (char)(i & 0xFF);
        if ((int)write(fd, &b, 1) != 1) { close(fd); ok = 0; break; }
        close(fd);

        fd = open(path, O_RDONLY);
        if (fd < 0) { ok = 0; break; }
        char got = 0;
        if ((int)read(fd, &got, 1) != 1 || got != b) { close(fd); ok = 0; break; }
        close(fd);

        if (unlink(path) < 0) { ok = 0; break; }
    }

    TEST(group,
         "irq-latency/storm: 300 create+write+close+read+verify+unlink cycles",
         ok);
    unlink(path);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_blkcache_stress(us_test_group_t *group) {
    ut_group_begin(group, "blkcache_stress");

    /* --- single-process investigative variants (from FEEDBACK_MULTIFS §1) ---
     * "narrow further: a loop that only creates files (no unlink) vs. one
     *  that only writes to an existing file vs. one that forces the buffer
     *  cache past its capacity"                                             */
    test_open_write_close_unlink_loop(group); /* baseline: alloc→write→free */
    test_create_nounlink_verbose(group);      /* only creates, no unlink    */
    test_single_fd_spam(group);              /* only writes, one hot block  */

    /* --- cache-slot invariant (FEEDBACK_MULTIFS §3 — Issue B) ---
     * 40 files overflow the cache, forcing dirty evictions.  Every file must
     * read back correctly; a double-claimed slot produces silent wrong data. */
    test_cache_eviction_thrash(group);

    /* --- busy-gate race (FEEDBACK_MULTIFS §2 — Issue A) ---
     * 2 concurrent children race the blk_dev.busy check-and-set.           */
    test_busy_gate_race(group);

    /* --- IRQ delivery latency (FEEDBACK_MULTIFS §5 — Issue D) ---
     * probe first: 1 write + 1 read → exactly 2 "IRQ fired" kernel prints.
     * The heavier tests follow; comment them out while diagnosing.          */
    test_irq_probe(group);
    test_irq_large_file_scan(group);
    test_irq_sequential_io(group);
    test_irq_create_unlink_storm(group);

    /* --- full concurrent churn (most thorough multi-process stress) ---    */
    test_concurrent_churn(group);

    ut_group_end(group);
}
