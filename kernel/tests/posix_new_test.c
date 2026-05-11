#include "kernel/printk.h"
#include "kernel/types.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "process/process.h"
#include "tests/test.h"
#include "lib/string.h"
#include "errno.h"
#include "trap/syscall.h"

static int passed = 0;
static int failed = 0;

static struct proc *_prev_current = NULL;

static int bind_init(void) {
    _prev_current = current;
    struct proc *init = proc_find(1);
    if (!init || !init->fs) { _prev_current = NULL; return -1; }
    current = init;
    return 0;
}

static void restore_current(void) {
    current = _prev_current;
    _prev_current = NULL;
}

/* =========================================================================
 * 1. umask
 * ====================================================================== */
static void test_umask(void) {
    printk("--- umask ---\n");

    mode_t old = sys_umask(022);
    TEST("umask: initial default is 022", old == 022);

    old = sys_umask(027);
    TEST("umask: returns previous 022", old == 022);

    old = sys_umask(022);
    TEST("umask: returns previous 027", old == 027);

    sys_umask(022); /* leave at default */
}

/* =========================================================================
 * 2. ftruncate + truncate
 * ====================================================================== */
static void test_ftruncate(void) {
    printk("--- ftruncate / truncate ---\n");

    const char *path = "/tmp/trunc_test.txt";
    const char *data = "Hello, World!"; /* 13 bytes */

    int fd = sys_open(path, 0x241 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644);
    TEST("ftruncate: open test file", fd >= 0);
    if (fd < 0) return;

    ssize_t nw = sys_write(fd, data, 13);
    TEST("ftruncate: wrote 13 bytes", nw == 13);

    int ret = sys_ftruncate(fd, 0);
    TEST("ftruncate(fd, 0) succeeds", ret == 0);

    struct stat st;
    ret = sys_fstat(fd, &st);
    TEST("ftruncate: size 0 after ftruncate(0)", ret == 0 && st.st_size == 0);

    /* write again, truncate to 5 */
    file_lseek(fd, 0, 0 /* SEEK_SET */);
    sys_write(fd, data, 13);
    ret = sys_ftruncate(fd, 5);
    TEST("ftruncate(fd, 5) succeeds", ret == 0);
    ret = sys_fstat(fd, &st);
    TEST("ftruncate: size 5 after ftruncate(5)", ret == 0 && st.st_size == 5);

    sys_close(fd);

    /* truncate via path */
    ret = sys_truncate(path, 3);
    TEST("truncate(path, 3) succeeds", ret == 0);
    ret = sys_stat(path, &st);
    TEST("truncate: size 3 after truncate(path,3)", ret == 0 && st.st_size == 3);

    ret = sys_truncate(path, 0);
    TEST("truncate(path, 0) succeeds", ret == 0);
    ret = sys_stat(path, &st);
    TEST("truncate: size 0 after truncate(path,0)", ret == 0 && st.st_size == 0);

    /* error cases */
    ret = sys_ftruncate(999, 0);
    TEST("ftruncate: bad fd → error", ret < 0);

    ret = sys_truncate("/no/such/file/xyz", 0);
    TEST("truncate: nonexistent path → error", ret < 0);

    sys_unlink(path);
}

/* =========================================================================
 * 3. chown / lchown / fchown (single-user stubs — must return 0)
 * ====================================================================== */
static void test_chown(void) {
    printk("--- chown family ---\n");

    const char *path = "/tmp/chown_test.txt";
    int fd = sys_open(path, 0x241, 0644);
    TEST("chown: created test file", fd >= 0);
    if (fd < 0) return;

    int ret = sys_fchown(fd, 0, 0);
    TEST("fchown(fd,0,0) returns 0", ret == 0);
    sys_close(fd);

    ret = sys_chown(path, 0, 0);
    TEST("chown(path,0,0) returns 0", ret == 0);

    ret = sys_lchown(path, 0, 0);
    TEST("lchown(path,0,0) returns 0", ret == 0);

    ret = sys_chown("/no/such/path/xyz", 0, 0);
    TEST("chown: nonexistent path → error", ret < 0);

    sys_unlink(path);
}

/* =========================================================================
 * 4. utimensat
 * ====================================================================== */
static void test_utimensat(void) {
    printk("--- utimensat ---\n");

    const char *path = "/tmp/utime_test.txt";
    int fd = sys_open(path, 0x241, 0644);
    TEST("utimensat: created test file", fd >= 0);
    if (fd < 0) return;
    sys_close(fd);

    struct timespec times[2];
    times[0].tv_sec  = 1000; times[0].tv_nsec = 0;
    times[1].tv_sec  = 2000; times[1].tv_nsec = 0;

    int ret = sys_utimensat(AT_FDCWD, path, times, 0);
    TEST("utimensat(AT_FDCWD, path) succeeds", ret == 0);

    struct stat st;
    ret = sys_stat(path, &st);
    TEST("utimensat: atime updated to 1000", ret == 0 && st.st_atim.tv_sec == 1000);
    TEST("utimensat: mtime updated to 2000", ret == 0 && st.st_mtim.tv_sec == 2000);

    /* UTIME_OMIT: leave atime unchanged */
    times[0].tv_nsec = 0x3ffffffe; /* UTIME_OMIT */
    times[1].tv_sec  = 3000; times[1].tv_nsec = 0;
    ret = sys_utimensat(AT_FDCWD, path, times, 0);
    TEST("utimensat UTIME_OMIT succeeds", ret == 0);
    ret = sys_stat(path, &st);
    TEST("utimensat: atime unchanged with UTIME_OMIT", ret == 0 && st.st_atim.tv_sec == 1000);
    TEST("utimensat: mtime updated to 3000", ret == 0 && st.st_mtim.tv_sec == 3000);

    /* futimens via fd (NULL path) */
    fd = sys_open(path, 0x0 /* O_RDONLY */, 0);
    if (fd >= 0) {
        times[0].tv_sec  = 5000; times[0].tv_nsec = 0;
        times[1].tv_sec  = 6000; times[1].tv_nsec = 0;
        ret = sys_utimensat(fd, NULL, times, 0);
        TEST("futimens via utimensat(fd,NULL) succeeds", ret == 0);
        sys_close(fd);
        ret = sys_stat(path, &st);
        TEST("futimens: atime updated to 5000", ret == 0 && st.st_atim.tv_sec == 5000);
        TEST("futimens: mtime updated to 6000", ret == 0 && st.st_mtim.tv_sec == 6000);
    } else {
        TEST("futimens: open for test skipped", 0);
        TEST("futimens: atime check skipped", 0);
        TEST("futimens: mtime check skipped", 0);
    }

    ret = sys_utimensat(AT_FDCWD, "/no/such/file/xyz", times, 0);
    TEST("utimensat: nonexistent path → error", ret < 0);

    sys_unlink(path);
}

/* =========================================================================
 * Entry point
 * ====================================================================== */
void posix_new_test_run(void) {
    printk("=== posix_new tests (ftruncate/truncate/umask/chown/utimensat) ===\n");

    if (bind_init() < 0) {
        printk("posix_new_test: cannot bind to init proc, skipping\n");
        return;
    }

    test_umask();
    test_ftruncate();
    test_chown();
    test_utimensat();

    restore_current();
    printk("posix_new tests: %d passed, %d failed\n", passed, failed);
}
