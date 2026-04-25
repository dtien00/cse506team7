#include "kernel/printk.h"
#include "kernel/types.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/dentry.h"
#include "process/process.h"
#include "tests/test.h"
#include "lib/string.h"
#include "errno.h"
#include "string.h"

static int passed = 0;
static int failed = 0;

static int contains_substring(const char *haystack, const char *needle) {
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen == 0) return 1;
    if (hlen < nlen) return 0;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (size_t j = 0; j < nlen; j++) {
            if (haystack[i + j] != needle[j]) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

void procfs_test_run(void) {
    printk("=== procfs kernel tests ===\n");

    struct proc *saved_current = current;
    current = proc_find(1);
    TEST("procfs kernel test: init process exists", current != NULL);

    mount_t *proc_mnt = vfs_find_mount("/proc");
    TEST("procfs mount exists at /proc",
         proc_mnt != NULL && proc_mnt->fs_type && strcmp(proc_mnt->fs_type->name, "procfs") == 0);

    dentry_t *self_dentry = NULL;
    int rc = vfs_lookup("/proc/self", &self_dentry);
    TEST("vfs_lookup(/proc/self) succeeds", rc == 0 && self_dentry != NULL);
    if (rc == 0 && self_dentry) {
        char buf[256];
        uint32_t entries = 0;
        int n = vfs_readdir(self_dentry, 0, buf, sizeof(buf), &entries);
        TEST("vfs_readdir(/proc/self) succeeds", n > 0 && entries > 0);
        if (n > 0 && entries > 0) {
            fs_dent_t *dent = (fs_dent_t *) buf;
            TEST("procfs /proc/self contains status file",
                 strcmp(dent->d_name, "status") == 0);
            TEST("procfs status inode type is regular file",
                 dent->d_type == DT_REG);
        }
        dentry_put(self_dentry);
    }

    int fd = file_open("/proc/self/status");
    TEST("file_open(/proc/self/status) succeeds", fd >= 0);
    if (fd >= 0) {
        char buf[256];
        int n = file_read(fd, buf, sizeof(buf) - 1);
        if (n > 0)
            buf[n] = '\0';
        TEST("file_read(/proc/self/status) returns non-zero bytes", n > 0);
        if (n > 0) {
            TEST("procfs status contains pid label", contains_substring(buf, "pid:"));
            TEST("procfs status contains name label", contains_substring(buf, "name:"));
            TEST("procfs status contains state label", contains_substring(buf, "state:"));
        }
        file_close(fd);
    }

    current = saved_current;
}
