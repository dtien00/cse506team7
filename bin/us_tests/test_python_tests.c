/*
 * test_python_tests.c — Run MicroPython .exp-backed test suite inside sbunix.
 *
 * For each *.py file in PYTEST_DIR that has a matching *.py.exp file:
 *   1. fork() a child that execve's /bin/python with the .py file
 *   2. capture child stdout via a pipe
 *   3. compare captured output byte-for-byte against the .exp file
 *   4. PASS / FAIL accordingly — bail on the FIRST failure
 *
 * Tests without a .exp file are skipped (we have no CPython reference inside
 * the kernel).
 *
 * The python test files and their .exp files must be present in PYTEST_DIR
 * at runtime (copy them into rootfs before booting QEMU).
 */

#include "us_test_common.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>

#define PYTEST_DIR   "/pytests/basics"
#define PYTHON_BIN   "/bin/python"
#define BUF_SIZE     65536   /* max output we'll capture per test */
#define MAX_TESTS    1024

/* Run one .py file, capture its stdout, return bytes written to out_buf.
 * Returns -1 on fork/exec failure. */
static ssize_t run_py(const char *py_path, char *out_buf, size_t buf_sz)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    if (pid == 0) {
        /* child: wire stdout→pipe write-end, close read-end */
        close(pipefd[0]);
        if (pipefd[1] != STDOUT_FILENO) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
        }
        /* redirect stderr to same pipe so tracebacks appear in captured output */
        dup2(pipefd[1], STDERR_FILENO);
        char *av[] = { (char *)PYTHON_BIN, (char *)py_path, NULL };
        execve(PYTHON_BIN, av, NULL);
        _exit(127);
    }

    /* parent: read from pipe */
    close(pipefd[1]);
    ssize_t total = 0;
    ssize_t n;
    while (total < (ssize_t)buf_sz &&
           (n = read(pipefd[0], out_buf + total, buf_sz - (size_t)total)) > 0)
        total += n;
    close(pipefd[0]);

    int st = 0;
    waitpid(pid, &st, 0);
    /* If python exited non-zero, the output still goes through comparison;
     * a mismatch will catch it.  If it exited 127 (exec failed) the output
     * will be empty and the test will fail clearly. */
    (void)st;
    return total;
}

/* Read entire file into buf.  Returns bytes read, -1 on error. */
static ssize_t read_file(const char *path, char *buf, size_t sz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t total = 0, n;
    while (total < (ssize_t)sz &&
           (n = read(fd, buf + total, sz - (size_t)total)) > 0)
        total += n;
    close(fd);
    return total;
}

/* Collect all .py filenames in dir that have a matching .py.exp, sorted. */
static int collect_tests(const char *dir, char names[][256], int max)
{
    DIR *dp = opendir(dir);
    if (!dp) {
        // printf("  [DBG] opendir(%s) failed errno=%d\n", dir, errno);
        return 0;
    }
    // printf("  [DBG] opendir(%s) ok\n", dir);

    int count = 0;
    struct dirent *de;
    int entry_num = 0;
    while ((de = readdir(dp)) != NULL && count < max) {
        const char *nm = de->d_name;
        size_t len = strlen(nm);
        if (entry_num < 5)
            // printf("  [DBG] readdir entry[%d]: d_name='%s' len=%zu\n", entry_num, nm, len);
        entry_num++;
        /* must end with ".py" and NOT already be ".py.exp" */
        if (len < 4) continue;
        if (strcmp(nm + len - 3, ".py") != 0) continue;

        /* build .exp path and check it exists */
        char exp_path[512];
        snprintf(exp_path, sizeof(exp_path), "%s/%s.exp", dir, nm);
        int fd = open(exp_path, O_RDONLY);
        if (count < 3)
            // printf("  [DBG] check exp=%s fd=%d errno=%d\n", exp_path, fd, errno);
        if (fd < 0) continue;   /* no .exp → skip */
        close(fd);

        strncpy(names[count], nm, 255);
        names[count][255] = '\0';
        count++;
    }
    closedir(dp);

    // printf("  [DBG] before sort: names[0]='%s' names[1]='%s'\n",
        //    count > 0 ? names[0] : "(none)",
        //    count > 1 ? names[1] : "(none)");

    /* simple insertion sort by name for deterministic order */
    for (int i = 1; i < count; i++) {
        char tmp[256];
        strncpy(tmp, names[i], 255);
        tmp[255] = '\0';
        int j = i - 1;
        while (j >= 0 && strcmp(names[j], tmp) > 0) {
            strncpy(names[j+1], names[j], 255);
            names[j+1][255] = '\0';
            j--;
        }
        strncpy(names[j+1], tmp, 255);
        names[j+1][255] = '\0';
    }

    // printf("  [DBG] after sort: names[0]='%s' names[1]='%s'\n",
    //        count > 0 ? names[0] : "(none)",
    //        count > 1 ? names[1] : "(none)");
    return count;
}

void run_test_python_tests(us_test_group_t *group)
{
    ut_group_begin(group, "python_tests (basics/*.py.exp)");

    static char names[MAX_TESTS][256];
    int count = collect_tests(PYTEST_DIR, names, MAX_TESTS);

    // printf("  [DBG] collect_tests found %d .exp-backed tests in " PYTEST_DIR "\n", count);
    // for (int j = 0; j < count && j < 3; j++)
    //     printf("  [DBG]   [%d] %s\n", j, names[j]);

    if (count == 0) {
        SKIP(group, "python_tests", "no *.py.exp files found in " PYTEST_DIR);
        ut_group_end(group);
        return;
    }

    static char got[BUF_SIZE];
    static char exp[BUF_SIZE];

    for (int i = 0; i < count; i++) {
        char py_path[512], exp_path[512];
        snprintf(py_path,  sizeof(py_path),  "%s/%s",     PYTEST_DIR, names[i]);
        snprintf(exp_path, sizeof(exp_path), "%s/%s.exp", PYTEST_DIR, names[i]);

        // printf("  [DBG] py=%s\n", py_path);
        // printf("  [DBG] exp=%s\n", exp_path);

        ssize_t exp_len = read_file(exp_path, exp, sizeof(exp) - 1);
        // printf("  [DBG] exp_len=%zd errno=%d\n", exp_len, errno);

        ssize_t got_len = run_py(py_path, got, sizeof(got) - 1);
        // printf("  [DBG] got_len=%zd\n", got_len);

        if (got_len < 0) {
            printf("  [FAIL] %s: could not execute /bin/python\n", names[i]);
            group->failed++;
            ut_group_end(group);
            exit(1);
        }
        if (exp_len < 0) {
            SKIP(group, names[i], ".exp file vanished");
            continue;
        }

        got[got_len] = '\0';
        exp[exp_len] = '\0';

        /* Check if test output is "SKIP" — micropython signals unsupported feature */
        if (got_len >= 4 && strncmp(got, "SKIP", 4) == 0) {
            SKIP(group, names[i], "micropython printed SKIP");
            continue;
        }

        int pass = (got_len == exp_len && memcmp(got, exp, (size_t)got_len) == 0);
        TEST(group, names[i], pass);

        if (!pass) {
            /* print a short diff hint before bailing */
            printf("  expected (%zd bytes): ", exp_len);
            fwrite(exp, 1, exp_len < 360 ? (size_t)exp_len : 360, stdout);
            printf("%s\n", exp_len > 360 ? "..." : "");
            printf("  got      (%zd bytes): ", got_len);
            fwrite(got, 1, got_len < 360 ? (size_t)got_len : 360, stdout);
            printf("%s\n", got_len > 360 ? "..." : "");
            ut_group_end(group);
            exit(1);   /* fail fast */
        }
    }

    ut_group_end(group);
}
