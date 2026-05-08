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
            if (haystack[i + j] != needle[j]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

static int read_proc_file(const char *path, char *buf, int bufsize) {
    int fd = file_open(path);
    if (fd < 0) return fd;
    int n = file_read(fd, buf, bufsize - 1);
    if (n > 0) buf[n] = '\0';
    file_close(fd);
    return n;
}

void procfs_test_run(void) {
    printk("=== procfs kernel tests ===\n");

    struct proc *saved_current = current;
    current = proc_find(1);
    TEST("procfs: init process exists", current != NULL);

    // ── mount check ────────────────────────────────────────────────────────

    mount_t *proc_mnt = vfs_find_mount("/proc");
    TEST("procfs: mounted at /proc",
         proc_mnt != NULL && proc_mnt->fs_type &&
         strcmp(proc_mnt->fs_type->name, "procfs") == 0);

    // ── /proc/self lookup ──────────────────────────────────────────────────

    dentry_t *self_dentry = NULL;
    int rc = vfs_lookup("/proc/self", &self_dentry);
    TEST("vfs_lookup(/proc/self) succeeds", rc == 0 && self_dentry != NULL);
    if (rc == 0 && self_dentry) {
        TEST("procfs /proc/self vnode is symlink",
             self_dentry->d_vnode && self_dentry->d_vnode->type == VNODE_LNK);
        dentry_put(self_dentry);
    }

    // ── /proc/self readlink ────────────────────────────────────────────────

    char link_buf[64];
    ssize_t lr = vfs_readlink("/proc/self", link_buf, sizeof(link_buf));
    TEST("vfs_readlink(/proc/self) returns positive length", lr > 0);
    if (lr > 0) {
        // Should be a numeric string matching current->pid
        int val = 0;
        int valid = 1;
        for (int i = 0; i < (int)lr; i++) {
            if (link_buf[i] < '0' || link_buf[i] > '9') { valid = 0; break; }
            val = val * 10 + (link_buf[i] - '0');
        }
        TEST("vfs_readlink(/proc/self) returns numeric pid string", valid);
        TEST("vfs_readlink(/proc/self) pid matches current->pid",
             current && val == (int)current->pid);
    }

    // ── /proc/<pid> directory ──────────────────────────────────────────────

    if (current) {
        char pid_path[64];
        char nbuf[16];
        int n = 0;
        int v = (int)current->pid;
        do { nbuf[n++] = '0' + v % 10; v /= 10; } while (v);
        memcpy(pid_path, "/proc/", 6);
        for (int i = 0; i < n; i++) pid_path[6 + i] = nbuf[n - 1 - i];
        pid_path[6 + n] = '\0';

        dentry_t *pid_dentry = NULL;
        rc = vfs_lookup(pid_path, &pid_dentry);
        TEST("vfs_lookup(/proc/<pid>) succeeds", rc == 0 && pid_dentry != NULL);
        if (rc == 0 && pid_dentry) {
            char dbuf[512];
            uint32_t entries = 0;
            int dn = vfs_readdir(pid_dentry, 0, dbuf, sizeof(dbuf), &entries);
            TEST("vfs_readdir(/proc/<pid>) returns 5 entries", dn > 0 && entries == 5);

            // Verify expected names appear
            int has_status = 0, has_stat = 0, has_cwd = 0, has_exe = 0, has_root = 0;
            char *p2 = dbuf;
            for (uint32_t i = 0; i < entries; i++) {
                fs_dent_t *de = (fs_dent_t *)p2;
                if (strcmp(de->d_name, "status") == 0) has_status = 1;
                if (strcmp(de->d_name, "stat")   == 0) has_stat   = 1;
                if (strcmp(de->d_name, "cwd")    == 0) has_cwd    = 1;
                if (strcmp(de->d_name, "exe")    == 0) has_exe    = 1;
                if (strcmp(de->d_name, "root")   == 0) has_root   = 1;
                p2 += de->d_reclen;
            }
            TEST("procfs pid dir has status", has_status);
            TEST("procfs pid dir has stat",   has_stat);
            TEST("procfs pid dir has cwd",    has_cwd);
            TEST("procfs pid dir has exe",    has_exe);
            TEST("procfs pid dir has root",   has_root);
            dentry_put(pid_dentry);
        }
    }

    // ── /proc/self/status ─────────────────────────────────────────────────

    int fd = file_open("/proc/self/status");
    TEST("file_open(/proc/self/status) succeeds", fd >= 0);
    if (fd >= 0) {
        char buf[1024];
        int n = file_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) buf[n] = '\0';
        TEST("file_read(/proc/self/status) returns bytes", n > 0);
        if (n > 0) {
            TEST("status contains Name",        contains_substring(buf, "Name:"));
            TEST("status contains State",       contains_substring(buf, "State:"));
            TEST("status contains Pid",         contains_substring(buf, "Pid:"));
            TEST("status contains PPid",        contains_substring(buf, "PPid:"));
            TEST("status contains VmSize",      contains_substring(buf, "VmSize:"));
            TEST("status contains KernelStack", contains_substring(buf, "KernelStack:"));
            TEST("status contains SigPnd",      contains_substring(buf, "SigPnd:"));
            TEST("status contains Threads",     contains_substring(buf, "Threads:"));
        }
        file_close(fd);
    }

    // ── /proc/self/stat ────────────────────────────────────────────────────

    {
        char buf[512];
        int n = read_proc_file("/proc/self/stat", buf, sizeof(buf));
        TEST("file_read(/proc/self/stat) returns bytes", n > 0);
        if (n > 0) {
            TEST("stat line starts with digit", buf[0] >= '0' && buf[0] <= '9');
            TEST("stat line contains comm in parens", contains_substring(buf, "("));
        }
    }

    // ── /proc/meminfo ──────────────────────────────────────────────────────

    {
        char buf[512];
        int n = read_proc_file("/proc/meminfo", buf, sizeof(buf));
        TEST("file_read(/proc/meminfo) returns bytes", n > 0);
        if (n > 0) {
            TEST("meminfo contains MemTotal",     contains_substring(buf, "MemTotal:"));
            TEST("meminfo contains MemFree",      contains_substring(buf, "MemFree:"));
            TEST("meminfo contains MemAvailable", contains_substring(buf, "MemAvailable:"));
            TEST("meminfo contains SwapTotal",    contains_substring(buf, "SwapTotal:"));
        }
    }

    // ── /proc/self/cwd readlink ────────────────────────────────────────────

    {
        ssize_t r = vfs_readlink("/proc/self/cwd", link_buf, sizeof(link_buf));
        TEST("vfs_readlink(/proc/self/cwd) returns positive length", r > 0);
        if (r > 0)
            TEST("cwd readlink starts with /", link_buf[0] == '/');
    }

    // ── /proc/self/root readlink ───────────────────────────────────────────

    {
        ssize_t r = vfs_readlink("/proc/self/root", link_buf, sizeof(link_buf));
        TEST("vfs_readlink(/proc/self/root) returns positive length", r > 0);
        if (r > 0)
            TEST("root readlink starts with /", link_buf[0] == '/');
    }

    // ── /proc/<bad>/status → ENOENT ───────────────────────────────────────

    {
        int bad_fd = file_open("/proc/999999/status");
        TEST("open invalid /proc/<pid>/status returns error", bad_fd < 0);
        if (bad_fd >= 0) file_close(bad_fd);
    }

    // ── root readdir includes self and meminfo ─────────────────────────────

    {
        dentry_t *root_dentry = NULL;
        rc = vfs_lookup("/proc", &root_dentry);
        TEST("vfs_lookup(/proc) succeeds", rc == 0 && root_dentry != NULL);
        if (rc == 0 && root_dentry) {
            char dbuf[1024];
            uint32_t entries = 0;
            int dn = vfs_readdir(root_dentry, 0, dbuf, sizeof(dbuf), &entries);
            TEST("vfs_readdir(/proc) returns entries", dn > 0 && entries > 0);
            if (dn > 0 && entries > 0) {
                int has_self = 0, has_meminfo = 0;
                char *p2 = dbuf;
                for (uint32_t i = 0; i < entries; i++) {
                    fs_dent_t *de = (fs_dent_t *)p2;
                    if (strcmp(de->d_name, "self")    == 0) has_self    = 1;
                    if (strcmp(de->d_name, "meminfo") == 0) has_meminfo = 1;
                    p2 += de->d_reclen;
                }
                TEST("/proc readdir contains self",    has_self);
                TEST("/proc readdir contains meminfo", has_meminfo);
            }
            dentry_put(root_dentry);
        }
    }

    current = saved_current;
}
