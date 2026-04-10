#include "kernel/printk.h"
#include "kernel/types.h"
#include "fs/vfs.h"
#include "fs/ext2.h"
#include "fs/block_cache.h"
#include "mm/kmalloc.h"
#include "tests/test.h"
#include "lib/string.h"

static int passed = 0;
static int failed = 0;

/* ELF64 magic */
#define ELF_MAG0   0x7F
#define ELF_MAG1   'E'
#define ELF_MAG2   'L'
#define ELF_MAG3   'F'
#define ELFCLASS64 2

/*
 * Read len bytes from a vnode starting at offset.
 * Returns bytes read, or -1 on error.
 */
static int vnode_read(vnode_t *vn, void *buf, size_t offset, size_t len) {
    if (!vn || !vn->fs_type || !vn->fs_type->ops.read)
        return -1;
    return vn->fs_type->ops.read(vn, buf, offset, len);
}

/*
 * Check a regular file: exists, correct type, minimum size, valid ELF64 magic,
 * and a full sequential read that matches the reported size.
 * Returns 1 if all checks pass.
 */
static int check_file(const char *path, size_t min_size, int check_elf) {
    int ok = 1;

    dentry_t *dent = vfs_lookup(path);
    if (!dent || !dent->d_vnode) {
        printk("  [FAIL] %s: vfs_lookup returned NULL\n", path);
        failed++;
        return 0;
    }
    printk("  [PASS] %s: exists\n", path);
    passed++;

    vnode_t *vn = dent->d_vnode;

    /* type must be VNODE_REG */
    if (vn->type != VNODE_REG) {
        printk("  [FAIL] %s: wrong vnode type %d (expected VNODE_REG=%d)\n",
               path, vn->type, VNODE_REG);
        failed++;
        ok = 0;
    } else {
        printk("  [PASS] %s: vnode type is VNODE_REG\n", path);
        passed++;
    }

    /* size */
    size_t sz = vn->size;
    printk("  [INFO] %s: reported size = %d bytes\n", path, (int)sz);
    if (sz < min_size) {
        printk("  [FAIL] %s: size %d < minimum %d\n", path, (int)sz, (int)min_size);
        failed++;
        ok = 0;
    } else {
        printk("  [PASS] %s: size OK\n", path);
        passed++;
    }

    if (!check_elf || sz == 0)
        return ok;

    /* ELF magic — read first 16 bytes */
    unsigned char hdr[16];
    int n = vnode_read(vn, hdr, 0, sizeof(hdr));
    if (n < 16) {
        printk("  [FAIL] %s: could only read %d bytes for ELF header\n", path, n);
        failed++;
        ok = 0;
    } else if (hdr[0] != ELF_MAG0 || hdr[1] != ELF_MAG1 ||
               hdr[2] != ELF_MAG2 || hdr[3] != ELF_MAG3) {
        printk("  [FAIL] %s: bad ELF magic: %02x %02x %02x %02x\n",
               path, hdr[0], hdr[1], hdr[2], hdr[3]);
        failed++;
        ok = 0;
    } else if (hdr[4] != ELFCLASS64) {
        printk("  [FAIL] %s: not ELF64 (class=%d)\n", path, hdr[4]);
        failed++;
        ok = 0;
    } else {
        printk("  [PASS] %s: valid ELF64 header\n", path);
        passed++;
    }

    /* Full sequential read — verify total bytes matches reported size */
    size_t total = 0;
    char blk[512];
    int r;
    while (total < sz) {
        size_t want = sz - total;
        if (want > sizeof(blk)) want = sizeof(blk);
        r = vnode_read(vn, blk, total, want);
        if (r <= 0) {
            printk("  [FAIL] %s: read stopped at offset %d (got %d)\n",
                   path, (int)total, r);
            failed++;
            ok = 0;
            return ok;
        }
        total += (size_t)r;
    }
    if (total != sz) {
        printk("  [FAIL] %s: sequential read got %d bytes, size=%d\n",
               path, (int)total, (int)sz);
        failed++;
        ok = 0;
    } else {
        printk("  [PASS] %s: full sequential read OK (%d bytes)\n", path, (int)total);
        passed++;
    }

    return ok;
}

static void check_dir(const char *path) {
    dentry_t *dent = vfs_lookup(path);
    if (!dent || !dent->d_vnode) {
        printk("  [FAIL] %s: directory missing\n", path);
        failed++;
    } else if (dent->d_vnode->type != VNODE_DIR) {
        printk("  [FAIL] %s: exists but vnode type=%d (expected VNODE_DIR=%d)\n",
               path, dent->d_vnode->type, VNODE_DIR);
        failed++;
    } else {
        printk("  [PASS] %s: directory OK\n", path);
        passed++;
    }
}

/*
 * check_readdir_vfs — call vfs_readdir on a directory vnode and list what it
 * returns.  This is exactly what `ls` does, so a failure here means ls will
 * show nothing even if the files physically exist on disk.
 */
static void check_readdir_vfs(const char *path) {
    printk("  [readdir-vfs] %s\n", path);

    dentry_t *dent = vfs_lookup(path);
    if (!dent || !dent->d_vnode) {
        printk("  [FAIL] %s: vfs_lookup returned NULL\n", path);
        failed++;
        return;
    }

    vnode_t *vn = dent->d_vnode;
    if (vn->type != VNODE_DIR) {
        printk("  [FAIL] %s: not a directory (type=%d)\n", path, vn->type);
        failed++;
        return;
    }

    /* Check that the fs_type has a readdir op at all */
    if (!vn->fs_type || !vn->fs_type->ops.readdir) {
        printk("  [FAIL] %s: fs '%s' has no readdir op — ls will see nothing!\n",
               path, vn->fs_type ? vn->fs_type->name : "<null>");
        failed++;
        return;
    }

    char buf[2048];
    uint32_t nents = 0;
    int rc = vfs_readdir(vn, 0, buf, sizeof(buf), &nents);
    if (rc < 0) {
        printk("  [FAIL] %s: vfs_readdir returned %d\n", path, rc);
        failed++;
        return;
    }

    if (nents == 0) {
        printk("  [FAIL] %s: vfs_readdir returned 0 entries (directory appears empty to ls)\n", path);
        failed++;
        return;
    }

    printk("  [PASS] %s: vfs_readdir returned %d entries:\n", path, nents);
    passed++;

    /* Walk the fs_dent_t records and print each name */
    uint32_t off = 0;
    for (uint32_t i = 0; i < nents; i++) {
        if (off + sizeof(fs_dent_t) > sizeof(buf)) break;
        fs_dent_t *de = (fs_dent_t *)(buf + off);
        printk("         [%d] ino=%lu type=%d name=%s\n",
               i, (unsigned long)de->d_ino, (int)de->d_type, de->d_name);
        if (de->d_reclen == 0) break;
        off += de->d_reclen;
    }
}

/*
 * check_raw_ext2_dir — directly read the on-disk ext2 directory block for a
 * given inode and dump every dirent.  This bypasses the VFS readdir op
 * entirely, so it tells us whether the data is *physically on disk* even when
 * the readdir op is missing or broken.
 */
static void check_raw_ext2_dir(const char *path) {
    printk("  [raw-ext2-dir] %s\n", path);

    mount_t *mnt = vfs_find_mount("/");
    if (!mnt || !mnt->fs_data) {
        printk("  [FAIL] raw-ext2-dir: no ext2 mount\n");
        failed++;
        return;
    }

    dentry_t *dent = vfs_lookup(path);
    if (!dent || !dent->d_vnode) {
        printk("  [FAIL] %s: vfs_lookup returned NULL\n", path);
        failed++;
        return;
    }

    vnode_t *vn = dent->d_vnode;
    if (vn->type != VNODE_DIR) {
        printk("  [FAIL] %s: not a directory\n", path);
        failed++;
        return;
    }

    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;
    ext2_inode_t *inode = (ext2_inode_t *)vn->fs_data;
    if (!inode) {
        printk("  [FAIL] %s: vnode has no inode data\n", path);
        failed++;
        return;
    }

    printk("  [INFO] %s: inode=%lu size=%u i_block[0]=%u\n",
           path, (unsigned long)vn->ino, inode->i_size, inode->i_block[0]);

    if (inode->i_block[0] == 0) {
        printk("  [FAIL] %s: i_block[0] == 0, directory block never allocated\n", path);
        failed++;
        return;
    }

    /* Read the first directory block directly */
    uint8_t *blkbuf = (uint8_t *)kmalloc(ext2_mnt->block_size);
    if (!blkbuf) {
        printk("  [FAIL] %s: kmalloc failed\n", path);
        failed++;
        return;
    }

    if (block_cache_read(inode->i_block[0], blkbuf) != 0) {
        printk("  [FAIL] %s: block_cache_read(block=%u) failed\n",
               path, inode->i_block[0]);
        kfree(blkbuf);
        failed++;
        return;
    }

    /* Walk ext2_dirent_t records */
    int found_any = 0;
    uint32_t off = 0;
    while (off + 8 <= ext2_mnt->block_size) {
        ext2_dirent_t *de = (ext2_dirent_t *)(blkbuf + off);
        if (de->rec_len == 0) break;

        if (de->inode != 0) {
            /* Copy name: name field is NOT null-terminated in ext2 */
            char name[256];
            uint32_t nlen = de->name_len < 255 ? de->name_len : 255;
            memcpy(name, de->name, nlen);
            name[nlen] = '\0';

            /* Skip . and .. for the "found real entries" count */
            if (!(nlen == 1 && name[0] == '.') &&
                !(nlen == 2 && name[0] == '.' && name[1] == '.')) {
                found_any++;
            }
            printk("         dirent ino=%u reclen=%u type=%u name='%s'\n",
                   de->inode, de->rec_len, de->file_type, name);
        }

        off += de->rec_len;
    }

    kfree(blkbuf);

    if (found_any == 0) {
        printk("  [FAIL] %s: no real children found in on-disk dir block "
               "(files may not have been written to ext2)\n", path);
        failed++;
    } else {
        printk("  [PASS] %s: %d child(ren) found in raw ext2 dir block\n",
               path, found_any);
        passed++;
    }
}

void ext2_populate_test_run(void) {
    printk("\n");
    printk("========================================\n");
    printk("   ext2 Population Tests\n");
    printk("   (verifies tarfs -> ext2 copy)\n");
    printk("========================================\n");
    printk("\n");

    printk("-- directories --\n");
    check_dir("/bin");
    check_dir("/etc");
    printk("\n");

    printk("-- /bin/init --\n");
    check_file("/bin/init", 1024, 1);
    printk("\n");

    printk("-- /bin/sh --\n");
    check_file("/bin/sh", 1024, 1);
    printk("\n");

    printk("-- /bin/ls --\n");
    check_file("/bin/ls", 1024, 1);
    printk("\n");

    printk("-- /etc/rc (optional) --\n");
    dentry_t *rc = vfs_lookup("/etc/rc");
    if (rc && rc->d_vnode) {
        printk("  [INFO] /etc/rc present, size=%d\n", (int)rc->d_vnode->size);
        passed++;
    } else {
        printk("  [INFO] /etc/rc not present (skipped)\n");
    }
    printk("\n");

    /*
     * ------------------------------------------------------------------
     * RAW EXT2 DIRECTORY CHECK
     * Reads the on-disk directory block directly (no readdir op needed).
     * If this passes but the vfs_readdir check below fails, the data IS
     * on disk but ls can't see it because readdir is not implemented.
     * If this also fails, the populate step didn't write entries at all.
     * ------------------------------------------------------------------
     */
    printk("-- raw ext2 dir block checks (is data on disk?) --\n");
    check_raw_ext2_dir("/");
    check_raw_ext2_dir("/bin");
    printk("\n");

    /*
     * ------------------------------------------------------------------
     * VFS READDIR CHECK (the ls code path)
     * If this fails with "no readdir op" you need to implement and
     * register ext2_readdir in ext2_init().
     * If it returns 0 entries but raw check passed, the readdir impl
     * is wrong (e.g. reading wrong block, wrong offset arithmetic).
     * ------------------------------------------------------------------
     */
    printk("-- vfs_readdir checks (what ls sees) --\n");
    check_readdir_vfs("/");
    check_readdir_vfs("/bin");
    printk("\n");

    printk("-- mount checks --\n");
    mount_t *root_mnt = vfs_find_mount("/");
    if (!root_mnt) {
        printk("  [FAIL] root mount not found\n");
        failed++;
    } else if (strcmp(root_mnt->fs_type->name, "ext2") != 0) {
        printk("  [FAIL] root mount is '%s', expected 'ext2'\n",
               root_mnt->fs_type->name);
        failed++;
    } else {
        printk("  [PASS] root mount is ext2\n");
        passed++;
    }
    printk("\n");

    printk("========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");
    if (failed > 0)
        printk("WARN: %d test(s) FAILED — exec of /bin/init will likely fail\n", failed);
    else
        printk("INFO: all checks passed — /bin/init should be loadable\n");
    printk("\n");
}
