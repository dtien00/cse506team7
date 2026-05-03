#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

static int write_file(const char *path, const char *data) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t n = write(fd, data, strlen(data));
    close(fd);
    return (n == (ssize_t)strlen(data)) ? 0 : -1;
}

static int read_file(const char *path, char *buf, int bufsz) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, bufsz - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    return (int)n;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* spawn a binary and return its exit status, or -1 on fork failure */
static int run(char *argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char path[64];
        /* build /bin/<argv[0]> */
        strcpy(path, "/bin/");
        strcat(path, argv[0]);
        execve(path, argv, NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ── rename: file → file ────────────────────────────────────────────────── */

static void test_rename_file(us_test_group_t *group) {
    const char *src = "/mnt/rn_src.txt";
    const char *dst = "/mnt/rn_dst.txt";

    unlink(src); unlink(dst);

    TEST(group, "rename-file: setup src", write_file(src, "hello rename\n") == 0);

    int r = rename(src, dst);
    TEST(group, "rename-file: returns 0", r == 0);
    TEST(group, "rename-file: dst exists", file_exists(dst));
    TEST(group, "rename-file: src gone",   !file_exists(src));

    char buf[64];
    TEST(group, "rename-file: dst content correct",
         read_file(dst, buf, sizeof(buf)) > 0 && strcmp(buf, "hello rename\n") == 0);

    unlink(dst);
}

/* ── rename: file overwrite ─────────────────────────────────────────────── */

static void test_rename_overwrite(us_test_group_t *group) {
    const char *src = "/mnt/rn_ov_src.txt";
    const char *dst = "/mnt/rn_ov_dst.txt";

    unlink(src); unlink(dst);

    TEST(group, "rename-overwrite: setup",
         write_file(src, "new\n") == 0 && write_file(dst, "old\n") == 0);

    int r = rename(src, dst);
    TEST(group, "rename-overwrite: returns 0", r == 0);
    TEST(group, "rename-overwrite: src gone", !file_exists(src));

    char buf[64];
    TEST(group, "rename-overwrite: dst has new content",
         read_file(dst, buf, sizeof(buf)) > 0 && strcmp(buf, "new\n") == 0);

    unlink(dst);
}

/* ── rename: same path no-op ────────────────────────────────────────────── */

static void test_rename_same(us_test_group_t *group) {
    const char *path = "/mnt/rn_same.txt";

    unlink(path);
    TEST(group, "rename-same: setup", write_file(path, "same\n") == 0);

    int r = rename(path, path);
    TEST(group, "rename-same: returns 0", r == 0);
    TEST(group, "rename-same: file still exists", file_exists(path));

    unlink(path);
}

/* ── rename: directory ──────────────────────────────────────────────────── */

static void test_rename_dir(us_test_group_t *group) {
    const char *src = "/mnt/rn_dir_src";
    const char *dst = "/mnt/rn_dir_dst";
    const char *file_in_src = "/mnt/rn_dir_src/inner.txt";
    const char *file_in_dst = "/mnt/rn_dir_dst/inner.txt";

    rmdir(dst);
    unlink(file_in_src);
    rmdir(src);

    TEST(group, "rename-dir: mkdir src", mkdir(src, 0755) == 0);
    TEST(group, "rename-dir: create file inside", write_file(file_in_src, "inside\n") == 0);

    int r = rename(src, dst);
    TEST(group, "rename-dir: returns 0", r == 0);
    TEST(group, "rename-dir: dst is dir",  dir_exists(dst));
    TEST(group, "rename-dir: src gone",    !file_exists(src));
    TEST(group, "rename-dir: file accessible via new path", file_exists(file_in_dst));

    char buf[64];
    TEST(group, "rename-dir: file content preserved",
         read_file(file_in_dst, buf, sizeof(buf)) > 0 && strcmp(buf, "inside\n") == 0);

    unlink(file_in_dst);
    rmdir(dst);
}

/* ── rename: nonexistent src fails ─────────────────────────────────────── */

static void test_rename_noent(us_test_group_t *group) {
    int r = rename("/mnt/rn_no_such_file_xyz", "/mnt/rn_no_dst");
    TEST(group, "rename-noent: returns error", r < 0);
}

/* ── rm: plain file ─────────────────────────────────────────────────────── */

static void test_rm_file(us_test_group_t *group) {
    const char *path = "/mnt/rm_plain.txt";
    unlink(path);

    TEST(group, "rm-file: setup", write_file(path, "x\n") == 0);

    char *argv[] = { "rm", (char *)path, NULL };
    int ret = run(argv);
    TEST(group, "rm-file: exits 0", ret == 0);
    TEST(group, "rm-file: file gone", !file_exists(path));
}

/* ── rm: directory without -r fails ────────────────────────────────────── */

static void test_rm_dir_no_flag(us_test_group_t *group) {
    const char *dir = "/mnt/rm_norecurse_dir";
    rmdir(dir);
    TEST(group, "rm-dir-no-flag: setup mkdir", mkdir(dir, 0755) == 0);

    char *argv[] = { "rm", (char *)dir, NULL };
    int ret = run(argv);
    TEST(group, "rm-dir-no-flag: fails without -r", ret != 0);
    TEST(group, "rm-dir-no-flag: dir still exists", dir_exists(dir));

    rmdir(dir);
}

/* ── rm -r: empty directory ─────────────────────────────────────────────── */

static void test_rm_r_empty(us_test_group_t *group) {
    const char *dir = "/mnt/rm_r_empty";
    rmdir(dir);
    TEST(group, "rm-r-empty: setup mkdir", mkdir(dir, 0755) == 0);

    char *argv[] = { "rm", "-r", (char *)dir, NULL };
    int ret = run(argv);
    TEST(group, "rm-r-empty: exits 0", ret == 0);
    TEST(group, "rm-r-empty: dir gone", !file_exists(dir));
}

/* ── rm -r: directory with files ────────────────────────────────────────── */

static void test_rm_r_with_files(us_test_group_t *group) {
    const char *dir  = "/mnt/rm_r_files";
    const char *f1   = "/mnt/rm_r_files/a.txt";
    const char *f2   = "/mnt/rm_r_files/b.txt";

    unlink(f1); unlink(f2); rmdir(dir);

    TEST(group, "rm-r-files: setup", mkdir(dir, 0755) == 0 &&
         write_file(f1, "a\n") == 0 && write_file(f2, "b\n") == 0);

    char *argv[] = { "rm", "-r", (char *)dir, NULL };
    int ret = run(argv);
    TEST(group, "rm-r-files: exits 0", ret == 0);
    TEST(group, "rm-r-files: dir gone", !file_exists(dir));
}

/* ── rm -r: nested directories ──────────────────────────────────────────── */

static void test_rm_r_nested(us_test_group_t *group) {
    const char *base    = "/mnt/rm_r_nest";
    const char *sub     = "/mnt/rm_r_nest/sub";
    const char *subsub  = "/mnt/rm_r_nest/sub/deep";
    const char *deepf   = "/mnt/rm_r_nest/sub/deep/leaf.txt";

    unlink(deepf); rmdir(subsub); rmdir(sub); rmdir(base);

    TEST(group, "rm-r-nested: setup",
         mkdir(base,   0755) == 0 &&
         mkdir(sub,    0755) == 0 &&
         mkdir(subsub, 0755) == 0 &&
         write_file(deepf, "leaf\n") == 0);

    char *argv[] = { "rm", "-r", (char *)base, NULL };
    int ret = run(argv);
    TEST(group, "rm-r-nested: exits 0", ret == 0);
    TEST(group, "rm-r-nested: base gone", !file_exists(base));
}

/* ── mv: directory ──────────────────────────────────────────────────────── */

static void test_mv_dir(us_test_group_t *group) {
    const char *src      = "/mnt/mv_dir_src";
    const char *dst      = "/mnt/mv_dir_dst";
    const char *file_src = "/mnt/mv_dir_src/f.txt";
    const char *file_dst = "/mnt/mv_dir_dst/f.txt";

    unlink(file_dst); rmdir(dst);
    unlink(file_src); rmdir(src);

    TEST(group, "mv-dir: setup",
         mkdir(src, 0755) == 0 && write_file(file_src, "mv-dir\n") == 0);

    char *argv[] = { "mv", (char *)src, (char *)dst, NULL };
    int ret = run(argv);
    TEST(group, "mv-dir: exits 0", ret == 0);
    TEST(group, "mv-dir: dst is dir",  dir_exists(dst));
    TEST(group, "mv-dir: src gone",    !file_exists(src));
    TEST(group, "mv-dir: file in dst", file_exists(file_dst));

    char buf[64];
    TEST(group, "mv-dir: file content preserved",
         read_file(file_dst, buf, sizeof(buf)) > 0 && strcmp(buf, "mv-dir\n") == 0);

    unlink(file_dst);
    rmdir(dst);
}

/* ── cp: directory fails ────────────────────────────────────────────────── */

static void test_cp_dir_fails(us_test_group_t *group) {
    const char *dir = "/mnt/cp_dir_test";
    rmdir(dir);
    TEST(group, "cp-dir: setup mkdir", mkdir(dir, 0755) == 0);

    char *argv[] = { "cp", (char *)dir, "/mnt/cp_dir_dst", NULL };
    int ret = run(argv);
    TEST(group, "cp-dir: fails on directory src", ret != 0);

    rmdir(dir);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_rename_rmdir(us_test_group_t *group) {
    ut_group_begin(group, "rename/rm-r/dir-mv");

    test_rename_file(group);
    test_rename_overwrite(group);
    test_rename_same(group);
    test_rename_dir(group);
    test_rename_noent(group);
    test_rm_file(group);
    test_rm_dir_no_flag(group);
    test_rm_r_empty(group);
    test_rm_r_with_files(group);
    test_rm_r_nested(group);
    test_mv_dir(group);
    test_cp_dir_fails(group);

    ut_group_end(group);
}
