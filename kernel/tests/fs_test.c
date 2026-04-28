#include "kernel/printk.h"
#include "kernel/types.h"
#include "fs/vfs.h"
#include "fs/dentry.h"
#include "fs/file.h"
#include "fs/tmpfs.h"
#include "process/process.h"
#include "tests/test.h"
#include "lib/string.h"
#include "errno.h"
#include "mm/kmalloc.h"

#define SEEK_SET 0
#define SEEK_CUR 1

static int passed = 0;
static int failed = 0;

static struct proc *fs_test_prev_current = NULL;

static int fs_test_bind_to_init_proc(void) {
    fs_test_prev_current = current;
    struct proc *init = proc_find(1);
    if (!init || !init->fs) {
        fs_test_prev_current = NULL;
        return -1;
    }
    current = init;
    return 0;
}

static void fs_test_restore_current(void) {
    current = fs_test_prev_current;
    fs_test_prev_current = NULL;
}

void vfs_test(void) {
    printk("=== vfs tests ===\n");

    // 1. Open a known file
    int fd = file_open("/hello.txt");
    TEST("vfs_open /hello.txt", fd >= 0);
    if (fd < 0) return;

    // 2. Read through vfs
    char buf[64] = {0};
    int n = file_read(fd, buf, sizeof(buf)-1); // leave last character as null terminator
    TEST("vfs_read returns > 0 bytes", n > 0);
    printk("   vfs_read got: '%d'\n", n);

    // 3. Offset advances after read
    char buf2[8] = {0};
    int n2 = file_read(fd, buf2, 4);
    TEST("second read returns requested bytes", n2 == 4);

    // 4. Read past EOF returns 0
    file_read(fd, buf, sizeof(buf)+1);
    file_read(fd, buf, sizeof(buf)+1);
    int eof = file_read(fd, buf, sizeof(buf));
    TEST("read past EOF returns 0", eof == 0);

    // 5. Close
    file_close(fd);

    // 6. Open nonexistent file
    int bad = file_open("/nope.txt");
    TEST("open nonexistent file returns error", bad < 0);
    if (bad >= 0) file_close(bad);
}

void fd_test(void) {
    printk("=== fd table tests ===\n");

    fd_table_t table;
    fd_init(&table);

    // 1. stdin/stdout/stderr are initialized correctly
    file_t *stdin_f  = fd_get(&table, 0);
    file_t *stdout_f = fd_get(&table, 1);
    file_t *stderr_f = fd_get(&table, 2);
    TEST("fd 0 is FILE_STDIN",  stdin_f  != NULL && stdin_f->type  == FILE_STDIN);
    TEST("fd 1 is FILE_STDOUT", stdout_f != NULL && stdout_f->type == FILE_STDOUT);
    TEST("fd 2 is FILE_STDOUT", stderr_f != NULL && stderr_f->type == FILE_STDOUT);

    // 2. Allocate an fd — should land at 3
    file_t *f = kzalloc(sizeof(file_t));
    if (f) {
        f->type = FILE_STDIN;
        f->ref_count = 1;
    }
    int fd = fd_alloc(&table, f);
    TEST("first user fd is 3", fd == 3);

    // 3. Get it back
    file_t *got = fd_get(&table, fd);
    TEST("fd_get returns correct file", got == f);

    // 4. Close it
    fd_close(&table, fd);
    TEST("fd slot cleared after close", fd_get(&table, fd) == NULL);

    // 5. Slot is reused after close
    file_t *f2 = kzalloc(sizeof(file_t));
    if (f2) {
        f2->type = FILE_STDIN;
        f2->ref_count = 1;
    }
    int fd2 = fd_alloc(&table, f2);
    TEST("freed slot is reused", fd2 == fd);
    fd_close(&table, fd2);

    // 6. Out of bounds
    TEST("fd -1 returns NULL",  fd_get(&table, -1)  == NULL);
    TEST("fd 999 returns NULL", fd_get(&table, 999) == NULL);

    // 7. NULL table / file guards
    TEST("fd_alloc NULL table returns -1", fd_alloc(NULL, f)      == -1);
    TEST("fd_alloc NULL file returns -1",  fd_alloc(&table, NULL) == -1);

    // 8. Fill the table, then verify overflow returns -1
    file_t *fds[MAX_FDS];
    int allocated = 0;
    // slots 0-2 are already taken by stdin/stdout/stderr
    for (int i = 3; i < MAX_FDS; i++) {
        fds[i] = kzalloc(sizeof(file_t));
        if (fds[i]) {
            fds[i]->type = FILE_STDIN;
            fds[i]->ref_count = 1;
        }
        if (fd_alloc(&table, fds[i]) >= 0) allocated++;
    }
    file_t *overflow = kzalloc(sizeof(file_t));
    if (overflow) {
        overflow->type = FILE_STDIN;
        overflow->ref_count = 1;
    }
    TEST("fd_alloc on full table returns -1", fd_alloc(&table, overflow) == -1);
    if (overflow)
        kfree(overflow);
    for (int i = 3; i < MAX_FDS; i++) fd_close(&table, i);
}

void mount_test(void) {
    printk("=== mount tests ===\n");

    // 1. Find root mount
    mount_t *root_mnt = vfs_find_mount("/");
    TEST("root mount found", root_mnt != NULL);

    // 2. Find /tmp mount
    mount_t *tmp_mnt = vfs_find_mount("/tmp");
    TEST("/tmp mount found", tmp_mnt != NULL);

    // 3. Verify /tmp is tmpfs
    TEST("/tmp is tmpfs",
         tmp_mnt != NULL && strcmp(tmp_mnt->fs_type->name, "tmpfs") == 0);

    // 4. Verify root is tarfs
    TEST("root is tarfs",
         root_mnt != NULL && strcmp(root_mnt->fs_type->name, "tarfs") == 0);

    // 5. /tmp/foo resolves to /tmp, not root
    mount_t *tmp_foo_mnt = vfs_find_mount("/tmp/foo");
    TEST("/tmp/foo resolves to /tmp mount", tmp_foo_mnt == tmp_mnt);

    // 6. Root and /tmp are distinct mounts
    TEST("/tmp mount differs from root mount", tmp_mnt != root_mnt);

    // 7. Nested path still resolves longest prefix
    mount_t *deep = vfs_find_mount("/tmp/a/b/c");
    TEST("/tmp/a/b/c resolves to /tmp mount", deep == tmp_mnt);
}

void tmpfs_test(void) {
    printk("=== tmpfs tests ===\n");

    // 1. Create a file in /tmp
    int ret = vfs_create("/tmp/test.txt", 0644);
    TEST("created /tmp/test.txt", ret == 0);

    // 2. Open and write to the file
    int f = file_open_with_flags("/tmp/test.txt", O_WRONLY, 0);
    TEST("opened /tmp/test.txt", f >= 0);
    if (f < 0) return;

    const char *test_data = "Hello from tmpfs!";
    int written = file_write(f, test_data, strlen(test_data));
    TEST("write returned correct byte count", written == (int)strlen(test_data));

    file_close(f);

    // 3. Read back the data
    f = file_open("/tmp/test.txt");
    TEST("reopened /tmp/test.txt", f >= 0);
    if (f < 0) return;

    char buf[64] = {0};
    int rb = file_read(f, buf, sizeof(buf) - 1);
    TEST("read back correct data", rb > 0 && strcmp(buf, test_data) == 0);
    file_close(f);

    // 4. Write advances offset; second read continues from where first left off
    f = file_open("/tmp/test.txt");
    if (f >= 0) {
        char first[8] = {0}, second[8] = {0};
        file_read(f, first, 5);
        int mid_offset = file_lseek(f, 0, SEEK_CUR);
        file_read(f, second, 5);
        int end_offset = file_lseek(f, 0, SEEK_CUR);
        TEST("offset advances across sequential reads", mid_offset >= 0 && end_offset == mid_offset + 5);
        file_close(f);
    }

    // 5. Write at non-zero offset (sparse / append-style)
    ret = vfs_create("/tmp/sparse.txt", 0644);
    TEST("created /tmp/sparse.txt", ret == 0);
    f = file_open_with_flags("/tmp/sparse.txt", O_WRONLY, 0);
    if (f >= 0) {
        // Advance offset to simulate appending
        int seek_rc = file_lseek(f, 10, SEEK_SET);
        TEST("seek to offset 10 succeeds", seek_rc == 10);
        int w = file_write(f, "end", 3);
        TEST("write at offset 10 succeeds", w == 3);
        TEST("file offset is 13 after write", file_lseek(f, 0, SEEK_CUR) == 13);
        file_close(f);
    }

    // 6. Write to tarfs file returns error
    int tarf = file_open("/hello.txt");
    if (tarf >= 0) {
        int w = file_write(tarf, "x", 1);
        TEST("write to read-only tarfs returns error", w < 0);
        file_close(tarf);
    }

    // 7. Create duplicate file returns error
    ret = vfs_create("/tmp/test.txt", 0644);
    TEST("creating duplicate file returns error", ret < 0);

    // 8. Create a directory
    ret = vfs_mkdir("/tmp/testdir", 0755);
    TEST("created /tmp/testdir", ret == 0);

    // 9. Duplicate mkdir returns error
    ret = vfs_mkdir("/tmp/testdir", 0755);
    TEST("duplicate mkdir returns error", ret < 0);

    // 10. Create a file in the directory
    ret = vfs_create("/tmp/testdir/nested.txt", 0644);
    TEST("created /tmp/testdir/nested.txt", ret == 0);

    // 11. Write and read nested file
    f = file_open_with_flags("/tmp/testdir/nested.txt", O_WRONLY, 0);
    if (f >= 0) {
        const char *nested_data = "Nested file data";
        file_write(f, nested_data, strlen(nested_data));
        file_close(f);

        f = file_open("/tmp/testdir/nested.txt");
        if (f >= 0) {
            char nbuf[32] = {0};
            int nr = file_read(f, nbuf, sizeof(nbuf) - 1);
            TEST("nested file read back correctly",
                 nr > 0 && strcmp(nbuf, nested_data) == 0);
            file_close(f);
        }
    }

    // 12. Open a nonexistent file in tmpfs returns NULL
    int missing = file_open("/tmp/does_not_exist.txt");
    TEST("open nonexistent tmpfs file returns error", missing < 0);

    // 13. ref_count: two opens increment, each close decrements
    ret = vfs_create("/tmp/reftest.txt", 0644);
    if (ret == 0) {
        int r1 = file_open("/tmp/reftest.txt");
        int r2 = file_open("/tmp/reftest.txt");
        TEST("two opens return valid fds", r1 >= 0 && r2 >= 0);
        if (r1 >= 0) file_close(r1);
        if (r2 >= 0) file_close(r2);
    }

    // 14. Unlink a file
    ret = vfs_unlink("/tmp/test.txt");
    TEST("unlinked /tmp/test.txt", ret == 0);

    // 15. Unlinked file is no longer accessible
    f = file_open("/tmp/test.txt");
    TEST("unlinked file not accessible", f < 0);
    if (f >= 0) file_close(f);

    // 16. Unlink nonexistent file returns error
    ret = vfs_unlink("/tmp/test.txt");
    TEST("unlink nonexistent file returns error", ret < 0);

    // 17. Unlink non-empty directory returns error
    ret = vfs_unlink("/tmp/testdir");
    TEST("unlink non-empty dir returns error", ret < 0);

    // 18. Unlink empty directory succeeds
    ret = vfs_mkdir("/tmp/emptydir", 0755);
    if (ret == 0) {
        ret = vfs_unlink("/tmp/emptydir");
        TEST("unlink empty dir succeeds", ret == 0);
        int gone = file_open("/tmp/emptydir");
        TEST("unlinked empty dir not accessible", gone < 0);
        if (gone >= 0) file_close(gone);
    }

    // 19. Read with oversized buffer returns only available bytes (no overread)
    ret = vfs_create("/tmp/small.txt", 0644);
    if (ret == 0) {
        f = file_open_with_flags("/tmp/small.txt", O_WRONLY, 0);
        if (f >= 0) {
            file_write(f, "hi", 2);
            file_close(f);
        }
        f = file_open("/tmp/small.txt");
        if (f >= 0) {
            char big[128] = {0};
            int nr = file_read(f, big, sizeof(big));
            TEST("read with oversized buf returns only file bytes", nr == 2);
            file_close(f);
        }
    }

    // 20. Second write extends file and data is correct
    ret = vfs_create("/tmp/extend.txt", 0644);
    if (ret == 0) {
        f = file_open_with_flags("/tmp/extend.txt", O_WRONLY, 0);
        if (f >= 0) {
            file_write(f, "Hello", 5);
            file_write(f, " World", 6);
            file_close(f);
        }
        f = file_open("/tmp/extend.txt");
        if (f >= 0) {
            char ebuf[16] = {0};
            int nr = file_read(f, ebuf, sizeof(ebuf) - 1);
            TEST("sequential writes extend file correctly",
                 nr == 11 && strcmp(ebuf, "Hello World") == 0);
            file_close(f);
        }
    }

    // 21. Create file with missing intermediate directory returns error
    ret = vfs_create("/tmp/nodir/file.txt", 0644);
    TEST("create with missing parent dir returns error", ret < 0);

    // 22. Single open returns a valid fd
    ret = vfs_create("/tmp/refcount2.txt", 0644);
    if (ret == 0) {
        int rc = file_open("/tmp/refcount2.txt");
        TEST("single open returns a valid fd", rc >= 0);
        if (rc >= 0) file_close(rc);
    }

    // 23. chmod persists on tmpfs and blocks reopen for denied modes
    ret = vfs_create("/tmp/perm_enforced.txt", 0644);
    TEST("created /tmp/perm_enforced.txt", ret == 0);
    if (ret == 0) {
        ret = vfs_chmod("/tmp/perm_enforced.txt", 0000);
        TEST("chmod tmpfs file to 0000 succeeds", ret == 0);

        struct stat st = {0};
        ret = file_stat("/tmp/perm_enforced.txt", &st);
        TEST("stat after chmod on tmpfs succeeds", ret == 0);
        TEST("stat reflects tmpfs chmod", ret == 0 && (st.st_mode & 0777) == 0);

        int denied = file_open("/tmp/perm_enforced.txt");
        TEST("read open denied after chmod 0000", denied == -EACCES);
        if (denied >= 0) file_close(denied);

        ret = vfs_chmod("/tmp/perm_enforced.txt", 0600);
        TEST("chmod tmpfs file back to 0600 succeeds", ret == 0);
        int allowed = file_open_with_flags("/tmp/perm_enforced.txt", O_WRONLY, 0);
        TEST("write open allowed after chmod 0600", allowed >= 0);
        if (allowed >= 0) file_close(allowed);
    }

    // 24. directory execute bit gates traversal / creation
    ret = vfs_mkdir("/tmp/no_search", 0755);
    TEST("created /tmp/no_search", ret == 0);
    if (ret == 0) {
        ret = vfs_chmod("/tmp/no_search", 0666);
        TEST("chmod dir to remove execute bit succeeds", ret == 0);

        ret = vfs_create("/tmp/no_search/file.txt", 0644);
        TEST("create in non-searchable dir is denied", ret == -EACCES);

        ret = vfs_chmod("/tmp/no_search", 0777);
        TEST("chmod dir to restore execute bit succeeds", ret == 0);

        ret = vfs_create("/tmp/no_search/file.txt", 0644);
        TEST("create in searchable dir succeeds", ret == 0);
    }
}

void dentry_test(void) {
    printk("=== dentry / vfs_lookup tests ===\n");

    // ── vfs_lookup basics ────────────────────────────────────────────

    // Root lookup: succeeds, correct type, ref held
    {
        dentry_t *d = NULL;
        int err = vfs_lookup("/", &d);
        TEST("vfs_lookup '/' returns 0", err == 0);
        TEST("vfs_lookup '/' dentry not NULL", d != NULL);
        if (d) {
            TEST("'/' is VNODE_DIR", d->d_vnode && d->d_vnode->type == VNODE_DIR);
            TEST("'/' ref >= 1", d->d_ref >= 1);
            dentry_put(d);
        }
    }

    // Nonexistent path: ENOENT, out-param cleared
    {
        dentry_t *d = NULL;
        int err = vfs_lookup("/this/does/not/exist/ever", &d);
        TEST("vfs_lookup nonexistent returns -ENOENT", err == -ENOENT);
        TEST("vfs_lookup nonexistent out-param is NULL", d == NULL);
    }

    // Known tarfs file at root
    {
        dentry_t *d = NULL;
        int err = vfs_lookup("/hello.txt", &d);
        TEST("vfs_lookup '/hello.txt' succeeds", err == 0);
        if (d) {
            TEST("'/hello.txt' is VNODE_REG",
                 d->d_vnode && d->d_vnode->type == VNODE_REG);
            dentry_put(d);
        }
    }

    // Known directory
    {
        dentry_t *d = NULL;
        int err = vfs_lookup("/bin", &d);
        TEST("vfs_lookup '/bin' succeeds", err == 0);
        if (d) {
            TEST("'/bin' is VNODE_DIR",
                 d->d_vnode && d->d_vnode->type == VNODE_DIR);
            dentry_put(d);
        }
    }

    // Multi-component nested path
    {
        dentry_t *d = NULL;
        int err = vfs_lookup("/bin/init", &d);
        TEST("vfs_lookup '/bin/init' succeeds", err == 0);
        if (d) {
            TEST("'/bin/init' is VNODE_REG",
                 d->d_vnode && d->d_vnode->type == VNODE_REG);
            dentry_put(d);
        }
    }

    // Mount-point lookup crosses into the mounted fs
    {
        dentry_t *d = NULL;
        int err = vfs_lookup("/tmp", &d);
        TEST("vfs_lookup '/tmp' succeeds (crosses tmpfs mount)", err == 0);
        if (d) {
            TEST("'/tmp' resolves to VNODE_DIR",
                 d->d_vnode && d->d_vnode->type == VNODE_DIR);
            dentry_put(d);
        }
    }

    // ── Dentry cache ─────────────────────────────────────────────────

    // Two successive lookups of the same path return the same pointer
    {
        dentry_t *d1 = NULL, *d2 = NULL;
        int e1 = vfs_lookup("/hello.txt", &d1);
        int e2 = vfs_lookup("/hello.txt", &d2);
        TEST("two lookups of '/hello.txt' both succeed", e1 == 0 && e2 == 0);
        if (e1 == 0 && e2 == 0)
            TEST("cache hit: same dentry pointer", d1 == d2);
        if (e1 == 0) dentry_put(d1);
        if (e2 == 0) dentry_put(d2);
    }

    // ── vfs_lookup_from ──────────────────────────────────────────────

    // Relative lookup from root dentry
    {
        dentry_t *d = NULL;
        int err = vfs_lookup_from(vfs_get_root(), "hello.txt", &d);
        TEST("vfs_lookup_from(root, 'hello.txt') succeeds", err == 0);
        if (d) dentry_put(d);
    }

    // Absolute path from non-root start still anchors at '/'
    {
        dentry_t *d = NULL;
        int err = vfs_lookup_from(vfs_get_root(), "/bin/init", &d);
        TEST("vfs_lookup_from(root, '/bin/init') succeeds", err == 0);
        if (d) dentry_put(d);
    }

    // ── dentry_normalize_path ────────────────────────────────────────

    // Double slashes collapsed
    {
        char out[256];
        int err = dentry_normalize_path(vfs_get_root(), vfs_get_root(),
                                        "//hello.txt", out, sizeof(out));
        TEST("normalize '//hello.txt' returns 0", err == 0);
        TEST("normalize '//hello.txt' -> '/hello.txt'",
             err == 0 && strcmp(out, "/hello.txt") == 0);
    }

    // Trailing slash stripped
    {
        char out[256];
        int err = dentry_normalize_path(vfs_get_root(), vfs_get_root(),
                                        "/bin/", out, sizeof(out));
        TEST("normalize '/bin/' returns 0", err == 0);
        TEST("normalize '/bin/' -> '/bin'",
             err == 0 && strcmp(out, "/bin") == 0);
    }

    // Dot component is a no-op
    {
        char out[256];
        int err = dentry_normalize_path(vfs_get_root(), vfs_get_root(),
                                        "/bin/.", out, sizeof(out));
        TEST("normalize '/bin/.' returns 0", err == 0);
        TEST("normalize '/bin/.' -> '/bin'",
             err == 0 && strcmp(out, "/bin") == 0);
    }

    // Dotdot goes up one level
    {
        char out[256];
        int err = dentry_normalize_path(vfs_get_root(), vfs_get_root(),
                                        "/bin/../hello.txt", out, sizeof(out));
        TEST("normalize '/bin/../hello.txt' returns 0", err == 0);
        TEST("normalize '/bin/../hello.txt' -> '/hello.txt'",
             err == 0 && strcmp(out, "/hello.txt") == 0);
    }

    // Dotdot at root is clamped (no underflow)
    {
        char out[256];
        int err = dentry_normalize_path(vfs_get_root(), vfs_get_root(),
                                        "/../etc", out, sizeof(out));
        TEST("normalize '/../etc' returns 0", err == 0);
        TEST("normalize '/../etc' -> '/etc'",
             err == 0 && strcmp(out, "/etc") == 0);
    }

    // ── dentry_split_path ────────────────────────────────────────────

    // Three components
    {
        char buf[] = "/foo/bar/baz";
        char *comps[8];
        int n = dentry_split_path(buf, comps, 8);
        TEST("split '/foo/bar/baz' -> 3 components", n == 3);
        TEST("split comp[0]='foo'", n >= 1 && strcmp(comps[0], "foo") == 0);
        TEST("split comp[1]='bar'", n >= 2 && strcmp(comps[1], "bar") == 0);
        TEST("split comp[2]='baz'", n >= 3 && strcmp(comps[2], "baz") == 0);
    }

    // Root yields 0 components
    {
        char buf[] = "/";
        char *comps[8];
        int n = dentry_split_path(buf, comps, 8);
        TEST("split '/' -> 0 components", n == 0);
    }

    // Single component
    {
        char buf[] = "/foo";
        char *comps[8];
        int n = dentry_split_path(buf, comps, 8);
        TEST("split '/foo' -> 1 component", n == 1);
        TEST("split '/foo' comp[0]='foo'",
             n >= 1 && strcmp(comps[0], "foo") == 0);
    }

    // max_comps overflow returns -1
    {
        char buf[] = "/a/b/c";
        char *comps[2];
        int n = dentry_split_path(buf, comps, 2);
        TEST("split 3-part path with max_comps=2 returns -1", n == -1);
    }
}

void fs_test_run(void) {
    if (fs_test_bind_to_init_proc() < 0) {
        TEST("fs_test setup: bind current to pid 1", 0);
        return;
    }

    printk("\n");
    printk("========================================\n");
    printk("       Filesystem Tests\n");
    printk("========================================\n");
    printk("\n");

    vfs_test();
    printk("\n");

    fd_test();
    printk("\n");

    mount_test();
    printk("\n");

    tmpfs_test();
    printk("\n");

    dentry_test();
    printk("\n");
    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");

    if (failed > 0) {
        printk("WARN: %d test(s) failed — check output above\n", failed);
    }

    fs_test_restore_current();
}
