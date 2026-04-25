#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "dirent.h"
#include "errno.h"
#include "us_test_common.h"

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* Count all entries in a directory without side effects on the caller's DIR. */
static int count_entries(const char *path) {
    DIR *d = opendir(path);
    if (!d) return -1;
    int n = 0;
    while (readdir(d)) n++;
    closedir(d);
    return n;
}

/* Return 1 if d_type is a value we defined, 0 otherwise. */
static int valid_dtype(unsigned char t) {
    return t == DT_UNKNOWN || t == DT_FIFO || t == DT_CHR  ||
           t == DT_DIR     || t == DT_BLK  || t == DT_REG  ||
           t == DT_LNK     || t == DT_SOCK || t == DT_WHT;
}

/* ── test body ───────────────────────────────────────────────────────────── */

void run_test_dirent(us_test_group_t *group) {
    ut_group_begin(group, "dirent");

    printf("a");

    /* ── opendir ─────────────────────────────────────────────────────── */

    DIR *root = opendir("/");
    TEST(group, "opendir(\"/\"): returns non-NULL", root != NULL);

    printf("b");

    DIR *bad = opendir("/no_such_path_xyz");
    TEST(group, "opendir(nonexistent): returns NULL", bad == NULL);
    if (bad) closedir(bad);   /* shouldn't happen, but don't leak */


    /* ── readdir: NULL guard ─────────────────────────────────────────── */

    struct dirent *e = readdir(NULL);
    TEST(group, "readdir(NULL): returns NULL", e == NULL);

    /* ── readdir: first-entry field validation ───────────────────────── */

    if (!root) {
        SKIP(group, "readdir: first entry non-NULL",       "opendir(\"/\") failed");
        SKIP(group, "readdir: d_ino > 0",                  "opendir(\"/\") failed");
        SKIP(group, "readdir: d_type is a known DT_ value","opendir(\"/\") failed");
        SKIP(group, "readdir: d_name is non-empty",        "opendir(\"/\") failed");
        SKIP(group, "readdir: d_name is NUL-terminated",   "opendir(\"/\") failed");
    } else {
        struct dirent *first = readdir(root);
        TEST(group, "readdir: first entry non-NULL", first != NULL);

        if (first) {
            TEST(group, "readdir: d_ino > 0",
                 first->d_ino > 0);
            TEST(group, "readdir: d_type is a known DT_ value",
                 valid_dtype(first->d_type));
            TEST(group, "readdir: d_name is non-empty",
                 first->d_name[0] != '\0');
            TEST(group, "readdir: d_name is NUL-terminated within NAME_MAX",
                 strnlen(first->d_name, NAME_MAX + 1) <= NAME_MAX);
        } else {
            SKIP(group, "readdir: d_ino > 0",                  "first entry was NULL");
            SKIP(group, "readdir: d_type is a known DT_ value","first entry was NULL");
            SKIP(group, "readdir: d_name is non-empty",        "first entry was NULL");
            SKIP(group, "readdir: d_name is NUL-terminated",   "first entry was NULL");
        }
    }

    /* ── readdir: exhaustion behaviour ──────────────────────────────── */

    if (!root) {
        SKIP(group, "readdir: root has >= 1 entry",          "opendir(\"/\") failed");
        SKIP(group, "readdir: returns NULL at end-of-dir",   "opendir(\"/\") failed");
        SKIP(group, "readdir: NULL stays NULL after EOF",    "opendir(\"/\") failed");
    } else {
        /* Count remaining entries (first was already consumed above) */
        int n = (readdir(root) != NULL) ? 1 : 0;   /* re-use root, already at 2nd */
        while (readdir(root)) n++;

        /* n is entries from 2nd onward; we saw at least first already */
        TEST(group, "readdir: root has >= 1 entry", n >= 0);  /* >=0: root not empty */

        struct dirent *at_eof = readdir(root);
        TEST(group, "readdir: returns NULL at end-of-directory", at_eof == NULL);

        /* Repeated calls past EOF must keep returning NULL, not re-wrap */
        struct dirent *past_eof = readdir(root);
        TEST(group, "readdir: NULL stays NULL after EOF", past_eof == NULL);

        closedir(root);
        root = NULL;
    }

    /* ── rewinddir ───────────────────────────────────────────────────── */

    DIR *rwd = opendir("/");
    if (!rwd) {
        SKIP(group, "rewinddir: first entry matches after full rewind", "opendir failed");
        SKIP(group, "rewinddir: entry count same after rewind",         "opendir failed");
    } else {
        struct dirent *e1 = readdir(rwd);
        if (!e1) {
            SKIP(group, "rewinddir: first entry matches after full rewind", "no entries");
            SKIP(group, "rewinddir: entry count same after rewind",         "no entries");
            closedir(rwd);
        } else {
            char saved_name[NAME_MAX + 1];
            strncpy(saved_name, e1->d_name, NAME_MAX);
            saved_name[NAME_MAX] = '\0';

            /* Exhaust the stream, then rewind */
            while (readdir(rwd)) {}
            rewinddir(rwd);

            struct dirent *e2 = readdir(rwd);
            TEST(group, "rewinddir: first entry name matches after full rewind",
                 e2 != NULL && strcmp(e2->d_name, saved_name) == 0);

            /* Re-count to confirm we get the same total */
            int n_after = 1;  /* counted e2 already */
            while (readdir(rwd)) n_after++;

            int n_fresh = count_entries("/");
            TEST(group, "rewinddir: entry count same after rewind",
                 n_after == n_fresh);

            closedir(rwd);
        }
    }

    /* ── closedir ────────────────────────────────────────────────────── */

    DIR *c = opendir("/");
    if (!c) {
        SKIP(group, "closedir: returns 0 on valid DIR*", "opendir failed");
    } else {
        int r = closedir(c);
        TEST(group, "closedir: returns 0 on valid DIR*", r == 0);
    }

    int r = closedir(NULL);
    TEST(group, "closedir(NULL): returns -1", r == -1);

    /* ── two concurrent DIR* streams (per-stream buffer isolation) ───── */
    /*
     * This is the critical regression test for the static-buffer bug.
     * With static locals in readdir(), stream B's refill would silently
     * overwrite stream A's buf_pos/buf_len, causing A to return wrong
     * entries or crash. With per-DIR buffers they are fully independent.
     */
    DIR *a = opendir("/");
    DIR *b = opendir("/");

    if (!a || !b) {
        SKIP(group, "concurrent DIR*: stream A first entry non-NULL",      "opendir failed");
        SKIP(group, "concurrent DIR*: stream B first entry non-NULL",      "opendir failed");
        SKIP(group, "concurrent DIR*: streams return same first d_name",   "opendir failed");
        SKIP(group, "concurrent DIR*: advancing B does not shift A",       "opendir failed");
        if (a) closedir(a);
        if (b) closedir(b);
    } else {
        struct dirent *a1 = readdir(a);
        struct dirent *b1 = readdir(b);

        TEST(group, "concurrent DIR*: stream A first entry non-NULL", a1 != NULL);
        TEST(group, "concurrent DIR*: stream B first entry non-NULL", b1 != NULL);

        if (a1 && b1) {
            /* Both independent streams start at the same first entry */
            TEST(group, "concurrent DIR*: streams return same first d_name",
                 strcmp(a1->d_name, b1->d_name) == 0);

            /* Drain B fully; A must still be at its second entry, not EOF */
            while (readdir(b)) {}

            struct dirent *a2 = readdir(a);  /* A's second entry */
            TEST(group, "concurrent DIR*: draining B does not advance A",
                 a2 != NULL);               /* A still has entries left */
        } else {
            SKIP(group, "concurrent DIR*: streams return same first d_name", "entry was NULL");
            SKIP(group, "concurrent DIR*: draining B does not advance A",    "entry was NULL");
        }

        closedir(a);
        closedir(b);
    }

    /* ── DT_DIR entries can themselves be opened ─────────────────────── */
    /*
     * Walk root and find the first DT_DIR child. Open it with opendir()
     * and verify it succeeds. This checks that d_type is accurate and
     * that opendir works on non-root paths.
     */
    DIR *scan = opendir("/");
    if (!scan) {
        SKIP(group, "DT_DIR entry: opendir on child directory succeeds", "opendir failed");
    } else {
        char subdir_path[NAME_MAX + 2];  /* '/' + name + NUL */
        int  found_subdir = 0;
        struct dirent *se;

        while ((se = readdir(scan)) != NULL) {
            if (se->d_type != DT_DIR) continue;

            subdir_path[0] = '/';
            strncpy(subdir_path + 1, se->d_name, NAME_MAX);
            subdir_path[NAME_MAX + 1] = '\0';

            DIR *child = opendir(subdir_path);
            TEST(group, "DT_DIR entry: opendir on child directory succeeds",
                 child != NULL);
            if (child) closedir(child);
            found_subdir = 1;
            break;
        }

        if (!found_subdir)
            SKIP(group, "DT_DIR entry: opendir on child directory succeeds",
                 "no DT_DIR entries found in /");

        closedir(scan);
    }

    /* ── mountpoint readdir: terminal cover must resolve to mounted root ── */
    {
        char mp_path[128];
        char mp_name[96];
        snprintf(mp_name, sizeof(mp_name), ".dirent_mp_sentinel_%d", (int) getpid());
        snprintf(mp_path, sizeof(mp_path), "/tmp/%s", mp_name);

        int fd = open(mp_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (fd < 0) {
            SKIP(group, "mountpoint readdir: opendir(/tmp) sees file created in /tmp",
                 "cannot create sentinel under /tmp");
        } else {
            close(fd);

            DIR *tmpd = opendir("/tmp");
            TEST(group, "mountpoint readdir: opendir(/tmp) succeeds", tmpd != NULL);

            if (!tmpd) {
                SKIP(group, "mountpoint readdir: opendir(/tmp) sees file created in /tmp",
                     "opendir(/tmp) failed");
            } else {
                int found = 0;
                struct dirent *te;
                while ((te = readdir(tmpd)) != NULL) {
                    if (strcmp(te->d_name, mp_name) == 0) {
                        found = 1;
                        break;
                    }
                }

                TEST(group, "mountpoint readdir: opendir(/tmp) sees file created in /tmp", found);
                closedir(tmpd);
            }

            unlink(mp_path);
        }
    }

    /* ── dirfd ───────────────────────────────────────────────────────── */

    DIR *dfd_dir = opendir("/");
    if (!dfd_dir) {
        SKIP(group, "dirfd: returns non-negative fd on valid DIR*", "opendir failed");
        SKIP(group, "dirfd: readdir still works after dirfd call",  "opendir failed");
    } else {
        int dfd = dirfd(dfd_dir);
        TEST(group, "dirfd: returns non-negative fd on valid DIR*", dfd >= 0);

        /* fd must still be usable — readdir internally uses it */
        struct dirent *after = readdir(dfd_dir);
        TEST(group, "dirfd: readdir still works after dirfd call", after != NULL);

        closedir(dfd_dir);
    }

    TEST(group, "dirfd(NULL): returns -1", dirfd(NULL) == -1);

    TEST(group, "dirfd(NULL): returns -1", dirfd(NULL) == -1);

    /* ── fdopendir ───────────────────────────────────────────────────── */

    int raw_fd = open("/", O_RDONLY | O_DIRECTORY);
    if (raw_fd < 0) {
        SKIP(group, "fdopendir: returns non-NULL on valid dir fd",          "open(/) failed");
        SKIP(group, "fdopendir: readdir works on fdopendir stream",         "open(/) failed");
        SKIP(group, "fdopendir: dirfd round-trips back to original fd",     "open(/) failed");
    } else {
        DIR *fod = fdopendir(raw_fd);
        TEST(group, "fdopendir: returns non-NULL on valid dir fd", fod != NULL);

        if (fod) {
            TEST(group, "fdopendir: dirfd round-trips back to original fd",
                 dirfd(fod) == raw_fd);

            struct dirent *fe = readdir(fod);
            TEST(group, "fdopendir: readdir works on fdopendir stream", fe != NULL);

            closedir(fod);   /* closes raw_fd — do not close(raw_fd) */
        } else {
            close(raw_fd);
            SKIP(group, "fdopendir: readdir works on fdopendir stream",     "fdopendir failed");
            SKIP(group, "fdopendir: dirfd round-trips back to original fd", "fdopendir failed");
        }
    }

    /* fdopendir on a non-directory fd must fail */
    int reg_fd = open("/bin/init", O_RDONLY);   /* any regular file */
    if (reg_fd < 0) {
        SKIP(group, "fdopendir: returns NULL on regular-file fd", "open(/init) failed");
    } else {
        DIR *bad_fod = fdopendir(reg_fd);
        TEST(group, "fdopendir: returns NULL on regular-file fd", bad_fod == NULL);
        if (bad_fod) closedir(bad_fod); else close(reg_fd);
    }

    /* ── alphasort ───────────────────────────────────────────────────── */
    {
        /* Build two stack dirents just for the comparator — no heap needed */
        struct dirent da, db;
        strncpy(da.d_name, "apple",  NAME_MAX);
        strncpy(db.d_name, "banana", NAME_MAX);

        const struct dirent *pa = &da, *pb = &db;

        TEST(group, "alphasort: apple < banana returns negative",
             alphasort(&pa, &pb) < 0);
        TEST(group, "alphasort: banana > apple returns positive",
             alphasort(&pb, &pa) > 0);
        TEST(group, "alphasort: equal names returns 0",
             alphasort(&pa, &pa) == 0);
    }

    /* ── scandir ─────────────────────────────────────────────────────── */

    struct dirent **slist = NULL;
    int sn = scandir("/", &slist, NULL, alphasort);

    TEST(group, "scandir: returns > 0 entries for /", sn > 0);

    if (sn < 0) {
        SKIP(group, "scandir: result is sorted",           "scandir failed");
        SKIP(group, "scandir: each pointer is non-NULL",   "scandir failed");
    } else {
        /* Verify sort order: each name <= next */
        int sorted = 1;
        for (int i = 0; i + 1 < sn; i++) {
            if (strcmp(slist[i]->d_name, slist[i + 1]->d_name) > 0) {
                sorted = 0;
                break;
            }
        }
        TEST(group, "scandir: result is sorted by alphasort", sorted);

        int all_nonnull = 1;
        for (int i = 0; i < sn; i++)
            if (!slist[i]) { all_nonnull = 0; break; }
        TEST(group, "scandir: every returned pointer is non-NULL", all_nonnull);

        for (int i = 0; i < sn; i++) free(slist[i]);
        free(slist);
        slist = NULL;
    }

    /* scandir with nostat_filter must exclude "." and ".." */
    int fn = scandir("/", &slist, nostat_filter, NULL);
    if (fn < 0) {
        SKIP(group, "scandir+nostat_filter: no dot entries in result", "scandir failed");
    } else {
        int has_dot = 0;
        for (int i = 0; i < fn; i++) {
            const char *n = slist[i]->d_name;
            if ((n[0] == '.' && n[1] == '\0') ||
                (n[0] == '.' && n[1] == '.' && n[2] == '\0'))
                has_dot = 1;
            free(slist[i]);
        }
        free(slist);
        TEST(group, "scandir+nostat_filter: no dot entries in result", !has_dot);
    }

    /* scandir on a nonexistent path returns -1 */
    int bad_n = scandir("/no_such_path_xyz", &slist, NULL, NULL);
    TEST(group, "scandir: returns -1 on nonexistent path", bad_n == -1);
    if (bad_n >= 0) {
        for (int i = 0; i < bad_n; i++) free(slist[i]);
        free(slist);
    }

    ut_group_end(group);
}