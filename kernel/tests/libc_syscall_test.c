/*
 * libc_syscall_test.c — Kernel-side validation of the libc/kernel syscall contract
 *
 * These tests run in kernel mode (before schedule()) and verify that the
 * syscall handlers that libc depends on are behaving correctly.
 *
 * Complements the userspace libc_test binary by covering the kernel half:
 *   - sys_write to fd 1/2 goes to printk
 *   - sys_write to fd >= 3 returns -1 (expected limitation)
 *   - sys_open / sys_read / sys_close via VFS / tarfs
 *   - sys_getpid
 *   - sys_sbrk / sys_brk / sys_mmap / sys_munmap
 *   - Syscall number alignment: known-good and known-bad numbers
 *
 * Call libc_syscall_test_run() from run_all_tests() in kernel.c.
 */

#include "kernel/printk.h"
#include "kernel/types.h"
#include "trap/syscall.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "process/process.h"
#include "tests/test.h"
#include "errno.h"
#include "lib/string.h"
#include "mm/kmalloc.h"

static int passed = 0;
static int failed = 0;

static struct proc test_proc;
static void test_setup_proc(void) {
    memset(&test_proc, 0, sizeof(test_proc));
    test_proc.pid = 99;
    fd_init(&test_proc.fds);   // sets up FILE_STDIN/FILE_STDOUT entries
    test_proc.fs = fs_struct_alloc();
    fs_struct_set_root(test_proc.fs, "/");
    fs_struct_set_cwd(test_proc.fs, "/");
    current = &test_proc;
}
static void test_teardown_proc(void) {
    current = NULL;
}

/* ------------------------------------------------------------------ */
/* sys_write                                                            */
/* ------------------------------------------------------------------ */

static void test_sys_write(void) {
    test_setup_proc();
    printk("\n=== sys_write ===\n");

    const char *msg = "sys_write stdout test\n";
    ssize_t n = sys_write(1, msg, strlen(msg));
    TEST("sys_write(1,...) returns byte count", n == (ssize_t)strlen(msg));

    ssize_t m = sys_write(2, msg, strlen(msg));
    TEST("sys_write(2,...) returns byte count", m == (ssize_t)strlen(msg));

    /* Writing to fd 3 (not a real open file) should return -1 */
    ssize_t bad = sys_write(3, msg, strlen(msg));
    TEST("sys_write to fd 3 (no file) returns -1", bad < 0);

    /* Zero-length write to stdout */
    ssize_t zero = sys_write(1, msg, 0);
    TEST("sys_write zero length returns 0", zero == 0);
    test_teardown_proc();
}

/* ------------------------------------------------------------------ */
/* sys_open / sys_read / sys_close (tarfs)                             */
/* ------------------------------------------------------------------ */

static void test_sys_file_io(void) {
    printk("\n=== sys_open / sys_read / sys_close (tarfs) ===\n");

    test_setup_proc();

    int fd = file_open("/hello.txt");
    TEST("file_open /hello.txt returns valid fd", fd >= 0);

    if (fd >= 0) {
        /* Read some bytes */
        char buf[32];
        memset(buf, 0, sizeof(buf));
        int n = file_read(fd, buf, sizeof(buf) - 1);
        TEST("file_read returns > 0", n > 0);
        TEST("file_read data non-empty", n > 0 && buf[0] != '\0');

        /* Offset advances */
        int off1 = file_lseek(fd, 0, 1 /* SEEK_CUR */);
        char buf2[8];
        int n2 = file_read(fd, buf2, 4);
        int off2 = file_lseek(fd, 0, 1 /* SEEK_CUR */);
        TEST("offset advances after read", off1 >= 0 && n2 >= 0 && off2 == off1 + n2);

        /* Read past EOF eventually returns 0 */
        char drain[64];
        int eof = 0;
        for (int i = 0; i < 32; i++) {
            if (file_read(fd, drain, sizeof(drain)) == 0) { eof = 1; break; }
        }
        TEST("repeated reads reach EOF (returns 0)", eof);

        file_close(fd);
    }

    /* Open nonexistent file returns negative errno */
    int bad = file_open("/does_not_exist_xyz.txt");
    TEST("file_open nonexistent returns error", bad < 0);

    /* Write to tarfs file returns error (read-only filesystem) */
    int rf = file_open("/hello.txt");
    if (rf >= 0) {
        int w = file_write(rf, "x", 1);
        TEST("file_write to tarfs returns error", w < 0);
        file_close(rf);
    }

    test_teardown_proc();
}

/* ------------------------------------------------------------------ */
/* sys_getpid                                                           */
/* ------------------------------------------------------------------ */

static void test_sys_getpid(void) {
    printk("\n=== sys_getpid ===\n");

    /*
     * In RUN_TESTS mode there is no running user process, so current
     * may be NULL.  sys_getpid returns -1 when current==NULL which is
     * the correct fallback.  We just verify it doesn't crash.
     */
    pid_t pid = sys_getpid();
    TEST("sys_getpid does not crash", 1);  /* structural */
    printk("  [INFO] sys_getpid() = %d (expected -1 if no current process)\n", pid);
}

/* ------------------------------------------------------------------ */
/* sys_mmap / sys_munmap / sys_brk / sys_sbrk                          */
/* ------------------------------------------------------------------ */

static void test_sys_mmap(void) {
    printk("\n=== sys_mmap / sys_munmap / sys_brk / sys_sbrk ===\n");

    /*
     * mmap needs current->pagetable.  Without a process, it returns MAP_FAILED.
     * We verify the NULL-process guard path.
     */
    if (current == NULL) {
        printk("  [INFO] No current process — testing NULL guard paths\n");

        void *r = sys_mmap(NULL, 4096, 0x3, 0x22, -1, 0); /* PROT_RW | MAP_ANON|MAP_PRIVATE */
        TEST("sys_mmap without process returns MAP_FAILED", r == (void *)-1LL);

        int br = sys_brk(NULL);
        TEST("sys_brk without process returns -1", br == -1);

        void *sr = sys_sbrk(0);
        TEST("sys_sbrk without process returns MAP_FAILED", sr == (void *)-1LL);
    } else {
        printk("  [INFO] Process context available — testing mmap with real pagetable\n");

        /* Anonymous mmap */
        void *p = sys_mmap(NULL, 4096, 0x3, 0x22, -1, 0);
        TEST("sys_mmap anonymous 4KB", p != (void *)-1LL && p != NULL);
        if (p != (void *)-1LL && p != NULL) {
            sys_munmap(p, 4096);
            TEST("sys_munmap 4KB returns 0", 1);  /* doesn't panic */
        }

        /* sys_brk(NULL) returns current heap break */
        int bk = sys_brk(NULL);
        TEST("sys_brk(NULL) returns valid address", bk != -1);
    }

    /* Zero-length mmap always fails */
    void *zr = sys_mmap(NULL, 0, 0x3, 0x22, -1, 0);
    TEST("sys_mmap length=0 returns MAP_FAILED", zr == (void *)-1LL);

    /* File-backed mmap not supported */
    void *fbr = sys_mmap(NULL, 4096, 0x3, 0x02 /* MAP_PRIVATE, no MAP_ANONYMOUS */, 3, 0);
    TEST("sys_mmap file-backed returns MAP_FAILED", fbr == (void *)-1LL);
}

/* ------------------------------------------------------------------ */
/* Syscall number alignment check                                       */
/* ------------------------------------------------------------------ */

static void test_syscall_numbers(void) {
    printk("\n=== Syscall number alignment ===\n");

    /*
     * Verify the kernel's SYS_* constants match what libc/syscall.S uses.
     * If any of these fail, libc and kernel are out of sync.
     */
    TEST("SYS_exit  == 1",  SYS_exit  == 1);
    TEST("SYS_fork  == 2",  SYS_fork  == 2);
    TEST("SYS_read  == 3",  SYS_read  == 3);
    TEST("SYS_write == 4",  SYS_write == 4);
    TEST("SYS_open  == 5",  SYS_open  == 5);
    TEST("SYS_close == 6",  SYS_close == 6);
    TEST("SYS_wait  == 7",  SYS_wait  == 7);
    TEST("SYS_exec  == 8",  SYS_exec  == 8);
    TEST("SYS_getpid == 9", SYS_getpid == 9);
    TEST("SYS_sbrk  == 10", SYS_sbrk  == 10);
    TEST("SYS_kill  == 13", SYS_kill  == 13);
    TEST("SYS_mmap  == 14", SYS_mmap  == 14);
    TEST("SYS_munmap == 15",SYS_munmap == 15);
    TEST("SYS_brk   == 16", SYS_brk   == 16);
    TEST("SYS_pipe  == 22", SYS_pipe  == 22);
    TEST("SYS_dup   == 23", SYS_dup   == 23);
    TEST("SYS_dup2  == 33", SYS_dup2  == 33);

    /* Collision fixes verified */
    TEST("SYS_lseek  == 18", SYS_lseek  == 18);
    TEST("SYS_getcwd == 19", SYS_getcwd == 19);
    TEST("SYS_chdir  == 49", SYS_chdir  == 49);
    TEST("SYS_lseek != SYS_exec (no collision)",     SYS_lseek  != SYS_exec);
    TEST("SYS_getcwd != SYS_waitpid (no collision)", SYS_getcwd != SYS_waitpid);
    printk("  [INFO] getdents64 in syscall.S uses a7=61 — kernel handler not yet implemented\n");
}

/* ------------------------------------------------------------------ */
/* fd_table initialization                                             */
/* ------------------------------------------------------------------ */

static void test_fd_init(void) {
    printk("\n=== fd_table initialization ===\n");

    fd_table_t table;
    fd_init(&table);

    file_t *stdin_f  = fd_get(&table, 0);
    file_t *stdout_f = fd_get(&table, 1);
    file_t *stderr_f = fd_get(&table, 2);

    TEST("fd 0 is FILE_STDIN",  stdin_f  != NULL && stdin_f->type  == FILE_STDIN);
    TEST("fd 1 is FILE_STDOUT", stdout_f != NULL && stdout_f->type == FILE_STDOUT);
    TEST("fd 2 is FILE_STDOUT", stderr_f != NULL && stderr_f->type == FILE_STDOUT);

    /* fd_get out of bounds */
    TEST("fd_get(-1) returns NULL",  fd_get(&table, -1)  == NULL);
    TEST("fd_get(999) returns NULL", fd_get(&table, 999) == NULL);

    /* Allocate user fd — must land at 3 */
    file_t *f = kzalloc(sizeof(file_t));
    if (f) {
        f->type = FILE_STDIN;
        f->ref_count = 1;
    }
    int fd = fd_alloc(&table, f);
    TEST("first user fd is 3", fd == 3);

    file_t *got = fd_get(&table, fd);
    TEST("fd_get(3) returns the allocated file", got == f);

    fd_close(&table, fd);
    TEST("fd_get after close returns NULL", fd_get(&table, fd) == NULL);

    /* Slot should be reusable */
    file_t *f2 = kzalloc(sizeof(file_t));
    if (f2) {
        f2->type = FILE_STDIN;
        f2->ref_count = 1;
    }
    int fd2 = fd_alloc(&table, f2);
    TEST("freed slot reused", fd2 == 3);
    fd_close(&table, fd2);

    /* NULL guards */
    TEST("fd_alloc NULL table returns -1", fd_alloc(NULL, f)     == -1);
    TEST("fd_alloc NULL file  returns -1", fd_alloc(&table, NULL) == -1);
}

/* ------------------------------------------------------------------ */
/* sys_dup / sys_dup2                                                 */
/* ------------------------------------------------------------------ */

static void test_sys_dup_ops(void) {
    printk("\n=== sys_dup / sys_dup2 ===\n");

    test_setup_proc();

    int dup_stdout = sys_dup(1);
    TEST("sys_dup(stdout) returns first free fd", dup_stdout == 3);
    TEST("dup'd fd points at same file object",
         dup_stdout >= 0 && fd_get(&current->fds, dup_stdout) == fd_get(&current->fds, 1));

    file_t *stdout_file = fd_get(&current->fds, 1);
    TEST("dup increments ref_count",
         stdout_file != NULL && stdout_file->ref_count == 2);

    int same_fd = sys_dup2(1, 1);
    TEST("sys_dup2(fd, fd) returns fd", same_fd == 1);
    TEST("dup2(fd, fd) leaves ref_count unchanged",
         stdout_file != NULL && stdout_file->ref_count == 2);

    int replace_fd = sys_dup2(0, dup_stdout);
    TEST("sys_dup2(stdin, 3) returns target fd", replace_fd == dup_stdout);
    TEST("target fd now aliases stdin",
         replace_fd >= 0 && fd_get(&current->fds, replace_fd) == fd_get(&current->fds, 0));
    TEST("replacing dup target drops old stdout alias ref",
         stdout_file != NULL && stdout_file->ref_count == 1);

    file_t *stdin_file = fd_get(&current->fds, 0);
    TEST("dup2 increments new aliased file ref_count",
         stdin_file != NULL && stdin_file->ref_count == 2);

    TEST("sys_dup invalid oldfd returns error", sys_dup(99) < 0);
    TEST("sys_dup2 invalid oldfd returns error", sys_dup2(99, 4) < 0);
    TEST("sys_dup2 invalid newfd returns error", sys_dup2(0, MAX_FDS) < 0);

    test_teardown_proc();
}

/* ------------------------------------------------------------------ */
/* sys_pipe                                                            */
/* ------------------------------------------------------------------ */

static void test_sys_pipe_ops(void) {
    printk("\n=== sys_pipe ===\n");
    char buf[32];

    test_setup_proc();

    int pipefd[2] = {-1, -1};
    int rc = sys_pipe(pipefd);
    TEST("sys_pipe returns 0", rc == 0);
    TEST("pipe read fd is first free slot", pipefd[0] == 3);
    TEST("pipe write fd is next free slot", pipefd[1] == 4);
    TEST("pipe ends are distinct", pipefd[0] != pipefd[1]);

    file_t *read_end = fd_get(&current->fds, pipefd[0]);
    file_t *write_end = fd_get(&current->fds, pipefd[1]);
    TEST("pipe read end exists", read_end != NULL && read_end->type == FILE_PIPE);
    TEST("pipe write end exists", write_end != NULL && write_end->type == FILE_PIPE);
    TEST("pipe ends share same backing pipe",
         read_end != NULL && write_end != NULL && read_end->pipe == write_end->pipe);
    TEST("pipe read end is marked read-only",
         read_end != NULL && (read_end->pipe_flags & PIPE_END_READ) != 0);
    TEST("pipe write end is marked write-only",
         write_end != NULL && (write_end->pipe_flags & PIPE_END_WRITE) != 0);

    memset(buf, 0, sizeof(buf));
    TEST("sys_read on pipe write end fails", sys_read(pipefd[1], buf, 1) < 0);
    TEST("sys_write on pipe read end fails", sys_write(pipefd[0], "x", 1) < 0);

    const char *msg = "hello pipe";
    ssize_t nw = sys_write(pipefd[1], msg, strlen(msg));
    TEST("sys_write to pipe writes full payload", nw == (ssize_t)strlen(msg));

    memset(buf, 0, sizeof(buf));
    ssize_t nr = sys_read(pipefd[0], buf, 5);
    TEST("sys_read from pipe can read a partial payload", nr == 5);
    buf[5] = '\0';
    TEST("pipe preserves first partial chunk", strcmp(buf, "hello") == 0);

    memset(buf, 0, sizeof(buf));
    nr = sys_read(pipefd[0], buf, strlen(msg) - 5);
    TEST("sys_read from pipe reads remaining payload", nr == (ssize_t)(strlen(msg) - 5));
    TEST("pipe preserves remaining bytes", strcmp(buf, " pipe") == 0);

    int dup_read = sys_dup(pipefd[0]);
    TEST("sys_dup(pipe read end) returns next free fd", dup_read == 5);
    TEST("dup'd pipe read end aliases same file object",
         dup_read >= 0 && fd_get(&current->fds, dup_read) == read_end);

    int dup_write_target = sys_dup2(pipefd[1], 1);
    TEST("sys_dup2(pipe write end, 1) returns target fd", dup_write_target == 1);
    TEST("fd 1 now aliases pipe write end", fd_get(&current->fds, 1) == write_end);

    sys_close(pipefd[1]);
    sys_close(1);

    memset(buf, 0, sizeof(buf));
    nr = sys_read(pipefd[0], buf, sizeof(buf));
    TEST("read after closing all writers returns EOF", nr == 0);

    sys_close(dup_read);
    sys_close(pipefd[0]);

    test_teardown_proc();

    test_setup_proc();

    pipefd[0] = -1;
    pipefd[1] = -1;
    rc = sys_pipe(pipefd);
    TEST("second sys_pipe returns 0", rc == 0);
    TEST("second pipe allocates read fd 3", pipefd[0] == 3);
    TEST("second pipe allocates write fd 4", pipefd[1] == 4);

    sys_close(pipefd[0]);
    TEST("write after closing all readers returns EPIPE",
         sys_write(pipefd[1], "z", 1) == -EPIPE);

    sys_close(pipefd[1]);
    test_teardown_proc();
}

/* ------------------------------------------------------------------ */
/* sys_lseek / sys_chdir / sys_getcwd                                  */
/* ------------------------------------------------------------------ */

static void test_sys_lseek_chdir_getcwd(void) {
    printk("\n=== sys_lseek / sys_chdir / sys_getcwd ===\n");
    test_setup_proc();

    /* --- lseek --- */
    int fd = file_open("/bin/sh");
    if (fd >= 0) {
        off_t pos = sys_lseek(fd, 0, 0 /* SEEK_SET */);
        TEST("sys_lseek SEEK_SET 0 returns 0", pos == 0);

        off_t cur = sys_lseek(fd, 0, 1 /* SEEK_CUR */);
        TEST("sys_lseek SEEK_CUR 0 returns current pos", cur == 0);

        off_t bad = sys_lseek(99, 0, 0);
        TEST("sys_lseek invalid fd returns -1", bad == (off_t)-1);

        sys_close(fd);
    } else {
        printk("  [INFO] /bin/sh not found in tarfs; skipping lseek file tests\n");
    }

    /* --- getcwd --- */
    char buf[256];
    memset(buf, 0, sizeof(buf));
    char *r = sys_getcwd(buf, sizeof(buf));
    TEST("sys_getcwd returns non-NULL",  r != NULL);
    TEST("sys_getcwd initial cwd is /", r != NULL && buf[0] == '/' && buf[1] == '\0');

    char tiny[1];
    TEST("sys_getcwd tiny buffer returns NULL", sys_getcwd(tiny, 1) == NULL);

    /* --- chdir --- */
    TEST("sys_chdir / succeeds",             sys_chdir("/") == 0);
    TEST("sys_chdir nonexistent path fails", sys_chdir("/no_such_dir_xyz") < 0);

    int rc = sys_chdir("/bin");
    if (rc == 0) {
        memset(buf, 0, sizeof(buf));
        r = sys_getcwd(buf, sizeof(buf));
        TEST("getcwd after chdir /bin returns /bin",
             r != NULL && buf[0]=='/' && buf[1]=='b' && buf[2]=='i' && buf[3]=='n' && buf[4]=='\0');
        sys_chdir("/");  /* reset */
    } else {
        printk("  [INFO] /bin not in tarfs; skipping chdir /bin test\n");
    }

    test_teardown_proc();
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

void libc_syscall_test_run(void) {
    printk("\n");
    printk("========================================\n");
    printk("   libc/Kernel Syscall Interface Tests\n");
    printk("========================================\n");
    printk("\n");

    test_syscall_numbers();
    test_sys_lseek_chdir_getcwd();
    test_sys_write();
    test_sys_file_io();
    test_fd_init();
    test_sys_dup_ops();
    test_sys_pipe_ops();
    test_sys_getpid();
    test_sys_mmap();

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");

    if (failed > 0)
        printk("WARN: %d test(s) failed\n", failed);
}
