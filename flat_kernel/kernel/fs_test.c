#include "printk.h"
#include "types.h"
#include "tarfs.h"
#include "vfs.h"
#include "file.h"
#include "tmpfs.h"
#include "test.h"
#include "string.h"


void vfs_test(void) {
    printk("=== vfs tests ===\n");

    // 1. Open a known file
    file_t *f = vfs_open("hello.txt");
    if (!f) { printk("FAIL: vfs_open returned NULL\n"); return; }
    printk("PASS: vfs_open succeeded, ref_count=%d\n", f->ref_count);

    // 2. Read through vfs
    char buf[64] = {0};
    int n = vfs_read(f, buf, sizeof(buf) - 1);
    printk("PASS: vfs_read got %d bytes: '%s'\n", n, buf);

    // 3. Does offset advance after read?
    char buf2[8] = {0};
    int n2 = vfs_read(f, buf2, 4);   // should read from where we left off
    printk("PASS: second read from offset %zu got %d bytes\n", f->offset, n2);

    // 4. Read past EOF returns 0
    vfs_read(f, buf, sizeof(buf));
    vfs_read(f, buf, sizeof(buf));
    int eof = vfs_read(f, buf, sizeof(buf));
    printk("PASS: EOF read returned %d (expected 0)\n", eof);

    // 5. ref_count and close
    vfs_close(f);  // ref_count should hit 0 and kfree

    // 6. Open nonexistent file
    file_t *bad = vfs_open("nope.txt");
    if (bad) printk("FAIL: opened nonexistent file\n");
    else     printk("PASS: nonexistent open returned NULL\n");
}

void fd_test(void) {
    printk("=== fd table tests ===\n");

    fd_table_t table;
    fd_init(&table);

    // 1. Allocate an fd
    file_t *f = vfs_open("hello.txt");
    int fd = fd_alloc(&table, f);
    printk("PASS: allocated fd=%d\n", fd);  // expect 3 (0/1/2 = stdin/out/err)

    // 2. Get it back
    file_t *got = fd_get(&table, fd);
    if (got != f) printk("FAIL: fd_get returned wrong pointer\n");
    else          printk("PASS: fd_get returned correct file\n");

    // 3. Close it
    fd_close(&table, fd);
    file_t *closed = fd_get(&table, fd);
    if (closed) printk("FAIL: fd still valid after close\n");
    else        printk("PASS: fd correctly invalidated\n");

    // 4. Out of bounds
    file_t *oob = fd_get(&table, 999);
    if (oob) printk("FAIL: got non-NULL for fd=999\n");
    else     printk("PASS: out-of-bounds fd returned NULL\n");
}

void mount_test(void) {
    printk("=== mount tests ===\n");

    // 1. Find root mount
    mount_t *root_mnt = vfs_find_mount("/");
    if (!root_mnt) printk("FAIL: root mount not found\n");
    else           printk("PASS: found root mount at '/'\n");

    // 2. Find /tmp mount
    mount_t *tmp_mnt = vfs_find_mount("/tmp");
    if (!tmp_mnt) printk("FAIL: /tmp mount not found\n");
    else          printk("PASS: found /tmp mount\n");

    // 3. Verify /tmp is tmpfs
    if (tmp_mnt && strcmp(tmp_mnt->fs_type->name, "tmpfs") == 0) {
        printk("PASS: /tmp is tmpfs\n");
    } else {
        printk("FAIL: /tmp is not tmpfs\n");
    }

    // 4. Verify root is tarfs
    if (root_mnt && strcmp(root_mnt->fs_type->name, "tarfs") == 0) {
        printk("PASS: root is tarfs\n");
    } else {
        printk("FAIL: root is not tarfs\n");
    }

    // 5. Test path resolution for /tmp/foo
    mount_t *tmp_foo_mnt = vfs_find_mount("/tmp/foo");
    if (tmp_foo_mnt == tmp_mnt) {
        printk("PASS: /tmp/foo resolves to /tmp mount\n");
    } else {
        printk("FAIL: /tmp/foo doesn't resolve to /tmp mount\n");
    }
}

void tmpfs_test(void) {
    printk("=== tmpfs tests ===\n");

    // 1. Create a file in /tmp
    int ret = vfs_create("/tmp/test.txt", 0644);
    if (ret == 0) printk("PASS: created /tmp/test.txt\n");
    else          printk("FAIL: failed to create /tmp/test.txt\n");

    // 2. Open and write to the file
    file_t *f = vfs_open("/tmp/test.txt");
    if (!f) {
        printk("FAIL: failed to open /tmp/test.txt\n");
        return;
    }
    printk("PASS: opened /tmp/test.txt\n");

    const char *test_data = "Hello from tmpfs!";
    int written = vfs_write(f, test_data, strlen(test_data));
    if (written == (int)strlen(test_data)) {
        printk("PASS: wrote %d bytes to tmpfs file\n", written);
    } else {
        printk("FAIL: write returned %d, expected %zu\n", written, strlen(test_data));
    }

    vfs_close(f);

    // 3. Read back the data
    f = vfs_open("/tmp/test.txt");
    if (!f) {
        printk("FAIL: failed to reopen /tmp/test.txt\n");
        return;
    }

    char buf[64] = {0};
    int read_bytes = vfs_read(f, buf, sizeof(buf) - 1);
    if (read_bytes > 0 && strcmp(buf, test_data) == 0) {
        printk("PASS: read back correct data: '%s'\n", buf);
    } else {
        printk("FAIL: read returned %d bytes: '%s'\n", read_bytes, buf);
    }

    vfs_close(f);

    // 4. Create a directory
    ret = vfs_mkdir("/tmp/testdir", 0755);
    if (ret == 0) printk("PASS: created /tmp/testdir\n");
    else          printk("FAIL: failed to create /tmp/testdir\n");

    // 5. Create a file in the directory
    ret = vfs_create("/tmp/testdir/nested.txt", 0644);
    if (ret == 0) printk("PASS: created /tmp/testdir/nested.txt\n");
    else          printk("FAIL: failed to create nested file\n");

    // 6. Write to nested file
    f = vfs_open("/tmp/testdir/nested.txt");
    if (f) {
        const char *nested_data = "Nested file data";
        vfs_write(f, nested_data, strlen(nested_data));
        vfs_close(f);
        printk("PASS: wrote to nested file\n");
    }

    // 7. Unlink a file
    ret = vfs_unlink("/tmp/test.txt");
    if (ret == 0) printk("PASS: unlinked /tmp/test.txt\n");
    else          printk("FAIL: failed to unlink file\n");

    // 8. Verify file is gone
    f = vfs_open("/tmp/test.txt");
    if (!f) printk("PASS: unlinked file no longer accessible\n");
    else {
        printk("FAIL: unlinked file still accessible\n");
        vfs_close(f);
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
}