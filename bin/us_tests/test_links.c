#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "us_test_common.h"

/* Inline syscall wrappers — link/symlink/readlink may not be in our libc */
static int do_link(const char *old, const char *newp) {
    return link(old, newp);
}
static int do_symlink(const char *target, const char *lp) {
    return symlink(target, lp);
}
static ssize_t do_readlink(const char *path, char *buf, size_t sz) {
    return readlink(path, buf, sz);
}

/* ── helpers ──────────────────────────────────────────────────────────── */

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

/* ── hard link tests ─────────────────────────────────────────────────── */

static void run_hardlink_tests(us_test_group_t *group) {
    ut_group_begin(group, "links.hardlink");

    const char *orig  = "/tmp/hl_orig.txt";
    const char *link1 = "/tmp/hl_link1.txt";
    const char *link2 = "/tmp/hl_link2.txt";

    /* clean slate */
    unlink(orig);
    unlink(link1);
    unlink(link2);

    /* create source file */
    TEST(group, "hardlink: create source file", write_file(orig, "hardlink data\n") == 0);

    /* basic link() */
    TEST(group, "hardlink: link() returns 0", do_link(orig, link1) == 0);

    /* both paths are readable and have the same content */
    {
        char buf[64];
        int n = read_file(link1, buf, sizeof(buf));
        TEST(group, "hardlink: link is readable", n > 0);
        TEST(group, "hardlink: link content matches original", strcmp(buf, "hardlink data\n") == 0);
    }

    /* stat: both dentries report the same inode number */
    {
        struct stat so, sl;
        int r1 = stat(orig,  &so);
        int r2 = stat(link1, &sl);
        TEST(group, "hardlink: stat on original succeeds", r1 == 0);
        TEST(group, "hardlink: stat on link succeeds",     r2 == 0);
        TEST(group, "hardlink: same inode number",
             r1 == 0 && r2 == 0 && so.st_ino == sl.st_ino);
        TEST(group, "hardlink: nlink is 2",
             r1 == 0 && so.st_nlink == 2);
    }

    /* write through original; read back through link */
    {
        int fd = open(orig, O_WRONLY | O_TRUNC);
        if (fd >= 0) {
            write(fd, "updated\n", 8);
            close(fd);
        }
        char buf[64];
        int n = read_file(link1, buf, sizeof(buf));
        TEST(group, "hardlink: write through original visible via link",
             n == 8 && strcmp(buf, "updated\n") == 0);
    }

    /* write through link; read back through original */
    {
        int fd = open(link1, O_WRONLY | O_TRUNC);
        if (fd >= 0) {
            write(fd, "via link\n", 9);
            close(fd);
        }
        char buf[64];
        int n = read_file(orig, buf, sizeof(buf));
        TEST(group, "hardlink: write through link visible via original",
             n == 9 && strcmp(buf, "via link\n") == 0);
    }

    /* unlink original; link survives */
    TEST(group, "hardlink: unlink original returns 0", unlink(orig) == 0);
    {
        char buf[64];
        int n = read_file(link1, buf, sizeof(buf));
        TEST(group, "hardlink: link survives after original unlinked", n > 0);
    }
    {
        struct stat sl;
        int r = stat(link1, &sl);
        TEST(group, "hardlink: nlink decremented to 1 after unlink",
             r == 0 && sl.st_nlink == 1);
    }

    /* unlink last hard link; inode freed */
    TEST(group, "hardlink: unlink last link returns 0", unlink(link1) == 0);
    {
        struct stat sl;
        TEST(group, "hardlink: stat fails after all links removed",
             stat(link1, &sl) < 0);
    }

    /* link() to nonexistent source must fail */
    TEST(group, "hardlink: link() on nonexistent source fails",
         do_link("/tmp/hl_nosuchfile_xyz.txt", link2) < 0);

    /* link() where newpath already exists must fail */
    write_file(orig,  "orig\n");
    write_file(link2, "existing\n");
    TEST(group, "hardlink: link() when newpath exists fails",
         do_link(orig, link2) < 0);
    unlink(orig);
    unlink(link2);

    /* link() to a directory must fail */
    mkdir("/tmp/hl_dir_test", 0755);
    TEST(group, "hardlink: link() on directory fails",
         do_link("/tmp/hl_dir_test", link2) < 0);
    rmdir("/tmp/hl_dir_test");

    ut_group_end(group);
}

/* ── symlink tests ───────────────────────────────────────────────────── */

static void run_symlink_tests(us_test_group_t *group) {
    ut_group_begin(group, "links.symlink");

    const char *target  = "/tmp/sl_target.txt";
    const char *lnk     = "/tmp/sl_link.txt";
    const char *dangling = "/tmp/sl_dangling.txt";
    const char *noexist  = "/tmp/sl_no_target_xyz.txt";

    unlink(target);
    unlink(lnk);
    unlink(dangling);

    /* create target */
    TEST(group, "symlink: create target file", write_file(target, "symlink data\n") == 0);

    /* symlink() returns 0 */
    TEST(group, "symlink: symlink() returns 0", do_symlink(target, lnk) == 0);

    /* readlink returns the target path */
    {
        char buf[256];
        ssize_t n = do_readlink(lnk, buf, sizeof(buf) - 1);
        TEST(group, "symlink: readlink returns positive length", n > 0);
        if (n > 0) {
            buf[n] = '\0';
            TEST(group, "symlink: readlink returns correct target",
                 strcmp(buf, target) == 0);
        }
    }

    /* readlink does NOT NUL-terminate — verify buffer unchanged past n */
    {
        char buf[256];
        memset(buf, 0xAB, sizeof(buf));
        ssize_t n = do_readlink(lnk, buf, sizeof(buf) - 1);
        TEST(group, "symlink: readlink returns target length",
             n == (ssize_t)strlen(target));
    }

    /* open through symlink reads target content */
    {
        char buf[64];
        int n = read_file(lnk, buf, sizeof(buf));
        TEST(group, "symlink: open through symlink readable", n > 0);
        TEST(group, "symlink: open through symlink reads target content",
             strcmp(buf, "symlink data\n") == 0);
    }

    /* stat follows the symlink */
    {
        struct stat st;
        int r = stat(lnk, &st);
        TEST(group, "symlink: stat() follows symlink", r == 0);
        TEST(group, "symlink: stat() reports regular file type",
             r == 0 && S_ISREG(st.st_mode));
    }

    /* lstat does NOT follow the symlink */
    {
        struct stat lst;
        int r = lstat(lnk, &lst);
        TEST(group, "symlink: lstat() returns 0", r == 0);
        TEST(group, "symlink: lstat() reports symlink type",
             r == 0 && S_ISLNK(lst.st_mode));
    }

    /* write through symlink; read back through target path */
    {
        int fd = open(lnk, O_WRONLY | O_TRUNC);
        if (fd >= 0) {
            write(fd, "through symlink\n", 16);
            close(fd);
        }
        char buf[64];
        int n = read_file(target, buf, sizeof(buf));
        TEST(group, "symlink: write through symlink visible via target",
             n == 16 && strcmp(buf, "through symlink\n") == 0);
    }

    /* unlink(symlink) removes symlink, not target */
    TEST(group, "symlink: unlink(symlink) returns 0", unlink(lnk) == 0);
    {
        struct stat st;
        TEST(group, "symlink: target still exists after symlink unlinked",
             stat(target, &st) == 0);
        TEST(group, "symlink: symlink itself is gone",
             lstat(lnk, &st) < 0);
    }
    unlink(target);

    /* dangling symlink — target does not exist */
    TEST(group, "symlink: create dangling symlink returns 0",
         do_symlink(noexist, dangling) == 0);
    {
        /* readlink on dangling symlink still works */
        char buf[256];
        ssize_t n = do_readlink(dangling, buf, sizeof(buf) - 1);
        TEST(group, "symlink: readlink on dangling link returns target", n > 0);
        if (n > 0) {
            buf[n] = '\0';
            TEST(group, "symlink: dangling readlink target correct",
                 strcmp(buf, noexist) == 0);
        }
        /* open on dangling symlink must fail */
        int fd = open(dangling, O_RDONLY);
        TEST(group, "symlink: open on dangling symlink fails", fd < 0);
        if (fd >= 0) close(fd);
    }
    unlink(dangling);

    /* symlink() where linkpath already exists must fail */
    write_file(target, "orig\n");
    write_file(lnk, "existing\n");
    TEST(group, "symlink: symlink() when linkpath exists fails",
         do_symlink(target, lnk) < 0);
    unlink(target);
    unlink(lnk);

    /* readlink on a regular file must fail */
    write_file(target, "plain\n");
    {
        char buf[64];
        TEST(group, "symlink: readlink on regular file fails",
             do_readlink(target, buf, sizeof(buf)) < 0);
    }
    unlink(target);

    /* readlink on nonexistent path must fail */
    {
        char buf[64];
        TEST(group, "symlink: readlink on nonexistent path fails",
             do_readlink("/tmp/sl_no_such_xyz.txt", buf, sizeof(buf)) < 0);
    }

    ut_group_end(group);
}

/* ── ext2 hard link tests ────────────────────────────────────────────── */

static void run_ext2_hardlink_tests(us_test_group_t *group) {
    ut_group_begin(group, "links.ext2-hardlink");

    const char *orig  = "/mnt/hl_orig.txt";
    const char *link1 = "/mnt/hl_link1.txt";
    const char *link2 = "/mnt/hl_link2.txt";

    unlink(orig);
    unlink(link1);
    unlink(link2);

    TEST(group, "ext2 hardlink: create source file", write_file(orig, "hardlink data\n") == 0);
    TEST(group, "ext2 hardlink: link() returns 0", do_link(orig, link1) == 0);

    {
        char buf[64];
        int n = read_file(link1, buf, sizeof(buf));
        TEST(group, "ext2 hardlink: link is readable", n > 0);
        TEST(group, "ext2 hardlink: link content matches original",
             strcmp(buf, "hardlink data\n") == 0);
    }

    {
        struct stat so, sl;
        int r1 = stat(orig,  &so);
        int r2 = stat(link1, &sl);
        TEST(group, "ext2 hardlink: stat on original succeeds", r1 == 0);
        TEST(group, "ext2 hardlink: stat on link succeeds",     r2 == 0);
        TEST(group, "ext2 hardlink: same inode number",
             r1 == 0 && r2 == 0 && so.st_ino == sl.st_ino);
        TEST(group, "ext2 hardlink: nlink is 2",
             r1 == 0 && so.st_nlink == 2);
    }

    /* write through original; read back through link */
    {
        int fd = open(orig, O_WRONLY | O_TRUNC);
        if (fd >= 0) { write(fd, "updated\n", 8); close(fd); }
        char buf[64];
        int n = read_file(link1, buf, sizeof(buf));
        TEST(group, "ext2 hardlink: write through original visible via link",
             n == 8 && strcmp(buf, "updated\n") == 0);
    }

    /* write through link; read back through original */
    {
        int fd = open(link1, O_WRONLY | O_TRUNC);
        if (fd >= 0) { write(fd, "via link\n", 9); close(fd); }
        char buf[64];
        int n = read_file(orig, buf, sizeof(buf));
        TEST(group, "ext2 hardlink: write through link visible via original",
             n == 9 && strcmp(buf, "via link\n") == 0);
    }

    /* unlink original; link survives */
    TEST(group, "ext2 hardlink: unlink original returns 0", unlink(orig) == 0);
    {
        char buf[64];
        int n = read_file(link1, buf, sizeof(buf));
        TEST(group, "ext2 hardlink: link survives after original unlinked", n > 0);
    }
    {
        struct stat sl;
        int r = stat(link1, &sl);
        TEST(group, "ext2 hardlink: nlink decremented to 1 after unlink",
             r == 0 && sl.st_nlink == 1);
    }

    TEST(group, "ext2 hardlink: unlink last link returns 0", unlink(link1) == 0);
    {
        struct stat sl;
        TEST(group, "ext2 hardlink: stat fails after all links removed",
             stat(link1, &sl) < 0);
    }

    /* link() to nonexistent source must fail */
    TEST(group, "ext2 hardlink: link() on nonexistent source fails",
         do_link("/mnt/hl_nosuchfile_xyz.txt", link2) < 0);

    /* link() where newpath already exists must fail */
    write_file(orig,  "orig\n");
    write_file(link2, "existing\n");
    TEST(group, "ext2 hardlink: link() when newpath exists fails",
         do_link(orig, link2) < 0);
    unlink(orig);
    unlink(link2);

    /* link() to a directory must fail */
    mkdir("/mnt/hl_dir_test", 0755);
    TEST(group, "ext2 hardlink: link() on directory fails",
         do_link("/mnt/hl_dir_test", link2) < 0);
    rmdir("/mnt/hl_dir_test");

    ut_group_end(group);
}

/* ── ext2 symlink tests ──────────────────────────────────────────────── */

static void run_ext2_symlink_tests(us_test_group_t *group) {
    ut_group_begin(group, "links.ext2-symlink");

    const char *target   = "/mnt/sl_target.txt";
    const char *lnk      = "/mnt/sl_link.txt";
    const char *dangling = "/mnt/sl_dangling.txt";
    const char *noexist  = "/mnt/sl_no_target_xyz.txt";

    unlink(target);
    unlink(lnk);
    unlink(dangling);

    TEST(group, "ext2 symlink: create target file", write_file(target, "symlink data\n") == 0);
    TEST(group, "ext2 symlink: symlink() returns 0", do_symlink(target, lnk) == 0);

    {
        char buf[256];
        ssize_t n = do_readlink(lnk, buf, sizeof(buf) - 1);
        TEST(group, "ext2 symlink: readlink returns positive length", n > 0);
        if (n > 0) {
            buf[n] = '\0';
            TEST(group, "ext2 symlink: readlink returns correct target",
                 strcmp(buf, target) == 0);
        }
    }

    {
        char buf[256];
        memset(buf, 0xAB, sizeof(buf));
        ssize_t n = do_readlink(lnk, buf, sizeof(buf) - 1);
        TEST(group, "ext2 symlink: readlink returns target length",
             n == (ssize_t)strlen(target));
    }

    {
        char buf[64];
        int n = read_file(lnk, buf, sizeof(buf));
        TEST(group, "ext2 symlink: open through symlink readable", n > 0);
        TEST(group, "ext2 symlink: open through symlink reads target content",
             strcmp(buf, "symlink data\n") == 0);
    }

    {
        struct stat st;
        int r = stat(lnk, &st);
        TEST(group, "ext2 symlink: stat() follows symlink", r == 0);
        TEST(group, "ext2 symlink: stat() reports regular file type",
             r == 0 && S_ISREG(st.st_mode));
    }

    {
        struct stat lst;
        int r = lstat(lnk, &lst);
        TEST(group, "ext2 symlink: lstat() returns 0", r == 0);
        TEST(group, "ext2 symlink: lstat() reports symlink type",
             r == 0 && S_ISLNK(lst.st_mode));
    }

    /* write through symlink; read back via target */
    {
        int fd = open(lnk, O_WRONLY | O_TRUNC);
        if (fd >= 0) { write(fd, "through symlink\n", 16); close(fd); }
        char buf[64];
        int n = read_file(target, buf, sizeof(buf));
        TEST(group, "ext2 symlink: write through symlink visible via target",
             n == 16 && strcmp(buf, "through symlink\n") == 0);
    }

    /* unlink(symlink) removes symlink, not target */
    TEST(group, "ext2 symlink: unlink(symlink) returns 0", unlink(lnk) == 0);
    {
        struct stat st;
        TEST(group, "ext2 symlink: target still exists after symlink unlinked",
             stat(target, &st) == 0);
        TEST(group, "ext2 symlink: symlink itself is gone",
             lstat(lnk, &st) < 0);
    }
    unlink(target);

    /* dangling symlink */
    TEST(group, "ext2 symlink: create dangling symlink returns 0",
         do_symlink(noexist, dangling) == 0);
    {
        char buf[256];
        ssize_t n = do_readlink(dangling, buf, sizeof(buf) - 1);
        TEST(group, "ext2 symlink: readlink on dangling link returns target", n > 0);
        if (n > 0) {
            buf[n] = '\0';
            TEST(group, "ext2 symlink: dangling readlink target correct",
                 strcmp(buf, noexist) == 0);
        }
        int fd = open(dangling, O_RDONLY);
        TEST(group, "ext2 symlink: open on dangling symlink fails", fd < 0);
        if (fd >= 0) close(fd);
    }
    unlink(dangling);

    /* symlink() where linkpath already exists must fail */
    write_file(target, "orig\n");
    write_file(lnk, "existing\n");
    TEST(group, "ext2 symlink: symlink() when linkpath exists fails",
         do_symlink(target, lnk) < 0);
    unlink(target);
    unlink(lnk);

    /* readlink on a regular file must fail */
    write_file(target, "plain\n");
    {
        char buf[64];
        TEST(group, "ext2 symlink: readlink on regular file fails",
             do_readlink(target, buf, sizeof(buf)) < 0);
    }
    unlink(target);

    /* readlink on nonexistent path must fail */
    {
        char buf[64];
        TEST(group, "ext2 symlink: readlink on nonexistent path fails",
             do_readlink("/mnt/sl_no_such_xyz.txt", buf, sizeof(buf)) < 0);
    }

    ut_group_end(group);
}

/* ── ln binary tests ─────────────────────────────────────────────────── */

static void run_ln_bin_tests(us_test_group_t *group) {
    ut_group_begin(group, "links.ln-bin");

    /* verify the ln binary is present */
    {
        struct stat st;
        int r = stat("/bin/ln", &st);
        TEST(group, "ln-bin: /bin/ln exists", r == 0);
        TEST(group, "ln-bin: /bin/ln is regular file", r == 0 && S_ISREG(st.st_mode));
    }

    ut_group_end(group);
}

/* ── entry point ─────────────────────────────────────────────────────── */

void run_test_links(us_test_group_t groups[], int *idx) {
    printf("\n=== links tests ===\n");
    run_hardlink_tests(&groups[(*idx)++]);
    run_symlink_tests(&groups[(*idx)++]);
    run_ln_bin_tests(&groups[(*idx)++]);
    run_ext2_hardlink_tests(&groups[(*idx)++]);
    run_ext2_symlink_tests(&groups[(*idx)++]);
}
