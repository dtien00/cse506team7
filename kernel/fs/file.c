#include "fs/file.h"
#include "arch/riscv.h"
#include "errno.h"
#include "fs/block_cache.h"
#include "fs/vfs.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "process/process.h"
#include "tty/term.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFLNK 0120000

static void file_release(file_t *f);

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
    if (!f)
        return;

    table->files[fd] = NULL;
    file_release(f);
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
/* Normalize 'input' into an absolute path in 'out'.                  */
/* Delegates entirely to dentry_normalize_path which handles ".", "..",*/
/* consecutive slashes, and relative-to-cwd expansion.               */
/* Returns 0 on success, negative errno on error.                     */
/* ------------------------------------------------------------------ */
static int normalize_path(const char *input, char *out, size_t size) {
    if (!current || !current->fs)
        return -EINVAL;
    return dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                                 input, out, size);
}

static int file_required_perms_for_open(int flags) {
    switch (flags & 0x3) {
        case O_WRONLY:
            return VFS_PERM_WRITE;
        case O_RDWR:
            return VFS_PERM_READ | VFS_PERM_WRITE;
        case O_RDONLY:
        default:
            return VFS_PERM_READ;
    }
}

int file_open(const char *path) {
    return file_open_with_flags(path, O_RDONLY, 0);
}

int file_open_with_flags(const char *path, int flags, mode_t mode) {
    if (!path || !current || !current->fs)
        return -EINVAL;

    if (flags & O_CREAT) {
        dentry_t *existing = NULL;
        int exists = vfs_lookup_from(current->fs->cwd, path, &existing);
        if (exists == 0) {
            dentry_put(existing);
            if (flags & O_EXCL)
                return -EEXIST;
        } else {
            /* vfs_create uses vfs_find_mount() which does prefix matching,
             * so it needs an already-normalized absolute path. */
            char resolved[MAX_PATH];
            if (normalize_path(path, resolved, sizeof(resolved)) != 0)
                return -ENAMETOOLONG;
            int create_rc = vfs_create(resolved, mode ? mode : 0644);
            if (create_rc < 0)
                return create_rc;
        }
    }

    /* Resolve relative to process cwd so paths like "." and "foo/bar"
     * work correctly after chdir. Absolute paths are unaffected. */
    dentry_t *d = NULL;
    int lookup_err = vfs_lookup_from(current->fs->cwd, path, &d);
    if (lookup_err)
        return lookup_err;
    // vfs_lookup_from returns ref=1 — that ref is owned by this open-file instance.

    if ((flags & O_DIRECTORY) && d->d_vnode && d->d_vnode->type != VNODE_DIR) {
        dentry_put(d);
        return -ENOTDIR;
    }

    int perm_err = vfs_may_access(d->d_vnode, file_required_perms_for_open(flags));
    if (perm_err) {
        dentry_put(d);
        return perm_err;
    }

    if ((flags & (O_TRUNC | O_APPEND)) && !(file_required_perms_for_open(flags) & VFS_PERM_WRITE)) {
        dentry_put(d);
        return -EACCES;
    }

    // 2. Allocate the file_t wrapper
    file_t *f = NULL;
    f = kzalloc(sizeof(file_t));
    if (!f) {
        dentry_put(d);
        return -ENOMEM;
    }

    // 3. Wire it up
    // file_t now owns the ref acquired above.
    f->dentry = d;
    f->vnode = d->d_vnode;
    f->type = FILE_VNODE;
    f->offset = 0;
    f->ref_count = 1;
    f->pipe = NULL;
    f->pipe_flags = 0;
    f->flags = flags;

    if ((flags & O_TRUNC) && (flags & (O_WRONLY | O_RDWR))) {
        if (f->vnode && f->vnode->mount && f->vnode->mount->fs_type && f->vnode->mount->fs_type->ops.truncate)
            f->vnode->mount->fs_type->ops.truncate(f->vnode);
    }

    if (flags & O_APPEND)
        f->offset = f->vnode ? f->vnode->size : 0;

    int fd = fd_alloc(&current->fds, f);
    if (fd < 0) {
        file_release(f);
        return -EMFILE;
    }

    return fd;
}

int file_close(int fd) {
    if (!current || fd < 0 || fd >= MAX_FDS)
        return -EBADF;

    file_t *f = fd_get(&current->fds, fd);
    if (!f)
        return -EBADF;

    current->fds.files[fd] = NULL;
    file_release(f);
    return 0;
}

static void file_release(file_t *f) {
    if (f->ref_count <= 0) {
        printk("file_close: double-close detected (ref_count=%d)\n", f->ref_count);
        return;
    }
    f->ref_count--;
    if (f->ref_count > 0)
        return;

    // Release resources based on type
    if (f->type == FILE_VNODE && f->dentry) {
        if (f->flags & O_WRONLY || f->flags & O_RDWR)
            block_cache_flush();
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

int file_read(int fd, void *buf, size_t len) {
    file_t *f = fd_get(&current->fds, fd);
    if (!f)
        return -EBADF;
    if (!buf)
        return -EINVAL;

    switch (f->type) {
        case FILE_VNODE: {
            if ((f->flags & O_ACCMODE) == O_WRONLY)
                return -EBADF;
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
                dst[nread] = p->buffer[p->read_pos];
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

int file_write(int fd, const void *buf, size_t len) {
    file_t *f = fd_get(&current->fds, fd);
    if (!f)
        return -EBADF;
    if (!buf)
        return -EINVAL;

    switch (f->type) {
        case FILE_STDOUT:
            return term_write(&console_term, buf, (uint32_t) len);

        case FILE_VNODE: {
            if ((f->flags & O_ACCMODE) == O_RDONLY)
                return -EBADF;
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
                if (p->readers <= 0) {
                    current->pending_signals |= (1U << KERNEL_SIGPIPE);
                    return nwritten > 0 ? (int) nwritten : -EPIPE;
                }

                while (p->count == PIPE_CAPACITY) {
                    if (p->readers <= 0) {
                        current->pending_signals |= (1U << KERNEL_SIGPIPE);
                        return nwritten > 0 ? (int) nwritten : -EPIPE;
                    }
                    proc_sleep(p);
                }
                p->buffer[p->write_pos] = src[nwritten];
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

int file_lseek(int fd, size_t offset, int whence) {
    file_t *f = fd_get(&current->fds, fd);
    if (!f)
        return -EBADF;

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
        return -EISDIR; // reject all other seeks on directories
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
    if (!f)
        return;
    f->ref_count++;
}

static int vnode_fill_stat(vnode_t *vn, struct stat *kst) {
    if (!vn || !kst)
        return -EINVAL;

    kst->st_dev = vn->mount ? vn->mount->dev_id : 0;
    kst->st_ino = (ino_t) vn->ino;

    // Combine permission bits with file-type bits
    // vnode->mode stores only permission bits (0644, 0755, etc.)
    // File type bits come from vnode->type
    mode_t type_bits = 0;
    switch (vn->type) {
        case VNODE_REG:
            type_bits = S_IFREG;
            break;
        case VNODE_DIR:
            type_bits = S_IFDIR;
            break;
        case VNODE_LNK:
            type_bits = S_IFLNK;
            break;
        default:
            type_bits = 0;
            break;
    }
    kst->st_mode = (mode_t) (type_bits | (vn->mode & 0xFFF));

    uint32_t nlink = vn->nlink;
    if (vn->mount && vn->mount->fs_type && vn->mount->fs_type->ops.get_nlink)
        nlink = vn->mount->fs_type->ops.get_nlink(vn);
    printk("vnode_fill_stat: ino=%u vnode->nlink=%u fresh_nlink=%u\n",
           (unsigned)vn->ino, (unsigned)vn->nlink, nlink);
    kst->st_nlink = (nlink_t) nlink;
    kst->st_uid = 0; // no user model
    kst->st_gid = 0; // no group model
    kst->st_size = (off_t) vn->size;

    kst->st_atime = (time_t) vn->atime;
    kst->st_mtime = (time_t) vn->mtime;
    kst->st_ctime = (time_t) vn->ctime;

    return 0;
}

int file_stat(const char *kpath, struct stat *kst) {
    if (!kpath || !kst || !current || !current->fs)
        return -EINVAL;

    dentry_t *d = NULL;
    int lookup_err = vfs_lookup_from(current->fs->cwd, kpath, &d);
    if (lookup_err)
        return lookup_err;

    int ret = vnode_fill_stat(d->d_vnode, kst);
    dentry_put(d);
    return ret;
}

int file_lstat(const char *kpath, struct stat *kst) {
    if (!kpath || !kst || !current || !current->fs)
        return -EINVAL;

    dentry_t *d = NULL;
    int lookup_err = vfs_lookup_nofollow_pub(current->fs->cwd, kpath, &d);
    if (lookup_err)
        return lookup_err;

    int ret = vnode_fill_stat(d->d_vnode, kst);
    dentry_put(d);
    return ret;
}

int file_fstat(int fd, struct stat *kst) {
    if (!current || !kst)
        return -EINVAL;

    file_t *f = fd_get(&current->fds, fd);
    if (!f)
        return -EBADF;

    // stat only makes sense for vnodes; pipes/stdin/stdout have no inode
    if (f->type != FILE_VNODE)
        return -EBADF;
    if (!f->vnode)
        return -EBADF;

    return vnode_fill_stat(f->vnode, kst);
}

// -----------------------------------------------------------------------------
// file operations - proc level reference counting
// -----------------------------------------------------------------------------

// f_get: look up fd, bump refcount so it's safe to use across a syscall
file_t *f_get(struct proc *p, int fd) {
    if (!p)
        return NULL;
    file_t *f = fd_get(&p->fds, fd);
    if (!f)
        return NULL;
    file_ref(f); // just delegates to the primitive
    return f;
}

// f_put: release the reference acquired by fget
void f_put(file_t *f) {
    if (!f)
        return;
    file_release(f);
}
