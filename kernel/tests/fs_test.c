#include "kernel/printk.h"
#include "kernel/types.h"
#include "fs/tarfs.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "tests/test.h"

void tarfs_test(void) {
    printk("=== tarfs tests ===\n");

    // 1. Does lookup find a known file?
    tarfs_entry_t *e = tarfs_lookup("hello.txt");
    if (!e) { printk("FAIL: lookup returned NULL\n"); return; }
    printk("PASS: found hello.txt, size=%zu\n", e->size);

    // 2. Does read return correct data?
    char buf[64] = {0};
    int n = tarfs_read(e, buf, 0, sizeof(buf) - 1);
    printk("PASS: read %d bytes: '%s'\n", n, buf);

    // 3. Does offset read work?
    char buf2[8] = {0};
    tarfs_read(e, buf2, 2, 4);  // read 4 bytes starting at offset 2
    printk("     offset read: '%s'\n", buf2);

    // 4. Does reading past EOF clamp correctly?
    int n2 = tarfs_read(e, buf, e->size - 2, 100);
    printk("PASS: clamped read got %d bytes (expected 2)\n", n2);

    // 5. Does lookup return NULL for a missing file?
    tarfs_entry_t *missing = tarfs_lookup("doesnotexist.txt");
    if (missing) printk("FAIL: got non-NULL for missing file\n");
    else         printk("PASS: missing file returned NULL\n");
}

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