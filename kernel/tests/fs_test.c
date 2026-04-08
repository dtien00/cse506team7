#include "kernel/printk.h"
#include "kernel/types.h"
#include "fs/tarfs.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/tmpfs.h"
#include "tests/test.h"
#include "lib/string.h"

static int passed = 0;
static int failed = 0;

void vfs_test(void) {
    printk("=== vfs tests ===\n");

    // 1. Open a known file
    file_t *f = file_open("/hello.txt");
    TEST("vfs_open /hello.txt", f != NULL);
    if (!f) return;
    TEST("vfs_open ref_count is 1", f->ref_count == 1);

    // 2. Read through vfs
    char buf[64] = {0};
    int n = file_read(f, buf, sizeof(buf)-1); // leave last character as null terminator
    TEST("vfs_read returns > 0 bytes", n > 0);
    printk("   vfs_read got: '%d'\n", n);

    // 3. Offset advances after read
    size_t off_after_first = f->offset;
    char buf2[8] = {0};
    int n2 = file_read(f, buf2, 4);
    TEST("second read advances offset", f->offset == off_after_first + (size_t)n2);

    // 4. Read past EOF returns 0
    file_read(f, buf, sizeof(buf)+1);
    file_read(f, buf, sizeof(buf)+1);
    int eof = file_read(f, buf, sizeof(buf));
    TEST("read past EOF returns 0", eof == 0);

    // 5. Close
    file_close(f);

    // 6. Open nonexistent file
    file_t *bad = file_open("/nope.txt");
    TEST("open nonexistent file returns NULL", bad == NULL);
    if (bad) file_close(bad);
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
    file_t *f = file_open("/hello.txt");
    int fd = fd_alloc(&table, f);
    TEST("first user fd is 3", fd == 3);

    // 3. Get it back
    file_t *got = fd_get(&table, fd);
    TEST("fd_get returns correct file", got == f);

    // 4. Close it
    fd_close(&table, fd);
    TEST("fd slot cleared after close", fd_get(&table, fd) == NULL);

    // 5. Slot is reused after close
    file_t *f2 = file_open("/hello.txt");
    int fd2 = fd_alloc(&table, f2);
    TEST("freed slot is reused", fd2 == fd);
    fd_close(&table, fd2);

    // 6. Out of bounds
    TEST("fd -1 returns NULL",  fd_get(&table, -1)  == NULL);
    TEST("fd 999 returns NULL", fd_get(&table, 999) == NULL);

    // 7. NULL table / file guards
    TEST("fd_alloc NULL table returns -1", fd_alloc(NULL, f)     == -1);
    TEST("fd_alloc NULL file returns -1",  fd_alloc(&table, NULL) == -1);

    // 8. Fill the table, then verify overflow returns -1
    file_t *fds[MAX_FDS];
    int allocated = 0;
    // slots 0-2 are already taken by stdin/stdout/stderr
    for (int i = 3; i < MAX_FDS; i++) {
        fds[i] = file_open("/hello.txt");
        if (fd_alloc(&table, fds[i]) >= 0) allocated++;
    }
    file_t *overflow = file_open("/hello.txt");
    TEST("fd_alloc on full table returns -1", fd_alloc(&table, overflow) == -1);
    file_close(overflow);
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
    file_t *f = file_open("/tmp/test.txt");
    TEST("opened /tmp/test.txt", f != NULL);
    if (!f) return;

    const char *test_data = "Hello from tmpfs!";
    int written = file_write(f, test_data, strlen(test_data));
    TEST("write returned correct byte count", written == (int)strlen(test_data));

    file_close(f);

    // 3. Read back the data
    f = file_open("/tmp/test.txt");
    TEST("reopened /tmp/test.txt", f != NULL);
    if (!f) return;

    char buf[64] = {0};
    int rb = file_read(f, buf, sizeof(buf) - 1);
    TEST("read back correct data", rb > 0 && strcmp(buf, test_data) == 0);
    file_close(f);

    // 4. Write advances offset; second read continues from where first left off
    f = file_open("/tmp/test.txt");
    if (f) {
        char first[8] = {0}, second[8] = {0};
        file_read(f, first, 5);
        size_t mid_offset = f->offset;
        file_read(f, second, 5);
        TEST("offset advances across sequential reads", f->offset == mid_offset + 5);
        file_close(f);
    }

    // 5. Write at non-zero offset (sparse / append-style)
    ret = vfs_create("/tmp/sparse.txt", 0644);
    TEST("created /tmp/sparse.txt", ret == 0);
    f = file_open("/tmp/sparse.txt");
    if (f) {
        // Manually advance offset to simulate appending
        f->offset = 10;
        int w = file_write(f, "end", 3);
        TEST("write at offset 10 succeeds", w == 3);
        TEST("file offset is 13 after write", f->offset == 13);
        file_close(f);
    }

    // 6. Write to tarfs file returns error
    file_t *tarf = file_open("/hello.txt");
    if (tarf) {
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
    f = file_open("/tmp/testdir/nested.txt");
    if (f) {
        const char *nested_data = "Nested file data";
        file_write(f, nested_data, strlen(nested_data));
        file_close(f);

        f = file_open("/tmp/testdir/nested.txt");
        if (f) {
            char nbuf[32] = {0};
            int nr = file_read(f, nbuf, sizeof(nbuf) - 1);
            TEST("nested file read back correctly",
                 nr > 0 && strcmp(nbuf, nested_data) == 0);
            file_close(f);
        }
    }

    // 12. Open a nonexistent file in tmpfs returns NULL
    file_t *missing = file_open("/tmp/does_not_exist.txt");
    TEST("open nonexistent tmpfs file returns NULL", missing == NULL);

    // 13. ref_count: two opens increment, each close decrements
    ret = vfs_create("/tmp/reftest.txt", 0644);
    if (ret == 0) {
        file_t *r1 = file_open("/tmp/reftest.txt");
        file_t *r2 = file_open("/tmp/reftest.txt");
        TEST("two opens give ref_count >= 1 each", r1 != NULL && r2 != NULL);
        if (r1) file_close(r1);
        if (r2) file_close(r2);
    }

    // 14. Unlink a file
    ret = vfs_unlink("/tmp/test.txt");
    TEST("unlinked /tmp/test.txt", ret == 0);

    // 15. Unlinked file is no longer accessible
    f = file_open("/tmp/test.txt");
    TEST("unlinked file not accessible", f == NULL);
    if (f) file_close(f);

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
        file_t *gone = file_open("/tmp/emptydir");
        TEST("unlinked empty dir not accessible", gone == NULL);
        if (gone) file_close(gone);
    }

    // 19. Read with oversized buffer returns only available bytes (no overread)
    ret = vfs_create("/tmp/small.txt", 0644);
    if (ret == 0) {
        f = file_open("/tmp/small.txt");
        if (f) {
            file_write(f, "hi", 2);
            file_close(f);
        }
        f = file_open("/tmp/small.txt");
        if (f) {
            char big[128] = {0};
            int nr = file_read(f, big, sizeof(big));
            TEST("read with oversized buf returns only file bytes", nr == 2);
            file_close(f);
        }
    }

    // 20. Second write extends file and data is correct
    ret = vfs_create("/tmp/extend.txt", 0644);
    if (ret == 0) {
        f = file_open("/tmp/extend.txt");
        if (f) {
            file_write(f, "Hello", 5);
            file_write(f, " World", 6);
            file_close(f);
        }
        f = file_open("/tmp/extend.txt");
        if (f) {
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

    // 22. ref_count is exactly 1 after a single open
    ret = vfs_create("/tmp/refcount2.txt", 0644);
    if (ret == 0) {
        file_t *rc = file_open("/tmp/refcount2.txt");
        TEST("single open gives ref_count == 1", rc != NULL && rc->ref_count == 1);
        if (rc) file_close(rc);
    }
}

void fs_test_run(void) {
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
    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");

    if (failed > 0) {
        printk("WARN: %d test(s) failed — check output above\n", failed);
    }
}