#include "fs/file.h"
#include "arch/riscv.h"
#include "errno.h"
#include "fs/vfs.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "process/process.h"
#include "tty/term.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// -----------------------------------------------------------------------------
// fd_init
// Initialize a file descriptor table with stdin (0), stdout (1), stderr (2).
// These are special pseudo-files that don't point to real tarfs entries.
// -----------------------------------------------------------------------------
void fd_init(fd_table_t *table) {
    if (!table)
        return;

    for (int i = 0; i < MAX_FDS; i++) {
        table->files[i] = NULL;
    }

    // Allocate stdin (fd 0)
    file_t *stdin_file = kmalloc(sizeof(file_t));
    if (stdin_file) {
        stdin_file->type = FILE_STDIN;
        stdin_file->vnode = NULL;
        stdin_file->pipe = NULL;
        stdin_file->pipe_flags = 0;
        stdin_file->offset = 0;
        stdin_file->ref_count = 1;
        stdin_file->flags = 0;
        table->files[0] = stdin_file;
    }

    // Allocate stdout (fd 1)
    file_t *stdout_file = kmalloc(sizeof(file_t));
    if (stdout_file) {
        stdout_file->type = FILE_STDOUT;
        stdout_file->vnode = NULL;
        stdout_file->pipe = NULL;
        stdout_file->pipe_flags = 0;
        stdout_file->offset = 0;
        stdout_file->ref_count = 1;
        stdout_file->flags = 1;
        table->files[1] = stdout_file;
    }

    // Allocate stderr (fd 2) - same as stdout for now
    file_t *stderr_file = kmalloc(sizeof(file_t));
    if (stderr_file) {
        stderr_file->type = FILE_STDOUT;
        stderr_file->vnode = NULL;
        stderr_file->pipe = NULL;
        stderr_file->pipe_flags = 0;
        stderr_file->offset = 0;
        stderr_file->ref_count = 1;
        stderr_file->flags = 1;
        table->files[2] = stderr_file;
    }
}

// -----------------------------------------------------------------------------
// fd_alloc
// Finds the first free slot in the fd table, assigns the file, and returns
// the fd number. Returns -1 if the table is full.
// -----------------------------------------------------------------------------
int fd_alloc(fd_table_t *table, file_t *f) {
    if (!table || !f)
        return -1;

    for (int i = 0; i < MAX_FDS; i++) {
        if (table->files[i] == NULL) {
            table->files[i] = f;
            return i;
        }
    }

    printk("fd_alloc: no free file descriptors\n");
    return -1;
}

// -----------------------------------------------------------------------------
// fd_get
// Returns the file_t* for a given fd, or NULL if invalid/closed.
// -----------------------------------------------------------------------------
file_t *fd_get(fd_table_t *table, int fd) {
    if (!table || fd < 0 || fd >= MAX_FDS)
        return NULL;
    return table->files[fd];
}

// -----------------------------------------------------------------------------
// fd_close
// Closes the file descriptor, calls vfs_close to decrement ref count,
// and clears the slot in the table.
// -----------------------------------------------------------------------------
void fd_close(fd_table_t *table, int fd) {
    if (!table || fd < 0 || fd >= MAX_FDS)
        return;

    file_t *f = table->files[fd];
    if (f) {
        file_close(f);
        table->files[fd] = NULL;
    }
}

// -----------------------------------------------------------------------------
// fd_destroy
// Closes all open file descriptors in a table. Called by proc_destroy()
// when a process exits. Stdin/stdout/stderr (fds 0-2) are also closed here
// since they were kmalloc'd in fd_init.
// -----------------------------------------------------------------------------
void fd_destroy(fd_table_t *table) {
    if (!table)
        return;
    for (int i = 0; i < MAX_FDS; i++) {
        if (table->files[i]) {
            fd_close(table, i); // calls vfs_close → frees file_t if ref==0
        }
    }
}

// -----------------------------------------------------------------------------
// file operations
// -----------------------------------------------------------------------------

/* ------------------------------------------------------------------ */
/* Resolves 'input' relative to 'base' (cwd) into 'out' (MAX_PATH).  */
/* Handles absolute paths, ".", "..", and consecutive slashes.        */
/* Returns 0 on success, -1 if the result would overflow MAX_PATH.   */
/* This is basic, should switch to dentry caching and finding parent from there   */
/* ------------------------------------------------------------------ */
int path_resolve(const char *base, const char *input, char *out) {
    char tmp[MAX_PATH];
    int pos = 0;

    if (input[0] == '/') {
        /* Absolute path — copy as-is into tmp for normalization */
        int i = 0;
        while (input[i] && pos < MAX_PATH - 1)
            tmp[pos++] = input[i++];
        tmp[pos] = '\0';
    } else {
        /* Relative path — prepend base */
        int i = 0;
        while (base[i] && pos < MAX_PATH - 1)
            tmp[pos++] = base[i++];
        /* Add separator if base doesn't already end with '/' */
        if (pos > 0 && tmp[pos - 1] != '/' && pos < MAX_PATH - 1)
            tmp[pos++] = '/';
        i = 0;
        while (input[i] && pos < MAX_PATH - 1)
            tmp[pos++] = input[i++];
        tmp[pos] = '\0';
    }

    /* Normalize: walk components into out[] */
    out[0] = '/';
    int opos = 1;
    out[opos] = '\0';

    char *p = tmp;
    if (*p == '/') p++;  /* skip the leading '/' we already placed */

    while (*p) {
        /* Find end of component */
        char *slash = p;
        while (*slash && *slash != '/') slash++;
        int clen = (int)(slash - p);

        if (clen == 0) {
            /* consecutive slash — skip */
        } else if (clen == 1 && p[0] == '.') {
            /* "." — current dir, skip */
        } else if (clen == 2 && p[0] == '.' && p[1] == '.') {
            /* ".." — go up one level */
            if (opos > 1) {
                opos--;  /* back past the last written char */
                while (opos > 1 && out[opos - 1] != '/')
                    opos--;
                if (opos > 1) opos--;  /* remove the preceding '/' too */
                out[opos] = '\0';
            }
            /* already at root — ".." is a no-op */
        } else {
            /* Normal component: append '/' + component */
            if (opos + 1 + clen >= MAX_PATH) return -1;
            if (opos > 1) out[opos++] = '/';
            for (int i = 0; i < clen; i++)
                out[opos++] = p[i];
            out[opos] = '\0';
        }

        p = *slash ? slash + 1 : slash;
    }

    return 0;
}

file_t *file_open(const char *path) {
    if (!path || !current || !current->fs) return NULL;

    char cwd[MAX_PATH];
    if (fs_struct_get_cwd(current->fs, cwd, MAX_PATH) < 0)
        return NULL;

    char resolved[MAX_PATH];
    if (path_resolve(cwd, path, resolved) < 0)
        return NULL;

    // printk("opening file at: %s", resolved);

    // 1. Resolve the path to a dentry (stays in vfs.c — that's its job)
    dentry_t *d = vfs_lookup(resolved);
    if (!d) {
        printk("file_open: file not found: %s\n", path);
        return NULL;
    }

    // 2. Allocate the file_t wrapper
    file_t *f = kzalloc(sizeof(file_t));
    if (!f) {
        printk("file_open: kmalloc failed\n");
        dentry_put(d);
        return NULL;
    }

    // 3. Wire it up
    dentry_get(d); // file_t now owns a reference to the dentry
    f->dentry = d;
    f->vnode = d->d_vnode;
    f->type = FILE_VNODE;
    f->offset = 0;
    f->ref_count = 1;
    f->pipe = NULL;
    f->pipe_flags = 0;

    return f;
}

void file_close(file_t *f) {
    if (!f)
        return;
    if (f->ref_count <= 0) {
        printk("file_close: double-close detected (ref_count=%d)\n", f->ref_count);
        return;
    }
    f->ref_count--;
    if (f->ref_count > 0)
        return;

    // Release resources based on type
    if (f->type == FILE_VNODE && f->dentry) {
        dentry_put(f->dentry); // the only type that owns a dentry ref
        f->dentry = NULL;
        f->vnode = NULL;
    } else if (f->type == FILE_PIPE && f->pipe) {
        pipe_t *p = f->pipe;
        if (f->pipe_flags & PIPE_END_READ) {
            p->readers--;
        }
        if (f->pipe_flags & PIPE_END_WRITE) {
            p->writers--;
        }
        proc_wakeup(p);
        if (p->readers <= 0 && p->writers <= 0) {
            kfree(p);
        }
    }
    kfree(f);
}

int file_read(file_t *f, void *buf, size_t len) {
    if (!f || !buf)
        return -EINVAL;

    switch (f->type) {
        case FILE_VNODE: {
            if (!f->vnode || !f->vnode->mount || !f->vnode->mount->fs_type->ops.read) {
                printk("file_read: no read op on vnode\n");
                return -EBADF;
            }
            int n = f->vnode->mount->fs_type->ops.read(f->vnode, buf, f->offset, len);
            if (n > 0)
                f->offset += (size_t) n;
            return n;
        }

        case FILE_STDIN:
            return term_read(&console_term, buf, (uint32_t) len);

        case FILE_PIPE: {
            pipe_t *p = f->pipe;
            char *dst = (char *) buf;
            size_t nread = 0;

            if (!p || !(f->pipe_flags & PIPE_END_READ))
                return -EBADF;

            while (nread < len) {
                while (p->count == 0) {
                    if (p->writers <= 0)
                        return (int) nread; // EOF — all writers closed
                    proc_sleep(p);
                }
                set_csr_bit(sstatus, SSTATUS_SUM);
                dst[nread] = p->buffer[p->read_pos];
                clear_csr_bit(sstatus, SSTATUS_SUM);
                nread++;
                p->read_pos = (p->read_pos + 1) % PIPE_CAPACITY;
                p->count--;
                proc_wakeup(p);
            }
            return (int) nread;
        }

        case FILE_STDOUT:
            printk("file_read: cannot read from stdout\n");
            return -EBADF;

        default:
            return -EBADF;
    }
}

int file_write(file_t *f, const void *buf, size_t len) {
    if (!f || !buf)
        return -EINVAL;

    switch (f->type) {
        case FILE_STDOUT:
            return term_write(&console_term, buf, (uint32_t) len);

        case FILE_VNODE: {
            if (!f->vnode || !f->vnode->mount || !f->vnode->mount->fs_type->ops.write) {
                printk("file_write: no write op on vnode\n");
                return -EBADF;
            }
            // O_APPEND: always write at end of file
            if (f->flags & O_APPEND)
                f->offset = f->vnode->size;
            int n = f->vnode->mount->fs_type->ops.write(f->vnode, buf, f->offset, len);
            if (n > 0)
                f->offset += (size_t) n;
            return n;
        }

        case FILE_PIPE: {
            pipe_t *p = f->pipe;
            const char *src = (const char *) buf;
            size_t nwritten = 0;

            if (!p || !(f->pipe_flags & PIPE_END_WRITE))
                return -EBADF;

            while (nwritten < len) {
                if (p->readers <= 0)
                    return nwritten > 0 ? (int) nwritten : -EPIPE;

                while (p->count == PIPE_CAPACITY) {
                    if (p->readers <= 0)
                        return nwritten > 0 ? (int) nwritten : -EPIPE;
                    proc_sleep(p);
                }
                set_csr_bit(sstatus, SSTATUS_SUM);
                p->buffer[p->write_pos] = src[nwritten];
                clear_csr_bit(sstatus, SSTATUS_SUM);
                nwritten++;
                p->write_pos = (p->write_pos + 1) % PIPE_CAPACITY;
                p->count++;
                proc_wakeup(p);
            }
            return (int) nwritten;
        }

        case FILE_STDIN:
            printk("file_write: cannot write to stdin\n");
            return -EBADF;

        default:
            return -EBADF;
    }
}

int file_lseek(file_t *f, size_t offset, int whence) {
    if (!f)
        return -EINVAL;

    // Pipes and terminal files have no concept of position
    if (f->type == FILE_PIPE || f->type == FILE_STDIN || f->type == FILE_STDOUT) {
        printk("file_lseek: cannot seek on pipe/stdio\n");
        return -ESPIPE;
    }

    // Everything below is FILE_VNODE (or legacy FILE_TARFS/FILE_TMPFS)
    if (!f->vnode) {
        printk("file_lseek: no vnode on file\n");
        return -EBADF;
    }

    if (f->vnode && f->vnode->type == VNODE_DIR) {
        if (whence == SEEK_SET && offset == 0) {
            f->offset = 0;
            return 0;
        }
        return -EISDIR;   // reject all other seeks on directories
    }

    size_t new_offset = 0;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;

        case SEEK_CUR:
            new_offset = f->offset + offset;
            break;

        case SEEK_END:
            new_offset = f->vnode->size + offset;
            break;

        default:
            printk("file_lseek: invalid whence %d\n", whence);
            return -EINVAL;
    }

    // Don't allow seeking past EOF
    if (f->vnode->mount && f->vnode->mount->fs_type && f->vnode->mount->fs_type->ops.write == NULL &&
        new_offset > f->vnode->size) {
        printk("file_lseek: seek past EOF (offset=%zu, size=%zu) on read-only file not allowed\n", new_offset,
               f->vnode->size);
        return -EINVAL;
    }

    f->offset = new_offset;
    return (int) new_offset;
}

void file_ref(file_t *f) {
    if (!f) return;
    f->ref_count++;
}

// -----------------------------------------------------------------------------
// file operations - proc level reference counting
// -----------------------------------------------------------------------------

// f_get: look up fd, bump refcount so it's safe to use across a syscall
// (prevents the file being closed by another thread while you hold it)
file_t *f_get(struct proc *p, int fd) {
    file_t *f = fd_get(&p->fds, fd);
    if (!f) return NULL;
    file_ref(f);    // just delegates to the primitive
    return f;
}

// f_put: release the reference acquired by fget
void f_put(file_t *f) {
    file_close(f); // file_close already handles ref_count-- and kfree at 0
}
