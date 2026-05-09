#include "fs/procfs.h"

#include "errno.h"
#include "fs/vfs.h"
#include "kernel/printk.h"
#include "kernel/types.h"
#include "kernel/limits.h"
#include "lib/string.h"
#include "mm/kmalloc.h"
#include "mm/pmm.h"
#include "mm/vma.h"
#include "process/process.h"

#define PROCFS_MAX_NAME      32
#define PROCFS_STATUS_BUFSIZE 1024
#define PROCFS_STAT_BUFSIZE   512
#define PROCFS_MEMINFO_BUFSIZE 256

typedef enum {
    PROCFS_NODE_ROOT,
    PROCFS_NODE_PID_DIR,
    PROCFS_NODE_STATUS_FILE,
    PROCFS_NODE_STAT_FILE,
    PROCFS_NODE_CWD_LINK,
    PROCFS_NODE_EXE_LINK,
    PROCFS_NODE_ROOT_LINK,
    PROCFS_NODE_SELF_LINK,
    PROCFS_NODE_MEMINFO_FILE,
    PROCFS_NODE_SMAPS_FILE,
} procfs_node_type_t;

typedef struct procfs_node {
    procfs_node_type_t type;
    int pid; // valid for PID_DIR and per-pid files/links
} procfs_node_t;

static vnode_t *procfs_root = NULL;

// ── number formatters ────────────────────────────────────────────────────────

static int procfs_itoa(int value, char *buf, int bufsize) {
    if (!buf || bufsize <= 0)
        return 0;
    if (value == 0) {
        if (bufsize < 2) return 0;
        buf[0] = '0'; buf[1] = '\0';
        return 1;
    }
    char temp[24];
    int pos = 0;
    int negative = 0;
    long v = value;
    if (v < 0) { negative = 1; v = -v; }
    while (v > 0 && pos < (int)sizeof(temp) - 1) {
        temp[pos++] = '0' + (int)(v % 10);
        v /= 10;
    }
    if (negative) temp[pos++] = '-';
    if (pos + 1 > bufsize) return 0;
    for (int i = 0; i < pos; i++)
        buf[i] = temp[pos - 1 - i];
    buf[pos] = '\0';
    return pos;
}

static int procfs_utoa(uint64_t value, char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return 0;
    if (value == 0) {
        if (bufsize < 2) return 0;
        buf[0] = '0'; buf[1] = '\0';
        return 1;
    }
    char temp[24];
    int pos = 0;
    uint64_t v = value;
    while (v > 0 && pos < (int)sizeof(temp) - 1) {
        temp[pos++] = '0' + (int)(v % 10);
        v /= 10;
    }
    if (pos + 1 > bufsize) return 0;
    for (int i = 0; i < pos; i++)
        buf[i] = temp[pos - 1 - i];
    buf[pos] = '\0';
    return pos;
}

// Write "<val> kB" into buf, return chars written (not including NUL)
static int procfs_utoa_kb(uint64_t bytes, char *buf, int bufsize) {
    uint64_t kb = bytes / 1024;
    int n = procfs_utoa(kb, buf, bufsize - 4);
    if (n <= 0) return 0;
    if (n + 4 >= bufsize) return n;
    buf[n]   = ' '; buf[n+1] = 'k'; buf[n+2] = 'B'; buf[n+3] = '\0';
    return n + 3;
}

static int procfs_hex64(uint64_t v, char *buf, int bufsize) {
    if (bufsize < 17) return 0;
    static const char hex[] = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[v & 0xf];
        v >>= 4;
    }
    buf[16] = '\0';
    return 16;
}

// ── state helpers ────────────────────────────────────────────────────────────

static const char *proc_state_linux(proc_state_t state) {
    switch (state) {
        case PROC_RUNNING:  return "R (running)";
        case PROC_CREATED:  return "R (running)";
        case PROC_READY:    return "R (running)";
        case PROC_BLOCKED:  return "S (sleeping)";
        case PROC_STOPPED:  return "T (stopped)";
        case PROC_ZOMBIE:   return "Z (zombie)";
        default:            return "S (sleeping)";
    }
}

static char proc_state_char(proc_state_t state) {
    switch (state) {
        case PROC_RUNNING:
        case PROC_CREATED:
        case PROC_READY:   return 'R';
        case PROC_BLOCKED: return 'S';
        case PROC_STOPPED: return 'T';
        case PROC_ZOMBIE:  return 'Z';
        default:           return 'S';
    }
}

// ── VMA stats ────────────────────────────────────────────────────────────────

static void procfs_vma_stats(struct proc *p,
                              uint64_t *total_b,
                              uint64_t *stk_b,
                              uint64_t *data_b,
                              uint64_t *exe_b) {
    *total_b = *stk_b = *data_b = *exe_b = 0;
    if (!p) return;
    for (struct vm_area *v = p->vma_list; v; v = v->next) {
        if (v->type == VMA_TYPE_DEVICE) continue;
        uint64_t sz = v->end - v->start;
        *total_b += sz;
        if (v->type & VMA_TYPE_STACK)
            *stk_b += sz;
        else if ((v->type & VMA_TYPE_HEAP) || (v->type & VMA_TYPE_ANON))
            *data_b += sz;
        if ((v->type & VMA_TYPE_FILE) && (v->prot & VMA_PROT_EXEC))
            *exe_b += sz;
    }
}

// ── append helper ────────────────────────────────────────────────────────────

static int buf_append(char *out, int out_size, int written, const char *s, int len) {
    if (written >= out_size) return written;
    int copy = len;
    if (written + copy > out_size) copy = out_size - written;
    memcpy(out + written, s, copy);
    return written + copy;
}

// ── status writer ────────────────────────────────────────────────────────────

static int procfs_write_status(char *out, int out_size, struct proc *p) {
    if (!out || out_size <= 0 || !p) return 0;

    char numbuf[32];
    char line[128];
    int w = 0;
    int n;

#define EMIT(label, val_str, val_len) do { \
    int ll = (int)strlen(label); \
    int vl = (val_len); \
    if (ll + 2 + vl + 1 < (int)sizeof(line)) { \
        memcpy(line, label, ll); \
        line[ll] = ':'; \
        line[ll + 1] = '\t'; \
        memcpy(line + ll + 2, (val_str), vl); \
        line[ll + 2 + vl] = '\n'; \
        w = buf_append(out, out_size, w, line, ll + 2 + vl + 1); \
    } \
} while (0)

    EMIT("Name",  p->name,                   (int)strlen(p->name));
    const char *st = proc_state_linux(p->state);
    EMIT("State", st,                         (int)strlen(st));

    n = procfs_itoa(p->pid, numbuf, sizeof(numbuf));
    EMIT("Tgid",  numbuf, n);

    n = procfs_itoa(p->pid, numbuf, sizeof(numbuf));
    EMIT("Pid",   numbuf, n);

    n = procfs_itoa(p->parent ? p->parent->pid : 0, numbuf, sizeof(numbuf));
    EMIT("PPid",  numbuf, n);

    n = procfs_itoa((int)p->pgid, numbuf, sizeof(numbuf));
    EMIT("Pgrp",  numbuf, n);

    n = procfs_itoa((int)p->pgid, numbuf, sizeof(numbuf));
    EMIT("Pgid",  numbuf, n);

    n = procfs_itoa((int)p->sid, numbuf, sizeof(numbuf));
    EMIT("Sid",   numbuf, n);

    uint64_t total_b, stk_b, data_b, exe_b;
    procfs_vma_stats(p, &total_b, &stk_b, &data_b, &exe_b);

    n = procfs_utoa_kb(total_b, numbuf, sizeof(numbuf));
    EMIT("VmSize", numbuf, n);

    n = procfs_utoa_kb(stk_b, numbuf, sizeof(numbuf));
    EMIT("VmStk",  numbuf, n);

    n = procfs_utoa_kb(data_b, numbuf, sizeof(numbuf));
    EMIT("VmData", numbuf, n);

    n = procfs_utoa_kb(exe_b, numbuf, sizeof(numbuf));
    EMIT("VmExe",  numbuf, n);

    n = procfs_utoa_kb(KSTACK_SIZE, numbuf, sizeof(numbuf));
    EMIT("KernelStack", numbuf, n);

    EMIT("Threads", "1", 1);

    n = procfs_hex64((uint64_t)p->pending_signals, numbuf, sizeof(numbuf));
    EMIT("SigPnd", numbuf, n);

    n = procfs_hex64((uint64_t)p->signal_mask, numbuf, sizeof(numbuf));
    EMIT("SigBlk", numbuf, n);

#undef EMIT

    return w;
}

// ── stat writer ──────────────────────────────────────────────────────────────

static int procfs_write_stat(char *out, int out_size, struct proc *p) {
    if (!out || out_size <= 0 || !p) return 0;

    uint64_t total_b, stk_b, data_b, exe_b;
    procfs_vma_stats(p, &total_b, &stk_b, &data_b, &exe_b);

    // Trim comm to 15 chars to match Linux (stored in parens)
    char comm[18];
    comm[0] = '(';
    int nl = (int)strlen(p->name);
    if (nl > 15) nl = 15;
    memcpy(comm + 1, p->name, nl);
    comm[nl + 1] = ')';
    comm[nl + 2] = '\0';

    char sc = proc_state_char(p->state);
    int ppid = p->parent ? (int)p->parent->pid : 0;
    uint64_t vsize = total_b;
    uint64_t rss   = total_b / 4096; // pages

    int w = 0;
    int max = out_size;

#define APPD(val) do { \
    char _nb[32]; int _n = procfs_itoa((val), _nb, sizeof(_nb)); \
    if (_n > 0 && w + _n + 1 < max) { \
        memcpy(out + w, _nb, _n); w += _n; \
        out[w++] = ' '; \
    } \
} while (0)

#define APPU(val) do { \
    char _nb[32]; int _n = procfs_utoa((val), _nb, sizeof(_nb)); \
    if (_n > 0 && w + _n + 1 < max) { \
        memcpy(out + w, _nb, _n); w += _n; \
        out[w++] = ' '; \
    } \
} while (0)

    APPD((int)p->pid);
    {
        int cl = (int)strlen(comm);
        if (w + cl + 1 < max) {
            memcpy(out + w, comm, cl); w += cl;
            out[w++] = ' ';
        }
    }
    if (w + 2 < max) { out[w++] = sc; out[w++] = ' '; }
    APPD(ppid);
    APPD((int)p->pgid);
    APPD((int)p->sid);
    for (int i = 0; i < 7; i++) APPD(0);
    APPU(p->utime_ticks);
    APPD(0); APPD(0);
    APPD(p->priority);
    APPD(0);
    APPD(1);
    APPD(0);
    APPD(0);
    APPU(vsize);
    APPU(rss);
    if (w > 0 && out[w-1] == ' ') out[w-1] = '\n';
    else if (w < max) out[w++] = '\n';

#undef APPD
#undef APPU

    return w;
}

// ── smaps writer ─────────────────────────────────────────────────────────────

static int procfs_write_smaps(char *out, int out_size, struct proc *p) {
    if (!out || out_size <= 0 || !p) return 0;

    uint64_t total_b, stk_b, data_b, exe_b;
    procfs_vma_stats(p, &total_b, &stk_b, &data_b, &exe_b);

    int w = 0;
    int max = out_size;

#define SMAPS_LINE(label, bytes) do { \
    char _nb[32]; \
    int _ll = (int)strlen(label); \
    int _nl = procfs_utoa_kb((bytes), _nb, sizeof(_nb)); \
    if (_nl > 0 && w + _ll + _nl + 5 < max) { \
        memcpy(out + w, label, _ll); w += _ll; \
        out[w++] = ':'; out[w++] = '\t'; \
        memcpy(out + w, _nb, _nl); w += _nl; \
        out[w++] = '\n'; \
    } \
} while (0)

    SMAPS_LINE("VmSize",      total_b);
    SMAPS_LINE("VmStack",     stk_b);
    SMAPS_LINE("VmData",      data_b);
    SMAPS_LINE("VmExe",       exe_b);
    SMAPS_LINE("KernelStack", (uint64_t)KSTACK_SIZE);

#undef SMAPS_LINE

    return w;
}

// ── meminfo writer ───────────────────────────────────────────────────────────

static int procfs_write_meminfo(char *out, int out_size) {
    if (!out || out_size <= 0) return 0;

    uint64_t total_kb = pmm_total_memory() / 1024;
    uint64_t free_kb  = pmm_free_memory()  / 1024;

    char tmp[PROCFS_MEMINFO_BUFSIZE];
    int w = 0;

#define MEM_LINE(label, kb) do { \
    char _nb[32]; \
    int _ll = (int)strlen(label); \
    int _nl = procfs_utoa((kb), _nb, sizeof(_nb)); \
    if (_nl > 0 && w + _ll + _nl + 10 < (int)sizeof(tmp)) { \
        memcpy(tmp + w, label, _ll); w += _ll; \
        tmp[w++] = ':'; tmp[w++] = '\t'; \
        memcpy(tmp + w, _nb, _nl); w += _nl; \
        tmp[w++] = ' '; tmp[w++] = 'k'; tmp[w++] = 'B'; tmp[w++] = '\n'; \
    } \
} while (0)

    MEM_LINE("MemTotal",     total_kb);
    MEM_LINE("MemFree",      free_kb);
    MEM_LINE("MemAvailable", free_kb);
    MEM_LINE("Buffers",      (uint64_t)0);
    MEM_LINE("Cached",       (uint64_t)0);
    MEM_LINE("SwapTotal",    (uint64_t)0);
    MEM_LINE("SwapFree",     (uint64_t)0);

#undef MEM_LINE

    int copy = w;
    if (copy > out_size) copy = out_size;
    memcpy(out, tmp, copy);
    return copy;
}

// ── vnode allocation ─────────────────────────────────────────────────────────

static vnode_t *procfs_alloc_vnode(procfs_node_type_t type, int pid, mount_t *mnt) {
    vnode_t *vn = kzalloc(sizeof(vnode_t));
    if (!vn) return NULL;

    switch (type) {
        case PROCFS_NODE_STATUS_FILE:
        case PROCFS_NODE_STAT_FILE:
        case PROCFS_NODE_MEMINFO_FILE:
        case PROCFS_NODE_SMAPS_FILE:
            vn->type  = VNODE_REG; vn->mode = 0444; vn->nlink = 1; break;
        case PROCFS_NODE_CWD_LINK:
            vn->type  = VNODE_REG; vn->mode = 0444; vn->nlink = 1; break;
        case PROCFS_NODE_EXE_LINK:
        case PROCFS_NODE_ROOT_LINK:
        case PROCFS_NODE_SELF_LINK:
            vn->type  = VNODE_LNK; vn->mode = 0777; vn->nlink = 1; break;
        default:
            vn->type  = VNODE_DIR; vn->mode = 0555; vn->nlink = 2; break;
    }
    vn->size       = 0;
    vn->ref_count  = 0;
    vn->mount      = mnt;
    vn->fs_type    = mnt ? mnt->fs_type : NULL;

    procfs_node_t *node = kzalloc(sizeof(procfs_node_t));
    if (!node) { kfree(vn); return NULL; }
    node->type = type;
    node->pid  = pid;
    vn->fs_data = node;
    return vn;
}

// ── path helpers ─────────────────────────────────────────────────────────────

static int procfs_parse_path(const char *path, char comps[3][PROCFS_MAX_NAME], int *count) {
    if (!path || !count) return -1;
    *count = 0;
    while (*path == '/') path++;
    if (*path == '\0') return 0;

    const char *start = path;
    size_t len = 0;
    while (1) {
        if (*path == '/' || *path == '\0') {
            if (len > 0) {
                if (len >= PROCFS_MAX_NAME || *count >= 3) return -1;
                memcpy(comps[*count], start, len);
                comps[(*count)++][len] = '\0';
            }
            if (*path == '\0') break;
            path++;
            while (*path == '/') path++;
            if (*path == '\0') break;
            start = path; len = 0;
            continue;
        }
        len++; path++;
    }
    return 0;
}

static struct proc *procfs_pid_from_name(const char *name) {
    if (!name) return NULL;
    int pid = 0;
    for (const char *c = name; *c; c++) {
        if (*c < '0' || *c > '9') return NULL;
        pid = pid * 10 + (*c - '0');
    }
    return proc_find((pid_t)pid);
}

// ── lookup ───────────────────────────────────────────────────────────────────

static vnode_t *procfs_lookup(mount_t *mnt, const char *path) {
    if (!mnt || !path || !procfs_root) return NULL;

    while (*path == '/') path++;
    if (*path == '\0') return procfs_root;

    char comps[3][PROCFS_MAX_NAME];
    int count = 0;
    if (procfs_parse_path(path, comps, &count) < 0) return NULL;

    if (count == 1) {
        if (strcmp(comps[0], "self") == 0)
            return procfs_alloc_vnode(PROCFS_NODE_SELF_LINK, 0, mnt);
        if (strcmp(comps[0], "meminfo") == 0)
            return procfs_alloc_vnode(PROCFS_NODE_MEMINFO_FILE, 0, mnt);
        struct proc *p = procfs_pid_from_name(comps[0]);
        if (!p) return NULL;
        return procfs_alloc_vnode(PROCFS_NODE_PID_DIR, p->pid, mnt);
    }

    if (count == 2) {
        // self/<file> resolves via current
        struct proc *p = NULL;
        if (strcmp(comps[0], "self") == 0) {
            p = current;
        } else {
            p = procfs_pid_from_name(comps[0]);
        }
        if (!p) return NULL;

        const char *name = comps[1];
        if (strcmp(name, "status") == 0)
            return procfs_alloc_vnode(PROCFS_NODE_STATUS_FILE, p->pid, mnt);
        if (strcmp(name, "stat") == 0)
            return procfs_alloc_vnode(PROCFS_NODE_STAT_FILE, p->pid, mnt);
        if (strcmp(name, "cwd") == 0)
            return procfs_alloc_vnode(PROCFS_NODE_CWD_LINK, p->pid, mnt);
        if (strcmp(name, "exe") == 0)
            return procfs_alloc_vnode(PROCFS_NODE_EXE_LINK, p->pid, mnt);
        if (strcmp(name, "root") == 0)
            return procfs_alloc_vnode(PROCFS_NODE_ROOT_LINK, p->pid, mnt);
        if (strcmp(name, "smaps") == 0)
            return procfs_alloc_vnode(PROCFS_NODE_SMAPS_FILE, p->pid, mnt);
        return NULL;
    }

    return NULL;
}

// ── read ─────────────────────────────────────────────────────────────────────

static int procfs_read(vnode_t *vnode, void *buf, size_t offset, size_t len) {
    if (!vnode || !buf) return -EINVAL;

    procfs_node_t *node = (procfs_node_t *)vnode->fs_data;
    if (!node) return -EINVAL;

    char *temp = (char *)kmalloc(PROCFS_STATUS_BUFSIZE);
    if (!temp) return -ENOMEM;
    int total = 0;
    int rc = 0;

    switch (node->type) {
        case PROCFS_NODE_STATUS_FILE: {
            struct proc *p = proc_find((pid_t)node->pid);
            if (!p) { rc = -ENOENT; goto out; }
            total = procfs_write_status(temp, PROCFS_STATUS_BUFSIZE, p);
            break;
        }
        case PROCFS_NODE_STAT_FILE: {
            struct proc *p = proc_find((pid_t)node->pid);
            if (!p) { rc = -ENOENT; goto out; }
            total = procfs_write_stat(temp, PROCFS_STATUS_BUFSIZE, p);
            break;
        }
        case PROCFS_NODE_MEMINFO_FILE:
            total = procfs_write_meminfo(temp, PROCFS_STATUS_BUFSIZE);
            break;
        case PROCFS_NODE_SMAPS_FILE: {
            struct proc *p = proc_find((pid_t)node->pid);
            if (!p) { rc = -ENOENT; goto out; }
            total = procfs_write_smaps(temp, PROCFS_STATUS_BUFSIZE, p);
            break;
        }
        case PROCFS_NODE_CWD_LINK: {
            struct proc *p = proc_find((pid_t)node->pid);
            if (!p) { rc = -ENOENT; goto out; }
            int r = fs_struct_get_cwd(p->fs, temp, PROCFS_STATUS_BUFSIZE);
            if (r < 0) { rc = r; goto out; }
            total = (int)strlen(temp);
            break;
        }
        case PROCFS_NODE_EXE_LINK: {
            struct proc *p = proc_find((pid_t)node->pid);
            if (!p) { rc = -ENOENT; goto out; }
            if (p->exe_path[0] == '\0') { rc = 0; goto out; }
            total = (int)strlen(p->exe_path);
            if (total >= PROCFS_STATUS_BUFSIZE) total = PROCFS_STATUS_BUFSIZE - 1;
            memcpy(temp, p->exe_path, total);
            temp[total] = '\0';
            break;
        }
        case PROCFS_NODE_ROOT_LINK:
        case PROCFS_NODE_SELF_LINK:
            rc = -EINVAL; goto out; // must use readlink
        case PROCFS_NODE_PID_DIR:
        case PROCFS_NODE_ROOT:
            rc = -EISDIR; goto out;
        default:
            rc = -EINVAL; goto out;
    }

    if (total <= 0) { rc = 0; goto out; }
    if (offset >= (size_t)total) { rc = 0; goto out; }
    size_t to_copy = len;
    if (offset + to_copy > (size_t)total) to_copy = (size_t)total - offset;
    memcpy(buf, temp + offset, to_copy);
    rc = (int)to_copy;
out:
    kfree(temp);
    return rc;
}

// ── readlink ─────────────────────────────────────────────────────────────────

static int procfs_readlink(vnode_t *vn, char *buf, size_t size) {
    if (!vn || !buf || size == 0) return -EINVAL;

    procfs_node_t *node = (procfs_node_t *)vn->fs_data;
    if (!node) return -EINVAL;

    switch (node->type) {
        case PROCFS_NODE_SELF_LINK: {
            if (!current) return -ESRCH;
            char pid_str[24];
            int pn = procfs_itoa((int)current->pid, pid_str, sizeof(pid_str));
            if (pn <= 0) return -EINVAL;
            if ((size_t)(pn + 1) > size) return -ENAMETOOLONG;
            memcpy(buf, pid_str, pn + 1);
            return pn;
        }
        case PROCFS_NODE_CWD_LINK: {
            struct proc *p = proc_find((pid_t)node->pid);
            if (!p) return -ENOENT;
            int r = fs_struct_get_cwd(p->fs, buf, size);
            if (r < 0) return r;
            return (int)strlen(buf);
        }
        case PROCFS_NODE_ROOT_LINK: {
            struct proc *p = proc_find((pid_t)node->pid);
            if (!p) return -ENOENT;
            int r = fs_struct_get_root(p->fs, buf, size);
            if (r < 0) return r;
            return (int)strlen(buf);
        }
        case PROCFS_NODE_EXE_LINK: {
            struct proc *p = proc_find((pid_t)node->pid);
            if (!p) return -ENOENT;
            if (p->exe_path[0] == '\0') {
                buf[0] = '\0';
                return 0;
            }
            size_t n = strlen(p->exe_path);
            size_t copy = n < size ? n : size - 1;
            memcpy(buf, p->exe_path, copy);
            buf[copy] = '\0';
            return (int)copy;
        }
        default:
            return -EINVAL;
    }
}

// ── readdir ──────────────────────────────────────────────────────────────────

// PID directory entries (order matches readdir index 0-5)
static const struct { const char *name; uint8_t dtype; } pid_dir_entries[] = {
    { "status", DT_REG },
    { "stat",   DT_REG },
    { "smaps",  DT_REG },
    { "cwd",    DT_REG },
    { "exe",    DT_LNK },
    { "root",   DT_LNK },
};
#define PID_DIR_NENTRIES 6

static int procfs_readdir(mount_t *mnt, vnode_t *vnode, uint32_t start_idx, void *buf,
                          uint32_t bufsize, uint32_t *entries_out) {
    if (!mnt || !vnode || !buf || !entries_out) return -EINVAL;

    procfs_node_t *node = (procfs_node_t *)vnode->fs_data;
    if (node && node->type != PROCFS_NODE_ROOT && node->type != PROCFS_NODE_PID_DIR)
        return -ENOTDIR;

    char *out = (char *)buf;
    uint32_t written = 0;
    uint32_t found = 0;
    uint32_t idx = 0;

    if (!node || node->type == PROCFS_NODE_ROOT) {
        // idx 0 → "meminfo" (DT_REG)
        if (idx++ >= start_idx) {
            const char *name = "meminfo";
            uint16_t reclen = FS_DENT_RECLEN(strlen(name));
            if (written + reclen > bufsize) goto done;
            fs_dent_t *dent = (fs_dent_t *)(out + written);
            dent->d_ino    = 0;
            dent->d_reclen = reclen;
            dent->d_type   = DT_REG;
            memcpy(dent->d_name, name, strlen(name) + 1);
            written += reclen;
            found++;
        }
        // idx 1 → "self" (DT_LNK)
        if (idx++ >= start_idx) {
            const char *name = "self";
            uint16_t reclen = FS_DENT_RECLEN(strlen(name));
            if (written + reclen > bufsize) goto done;
            fs_dent_t *dent = (fs_dent_t *)(out + written);
            dent->d_ino    = 0;
            dent->d_reclen = reclen;
            dent->d_type   = DT_LNK;
            memcpy(dent->d_name, name, strlen(name) + 1);
            written += reclen;
            found++;
        }
        // idx 2+ → numeric pids
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *p = &proctab[i];
            if (p->state == PROC_UNUSED) continue;
            if (idx++ < start_idx) continue;
            char name[PROCFS_MAX_NAME];
            int namelen = procfs_itoa(p->pid, name, sizeof(name));
            if (namelen <= 0) continue;
            uint16_t reclen = FS_DENT_RECLEN(namelen);
            if (written + reclen > bufsize) goto done;
            fs_dent_t *dent = (fs_dent_t *)(out + written);
            dent->d_ino    = (ino_t)p->pid;
            dent->d_reclen = reclen;
            dent->d_type   = DT_DIR;
            memcpy(dent->d_name, name, namelen + 1);
            written += reclen;
            found++;
        }
    } else {
        // PID directory: emit 5 entries
        for (int i = 0; i < PID_DIR_NENTRIES; i++) {
            if (idx++ < start_idx) continue;
            const char *name = pid_dir_entries[i].name;
            uint16_t reclen = FS_DENT_RECLEN(strlen(name));
            if (written + reclen > bufsize) goto done;
            fs_dent_t *dent = (fs_dent_t *)(out + written);
            dent->d_ino    = (ino_t)node->pid;
            dent->d_reclen = reclen;
            dent->d_type   = pid_dir_entries[i].dtype;
            memcpy(dent->d_name, name, strlen(name) + 1);
            written += reclen;
            found++;
        }
    }

done:
    *entries_out = found;
    return (int)written;
}

// ── mount/unmount ────────────────────────────────────────────────────────────

static int procfs_unmount(mount_t *mnt) {
    if (!mnt) return -EINVAL;
    if (procfs_root) { kfree(procfs_root); procfs_root = NULL; }
    return 0;
}

static vnode_t *procfs_mount(mount_t *mnt, const char *source) {
    (void)source;
    if (!mnt) return NULL;

    vnode_t *root = kzalloc(sizeof(vnode_t));
    if (!root) return NULL;

    root->type      = VNODE_DIR;
    root->mode      = 0555;
    root->size      = 0;
    root->nlink     = 2;
    root->ref_count = 0;
    root->mount     = mnt;
    root->fs_data   = NULL;
    root->fs_type   = mnt->fs_type;

    procfs_root = root;
    return root;
}

// ── fs_type registration ─────────────────────────────────────────────────────

static fs_type_t procfs_type = {
    .name = "procfs",
    .ops = {
        .mount    = procfs_mount,
        .unmount  = procfs_unmount,
        .lookup   = procfs_lookup,
        .read     = procfs_read,
        .readlink = procfs_readlink,
        .readdir  = procfs_readdir,
    },
    .next = NULL,
};

void procfs_init(void) {
    vfs_register_fs(&procfs_type);
    printk("procfs: registered\n");
}
