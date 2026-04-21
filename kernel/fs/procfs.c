#include "fs/procfs.h"

#include "errno.h"
#include "fs/vfs.h"
#include "kernel/printk.h"
#include "kernel/types.h"
#include "lib/string.h"
#include "mm/kmalloc.h"
#include "process/process.h"

#define PROCFS_MAX_NAME 32
#define PROCFS_STATUS_BUFSIZE 256

typedef enum {
    PROCFS_NODE_ROOT,
    PROCFS_NODE_PID_DIR,
    PROCFS_NODE_STATUS_FILE,
} procfs_node_type_t;

typedef struct procfs_node {
    procfs_node_type_t type;
    int pid; // valid for PID_DIR and STATUS_FILE
} procfs_node_t;

static vnode_t *procfs_root = NULL;

static const char *proc_state_name(proc_state_t state) {
    switch (state) {
        case PROC_UNUSED: return "unused";
        case PROC_CREATED: return "created";
        case PROC_READY: return "ready";
        case PROC_RUNNING: return "running";
        case PROC_BLOCKED: return "blocked";
        case PROC_ZOMBIE: return "zombie";
        case PROC_STOPPED: return "stopped";
        default: return "unknown";
    }
}

static int procfs_itoa(int value, char *buf, int bufsize) {
    if (!buf || bufsize <= 0)
        return 0;

    if (value == 0) {
        if (bufsize < 2) return 0;
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    char temp[16];
    int pos = 0;
    int negative = 0;
    long v = value;
    if (v < 0) {
        negative = 1;
        v = -v;
    }
    while (v > 0 && pos < (int)sizeof(temp) - 1) {
        temp[pos++] = '0' + (v % 10);
        v /= 10;
    }
    if (negative) {
        temp[pos++] = '-';
    }
    if (pos + 1 > bufsize)
        return 0;
    for (int i = 0; i < pos; i++) {
        buf[i] = temp[pos - 1 - i];
    }
    buf[pos] = '\0';
    return pos;
}

static int procfs_write_status(char *out, int out_size, struct proc *p) {
    if (!out || out_size <= 0 || !p)
        return 0;

    int written = 0;
    char numbuf[32];
    int n;
    char line[128];

    n = procfs_itoa(p->pid, numbuf, sizeof(numbuf));
    if (n <= 0) return 0;
    int line_len = 5 + n + 1;
    if (line_len > (int)sizeof(line))
        line_len = (int)sizeof(line);
    memcpy(line, "pid: ", 5);
    memcpy(line + 5, numbuf, n);
    line[5 + n] = '\n';
    n = (written + line_len <= out_size) ? line_len : out_size - written;
    if (n > 0) {
        memcpy(out + written, line, n);
        written += n;
    }

    n = procfs_itoa(p->parent ? p->parent->pid : 0, numbuf, sizeof(numbuf));
    line_len = 6 + n + 1;
    memcpy(line, "ppid: ", 6);
    memcpy(line + 6, numbuf, n);
    line[6 + n] = '\n';
    if (written < out_size) {
        int copy = (written + line_len <= out_size) ? line_len : out_size - written;
        memcpy(out + written, line, copy);
        written += copy;
    }

    int namelen = (int) strlen(p->name);
    line_len = 6 + namelen + 1;
    if (written < out_size) {
        memcpy(line, "name: ", 6);
        memcpy(line + 6, p->name, namelen);
        line[6 + namelen] = '\n';
        int copy = (written + line_len <= out_size) ? line_len : out_size - written;
        if (copy > 0) {
            memcpy(out + written, line, copy);
            written += copy;
        }
    }

    const char *state = proc_state_name(p->state);
    int statelen = (int) strlen(state);
    line_len = 7 + statelen + 1;
    if (written < out_size) {
        memcpy(line, "state: ", 7);
        memcpy(line + 7, state, statelen);
        line[7 + statelen] = '\n';
        int copy = (written + line_len <= out_size) ? line_len : out_size - written;
        if (copy > 0) {
            memcpy(out + written, line, copy);
            written += copy;
        }
    }

    n = procfs_itoa(p->priority, numbuf, sizeof(numbuf));
    line_len = 10 + n + 1;
    if (written < out_size) {
        memcpy(line, "priority: ", 10);
        memcpy(line + 10, numbuf, n);
        line[10 + n] = '\n';
        int copy = (written + line_len <= out_size) ? line_len : out_size - written;
        if (copy > 0) {
            memcpy(out + written, line, copy);
            written += copy;
        }
    }

    n = procfs_itoa((int) p->utime_ticks, numbuf, sizeof(numbuf));
    line_len = 11 + n + 1;
    if (written < out_size) {
        memcpy(line, "cpu_ticks: ", 11);
        memcpy(line + 11, numbuf, n);
        line[11 + n] = '\n';
        int copy = (written + line_len <= out_size) ? line_len : out_size - written;
        if (copy > 0) {
            memcpy(out + written, line, copy);
            written += copy;
        }
    }

    return written;
}

static vnode_t *procfs_alloc_vnode(procfs_node_type_t type, int pid, mount_t *mnt) {
    vnode_t *vnode = kzalloc(sizeof(vnode_t));
    if (!vnode)
        return NULL;
    vnode->type = (type == PROCFS_NODE_STATUS_FILE) ? VNODE_REG : VNODE_DIR;
    vnode->mode = (type == PROCFS_NODE_STATUS_FILE) ? 0444 : 0555;
    vnode->size = 0;
    vnode->nlink = (type == PROCFS_NODE_STATUS_FILE) ? 1 : 2;
    vnode->ref_count = 0;
    vnode->mount = mnt;
    vnode->fs_type = mnt ? mnt->fs_type : NULL;

    procfs_node_t *node = kzalloc(sizeof(procfs_node_t));
    if (!node) {
        kfree(vnode);
        return NULL;
    }
    node->type = type;
    node->pid = pid;
    vnode->fs_data = node;

    return vnode;
}

static struct proc *procfs_lookup_process(const char *name) {
    if (!name)
        return NULL;

    if (strcmp(name, "self") == 0) {
        if (!current)
            return NULL;
        return current;
    }

    int pid = 0;
    for (const char *c = name; *c; c++) {
        if (*c < '0' || *c > '9')
            return NULL;
        pid = pid * 10 + (*c - '0');
    }
    return proc_find((pid_t)pid);
}

static int procfs_parse_path(const char *path, char comps[3][PROCFS_MAX_NAME], int *count) {
    if (!path || !count)
        return -1;

    *count = 0;
    while (*path == '/')
        path++;
    if (*path == '\0')
        return 0;

    const char *start = path;
    size_t len = 0;
    while (1) {
        if (*path == '/' || *path == '\0') {
            if (len > 0) {
                if (len >= PROCFS_MAX_NAME)
                    return -1;
                memcpy(comps[*count], start, len);
                comps[*count][len] = '\0';
                (*count)++;
                if (*count >= 3)
                    return -1;
            }
            if (*path == '\0')
                break;
            path++;
            while (*path == '/')
                path++;
            if (*path == '\0')
                break;
            start = path;
            len = 0;
            continue;
        }
        len++;
        path++;
    }
    return 0;
}

static vnode_t *procfs_lookup(mount_t *mnt, const char *path) {
    if (!mnt || !path)
        return NULL;

    if (!procfs_root)
        return NULL;

    if (path[0] == '/')
        path++;
    if (*path == '\0')
        return procfs_root;

    char comps[3][PROCFS_MAX_NAME];
    int count = 0;
    if (procfs_parse_path(path, comps, &count) < 0)
        return NULL;

    if (count == 1) {
        struct proc *p = procfs_lookup_process(comps[0]);
        if (!p)
            return NULL;
        return procfs_alloc_vnode(PROCFS_NODE_PID_DIR, p->pid, mnt);
    }

    if (count == 2) {
        struct proc *p = procfs_lookup_process(comps[0]);
        if (!p)
            return NULL;
        if (strcmp(comps[1], "status") != 0)
            return NULL;
        return procfs_alloc_vnode(PROCFS_NODE_STATUS_FILE, p->pid, mnt);
    }

    return NULL;
}

static int procfs_read(vnode_t *vnode, void *buf, size_t offset, size_t len) {
    if (!vnode || !buf)
        return -EINVAL;

    procfs_node_t *node = (procfs_node_t *) vnode->fs_data;
    if (!node || node->type != PROCFS_NODE_STATUS_FILE)
        return -EISDIR;

    struct proc *p = proc_find((pid_t) node->pid);
    if (!p)
        return -ENOENT;

    char temp[PROCFS_STATUS_BUFSIZE];
    int total = procfs_write_status(temp, sizeof(temp), p);
    if (total <= 0)
        return 0;
    if ((size_t)offset >= (size_t) total)
        return 0;
    size_t to_copy = len;
    if (offset + to_copy > (size_t) total)
        to_copy = (size_t) total - offset;
    memcpy(buf, temp + offset, to_copy);
    return (int) to_copy;
}

static int procfs_readdir(mount_t *mnt, vnode_t *vnode, uint32_t start_idx, void *buf,
                          uint32_t bufsize, uint32_t *entries_out) {
    if (!mnt || !vnode || !buf || !entries_out)
        return -EINVAL;

    procfs_node_t *node = (procfs_node_t *) vnode->fs_data;
    if (node && node->type != PROCFS_NODE_ROOT && node->type != PROCFS_NODE_PID_DIR)
        return -ENOTDIR;

    char *out = (char *) buf;
    uint32_t written = 0;
    uint32_t found = 0;
    uint32_t idx = 0;

    if (!node || node->type == PROCFS_NODE_ROOT) {
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *p = &proctab[i];
            if (p->state == PROC_UNUSED)
                continue;
            if (idx++ < start_idx)
                continue;

            char name[PROCFS_MAX_NAME];
            int namelen = procfs_itoa(p->pid, name, sizeof(name));
            if (namelen <= 0)
                continue;
            uint16_t reclen = FS_DENT_RECLEN(namelen);
            if (written + reclen > bufsize)
                break;
            fs_dent_t *dent = (fs_dent_t *) (out + written);
            dent->d_ino = (ino_t) p->pid;
            dent->d_reclen = reclen;
            dent->d_type = DT_DIR;
            memcpy(dent->d_name, name, namelen + 1);
            written += reclen;
            found++;
        }
    } else {
        // PID directory only contains status file
        if (start_idx == 0) {
            const char *name = "status";
            uint16_t reclen = FS_DENT_RECLEN(strlen(name));
            if (written + reclen <= bufsize) {
                fs_dent_t *dent = (fs_dent_t *) (out + written);
                dent->d_ino = (ino_t) node->pid;
                dent->d_reclen = reclen;
                dent->d_type = DT_REG;
                memcpy(dent->d_name, name, strlen(name) + 1);
                written += reclen;
                found = 1;
            }
        }
    }

    *entries_out = found;
    return (int) written;
}

static int procfs_unmount(mount_t *mnt) {
    if (!mnt)
        return -EINVAL;
    if (procfs_root) {
        kfree(procfs_root);
        procfs_root = NULL;
    }
    return 0;
}

static vnode_t *procfs_mount(mount_t *mnt, const char *source) {
    (void) source;
    if (!mnt)
        return NULL;

    vnode_t *root = kzalloc(sizeof(vnode_t));
    if (!root)
        return NULL;

    root->type = VNODE_DIR;
    root->mode = 0555;
    root->size = 0;
    root->nlink = 2;
    root->ref_count = 0;
    root->mount = mnt;
    root->fs_data = NULL;
    root->fs_type = mnt->fs_type;

    procfs_root = root;
    return root;
}

static fs_type_t procfs_type = {
    .name = "procfs",
    .ops = {
        .mount = procfs_mount,
        .unmount = procfs_unmount,
        .lookup = procfs_lookup,
        .read = procfs_read,
        .readdir = procfs_readdir,
    },
    .next = NULL,
};

void procfs_init(void) {
    vfs_register_fs(&procfs_type);
    printk("procfs: registered\n");
}
