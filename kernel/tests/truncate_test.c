/*
 * truncate_test.c — kernel-level tests for sys_ftruncate() and sys_truncate().
 *
 * Runs at boot time in supervisor mode (RUN_TESTS).  Tests use tmpfs (/tmp)
 * so no persistent-disk side-effects.  current is temporarily bound to the
 * init process (same pattern as fs_test.c) for path-resolution to work.
 */

#include "trap/syscall.h"
#include "kernel/resource.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "process/process.h"
#include "tests/test.h"
#include "kernel/printk.h"
#include "lib/string.h"
#include "mm/kmalloc.h"
#include "errno.h"

static int passed = 0;
static int failed = 0;

static struct proc *saved_current = NULL;

static int bind_init(void) {
    saved_current = current;
    struct proc *init = proc_find(1);
    if (!init || !init->fs) { saved_current = NULL; return -1; }
    current = init;
    return 0;
}

static void unbind(void) {
    if (saved_current) current = saved_current;
    saved_current = NULL;
}

/* Write len bytes of data to path; return 0 on success. */
static int write_file(const char *path, const char *data, int len) {
    int fd = file_open_with_flags(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int w = file_write(fd, data, len);
    file_close(fd);
    return (w == len) ? 0 : -1;
}

void truncate_test_run(void) {
    printk("\n========================================\n");
    printk("   sys_ftruncate / sys_truncate Tests\n");
    printk("========================================\n\n");

    if (bind_init() < 0) {
        printk("  [SKIP] no init proc\n");
        return;
    }

    const char *path = "/tmp/ktrunc.txt";
    const char *data = "Hello, truncate!";   /* 16 bytes */
    const int   dlen = 16;
    struct stat st;

    vfs_unlink(path);

    /* ── setup ───────────────────────────────────────────────────────── */
    TEST("setup: create file", write_file(path, data, dlen) == 0);
    TEST("setup: initial size 16",
         file_stat(path, &st) == 0 && st.st_size == dlen);

    /* ── ftruncate to zero ───────────────────────────────────────────── */
    int fd = file_open_with_flags(path, O_WRONLY, 0);
    TEST("ftruncate(0): open", fd >= 0);
    if (fd >= 0) {
        TEST("ftruncate(0): returns 0", sys_ftruncate(fd, 0) == 0);
        file_close(fd);
        TEST("ftruncate(0): size == 0",
             file_stat(path, &st) == 0 && st.st_size == 0);
    }

    /* ── rewrite, partial shrink ─────────────────────────────────────── */
    write_file(path, data, dlen);
    fd = file_open_with_flags(path, O_WRONLY, 0);
    TEST("ftruncate(5): open", fd >= 0);
    if (fd >= 0) {
        TEST("ftruncate(5): returns 0", sys_ftruncate(fd, 5) == 0);
        file_close(fd);
        TEST("ftruncate(5): size == 5",
             file_stat(path, &st) == 0 && st.st_size == 5);

        char buf[8] = {0};
        int rfd = file_open_with_flags(path, O_RDONLY, 0);
        if (rfd >= 0) {
            int n = file_read(rfd, buf, sizeof(buf));
            file_close(rfd);
            TEST("ftruncate(5): read 5 bytes", n == 5);
            TEST("ftruncate(5): content correct", strncmp(buf, data, 5) == 0);
        }
    }

    /* ── extend (sparse) ─────────────────────────────────────────────── */
    fd = file_open_with_flags(path, O_WRONLY, 0);
    TEST("ftruncate(extend): open", fd >= 0);
    if (fd >= 0) {
        TEST("ftruncate(100): returns 0", sys_ftruncate(fd, 100) == 0);
        file_close(fd);
        TEST("ftruncate(100): size == 100",
             file_stat(path, &st) == 0 && st.st_size == 100);
    }

    /* ── error: bad fd ───────────────────────────────────────────────── */
    TEST("ftruncate(EBADF): bad fd", sys_ftruncate(9999, 0) == -EBADF);

    /* ── error: negative length ──────────────────────────────────────── */
    fd = file_open_with_flags(path, O_WRONLY, 0);
    if (fd >= 0) {
        TEST("ftruncate(-1): -EINVAL", sys_ftruncate(fd, -1) == -EINVAL);
        file_close(fd);
    }

    /* ── error: read-only fd ─────────────────────────────────────────── */
    fd = file_open_with_flags(path, O_RDONLY, 0);
    if (fd >= 0) {
        TEST("ftruncate(RDONLY fd): -EACCES", sys_ftruncate(fd, 0) == -EACCES);
        file_close(fd);
    }

    /* ── sys_truncate by path ────────────────────────────────────────── */
    write_file(path, data, dlen);
    TEST("sys_truncate(path,7): returns 0", sys_truncate(path, 7) == 0);
    TEST("sys_truncate(path,7): size == 7",
         file_stat(path, &st) == 0 && st.st_size == 7);

    TEST("sys_truncate(path,0): returns 0", sys_truncate(path, 0) == 0);
    TEST("sys_truncate(path,0): size == 0",
         file_stat(path, &st) == 0 && st.st_size == 0);

    /* ── error: missing path → -ENOENT ──────────────────────────────── */
    TEST("sys_truncate(missing): -ENOENT",
         sys_truncate("/tmp/no_such_kfile_trunctest.txt", 0) == -ENOENT);

    /* ── error: negative length → -EINVAL ───────────────────────────── */
    TEST("sys_truncate(negative): -EINVAL",
         sys_truncate(path, -1) == -EINVAL);

    vfs_unlink(path);
    unbind();

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
