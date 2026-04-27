#include <fcntl.h>
#include <unistd.h>
#include "sys/stat.h"
#include "us_test_common.h"

void run_test_file_io(us_test_group_t *group) {
    int fd;
    char buf[32];
    int n;

    ut_group_begin(group, "fcntl+unistd");

    fd = open("/hello.txt", O_RDONLY);
    TEST(group, "open existing tarfs file", fd >= 0);

    if (fd >= 0) {
        n = (int)read(fd, buf, sizeof(buf) - 1);
        TEST(group, "read returns positive bytes", n > 0);
        if (n > 0) {
            buf[n] = '\0';
            TEST(group, "read buffer NUL-terminated locally", buf[n] == '\0');
        }
        TEST(group, "close succeeds", close(fd) == 0);
    }

    fd = open("/definitely_missing_file", O_RDONLY);
    TEST(group, "open missing file fails", fd < 0);

    /* cwd-relative path: ls "." after chdir should see the new directory */
    {
        mkdir("/tmp/cwdtest", 0755);
        int cdf = open("/tmp/cwdtest/rel.txt", O_CREAT | O_WRONLY, 0644);
        TEST(group, "cwd: create file in subdir", cdf >= 0);
        if (cdf >= 0) close(cdf);

        chdir("/tmp/cwdtest");

        /* stat on a bare filename should resolve relative to cwd */
        struct stat cst;
        int sr = stat("rel.txt", &cst);
        TEST(group, "cwd: stat relative path after chdir", sr == 0);
        TEST(group, "cwd: relative stat is regular file", sr == 0 && S_ISREG(cst.st_mode));

        /* open relative path */
        int rfd = open("rel.txt", O_RDONLY);
        TEST(group, "cwd: open relative path after chdir", rfd >= 0);
        if (rfd >= 0) close(rfd);

        chdir("/");
    }

    /* open with explicit mode — stat should see the right permission bits */
    {
        int mfd = open("/tmp/mode_test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0600);
        TEST(group, "open-mode: O_CREAT with mode 0600 succeeds", mfd >= 0);
        if (mfd >= 0) close(mfd);

        struct stat mst;
        int sr = stat("/tmp/mode_test.txt", &mst);
        TEST(group, "open-mode: stat after creat succeeds", sr == 0);
        TEST(group, "open-mode: mode bits are 0600", sr == 0 && (mst.st_mode & 0777) == 0600);
    }

    /* chmod */
    {
        int cfd = open("/tmp/perm_test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        TEST(group, "chmod: create test file", cfd >= 0);
        if (cfd >= 0) close(cfd);

        int cr = chmod("/tmp/perm_test.txt", 0600);
        TEST(group, "chmod: returns 0", cr == 0);

        struct stat cst;
        int sr = stat("/tmp/perm_test.txt", &cst);
        TEST(group, "chmod: stat after chmod succeeds", sr == 0);
        TEST(group, "chmod: mode bits are 0600", sr == 0 && (cst.st_mode & 0777) == 0600);

        int rd = open("/tmp/perm_test.txt", O_RDONLY);
        TEST(group, "chmod: read still allowed with 0600", rd >= 0);
        if (rd >= 0) close(rd);

        cr = chmod("/tmp/perm_test.txt", 0000);
        TEST(group, "chmod: can drop permissions to 0000", cr == 0);
        TEST(group, "chmod: open denied after 0000",
             open("/tmp/perm_test.txt", O_RDONLY) < 0);

        cr = chmod("/tmp/perm_test.txt", 0200);
        TEST(group, "chmod: can set write-only mode", cr == 0);
        TEST(group, "chmod: read open denied after 0200",
             open("/tmp/perm_test.txt", O_RDONLY) < 0);
        int wfd = open("/tmp/perm_test.txt", O_WRONLY);
        TEST(group, "chmod: write open allowed after 0200", wfd >= 0);
        if (wfd >= 0) close(wfd);

        /* chmod nonexistent path returns error */
        TEST(group, "chmod: nonexistent path returns error",
             chmod("/tmp/no_such_file_xyz.txt", 0600) < 0);
    }

    /* fchmod */
    {
        int ffd = open("/tmp/fchmod_test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        TEST(group, "fchmod: create test file", ffd >= 0);
        if (ffd >= 0) {
            int fr = fchmod(ffd, 0755);
            TEST(group, "fchmod: returns 0", fr == 0);
            close(ffd);

            struct stat fst;
            int sr2 = stat("/tmp/fchmod_test.txt", &fst);
            TEST(group, "fchmod: mode bits are 0755", sr2 == 0 && (fst.st_mode & 0777) == 0755);
        }

        /* fchmod bad fd returns error */
        TEST(group, "fchmod: bad fd returns error", fchmod(-1, 0644) < 0);
    }

    /* directory search/write permissions */
    {
        mkdir("/tmp/perm_dir", 0755);
        int cr = chmod("/tmp/perm_dir", 0666);
        TEST(group, "dir-perm: chmod remove execute bit succeeds", cr == 0);
        TEST(group, "dir-perm: create in non-searchable dir fails",
             open("/tmp/perm_dir/file.txt", O_CREAT | O_WRONLY, 0644) < 0);

        cr = chmod("/tmp/perm_dir", 0777);
        TEST(group, "dir-perm: restore execute bit succeeds", cr == 0);
        int pfd = open("/tmp/perm_dir/file.txt", O_CREAT | O_WRONLY, 0644);
        TEST(group, "dir-perm: create succeeds after restoring execute", pfd >= 0);
        if (pfd >= 0) close(pfd);

        cr = chmod("/tmp/perm_dir", 0555);
        TEST(group, "dir-perm: remove write bit succeeds", cr == 0);
        TEST(group, "dir-perm: create denied without directory write",
             open("/tmp/perm_dir/second.txt", O_CREAT | O_WRONLY, 0644) < 0);
    }

    /* rmdir */
    {
        /* rmdir an empty directory */
        mkdir("/tmp/rmdir_empty", 0755);
        int rr = rmdir("/tmp/rmdir_empty");
        TEST(group, "rmdir: empty dir returns 0", rr == 0);

        struct stat rst;
        TEST(group, "rmdir: stat after rmdir fails", stat("/tmp/rmdir_empty", &rst) < 0);

        /* rmdir a non-empty directory must fail */
        mkdir("/tmp/rmdir_nonempty", 0755);
        int nfd = open("/tmp/rmdir_nonempty/f.txt", O_CREAT | O_WRONLY, 0644);
        if (nfd >= 0) close(nfd);
        TEST(group, "rmdir: non-empty dir returns error", rmdir("/tmp/rmdir_nonempty") < 0);

        /* rmdir a regular file must fail */
        int rff = open("/tmp/rmdir_file.txt", O_CREAT | O_WRONLY, 0644);
        if (rff >= 0) close(rff);
        TEST(group, "rmdir: regular file returns error", rmdir("/tmp/rmdir_file.txt") < 0);

        /* rmdir nonexistent path returns error */
        TEST(group, "rmdir: nonexistent path returns error",
             rmdir("/tmp/no_such_dir_xyz") < 0);
    }

    /* stat binary exists */
    {
        struct stat sst;
        int ssr = stat("/bin/stat", &sst);
        TEST(group, "stat-bin: binary exists in /bin", ssr == 0);
        TEST(group, "stat-bin: is regular file", ssr == 0 && S_ISREG(sst.st_mode));
    }

    /* chmod binary exists */
    {
        struct stat cst3;
        int csr = stat("/bin/chmod", &cst3);
        TEST(group, "chmod-bin: binary exists in /bin", csr == 0);
        TEST(group, "chmod-bin: is regular file", csr == 0 && S_ISREG(cst3.st_mode));
    }

    ut_group_end(group);
}
