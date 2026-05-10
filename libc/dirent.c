#include "dirent.h"

#include "errno.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

typedef struct {
    ino_t d_ino;
    uint16_t d_reclen; // bytes this record occupies in buf (with padding)
    unsigned char d_type;
    char d_name[]; // null-terminated, padded to 8-byte alignment
} fs_dent_t;

DIR *opendir(const char *name) {
    if (!name)
        return NULL;

    int fd = open(name, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return NULL;

    DIR *dir = malloc(sizeof(DIR));
    if (!dir) {
        close(fd);
        return NULL;
    }

    dir->fd = fd;
    dir->buf_pos = 0;
    dir->buf_len = 0;
    return dir;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp)
        return NULL;

    // Refill the buffer when the current batch is exhausted
    if (dirp->buf_pos >= dirp->buf_len) {
        ssize_t n = fs_getdents(dirp->fd, dirp->buf, DIRENT_BUF_SIZE, 0);
        if (n <= 0)
            return NULL; // 0 = end-of-directory, <0 = error
        dirp->buf_pos = 0;
        dirp->buf_len = (int) n;
    }

    // Parse the next record out of the buffer
    fs_dent_t *kd = (fs_dent_t *) (dirp->buf + dirp->buf_pos);
    dirp->buf_pos += kd->d_reclen; // advance by exact padded record size

    // Populate the caller-visible struct dirent — clean POSIX shape
    dirp->entry.d_ino = kd->d_ino;
    dirp->entry.d_reclen = (reclen_t) sizeof(struct dirent);
    dirp->entry.d_type = kd->d_type;

    size_t nlen = 0;
    while (kd->d_name[nlen] && nlen < NAME_MAX)
        nlen++;
    memcpy(dirp->entry.d_name, kd->d_name, nlen);
    dirp->entry.d_name[nlen] = '\0';

    return &dirp->entry;
}

int closedir(DIR *dirp) {
    if (!dirp)
        return -1;
    int ret = close(dirp->fd);
    free(dirp);
    return ret;
}

void rewinddir(DIR *dirp) {
    if (!dirp)
        return;
    lseek(dirp->fd, 0, SEEK_SET); // resets f->offset in the kernel
    dirp->buf_pos = 0; // discard any cached entries
    dirp->buf_len = 0; // force refill on next readdir()
}

int dirfd(DIR *dirp) {
    if (!dirp) {
        errno = EINVAL;
        return -1;
    }
    return dirp->fd;
}

DIR *fdopendir(int fd) {
    if (fd < 0) {
        errno = EBADF;
        return NULL;
    }

    // Verify fd refers to a directory — probe with a zero-byte read.
    // getdents sets errno=ENOTDIR if vnode->type != VNODE_DIR.
    char probe[1];
    int r = fs_getdents(fd, probe, sizeof(probe), 0);
    if (r < 0 && errno != EINVAL) { // EINVAL = buf too small, still a dir
        return NULL;
    }

    DIR *dir = malloc(sizeof(DIR));
    if (!dir) {
        errno = ENOMEM;
        return NULL;
    }

    dir->fd = fd;
    dir->buf_pos = 0; // userspace buffer starts empty
    dir->buf_len = 0; // kernel f->offset is already where caller left it
    return dir;
}

int nostat_filter(const struct dirent *e) {
    return !(e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')));
}

int alphasort(const struct dirent **a, const struct dirent **b) { return strcmp((*a)->d_name, (*b)->d_name); }

int scandir(const char *dirpath, struct dirent ***namelist, int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **)) {
    if (!dirpath || !namelist)
        return -1;

    DIR *dir = opendir(dirpath);
    if (!dir)
        return -1;

    // Growable array of dirent pointers
    size_t capacity = 16;
    size_t count = 0;

    struct dirent **list = malloc(capacity * sizeof(struct dirent *));
    if (!list) {
        closedir(dir);
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Apply filter — NULL filter means accept all
        if (filter && !filter(entry))
            continue;

        // Grow the array if needed
        if (count == capacity) {
            capacity *= 2;
            struct dirent **tmp = realloc(list, capacity * sizeof(struct dirent *));
            if (!tmp)
                goto oom;
            list = tmp;
        }

        // Deep-copy the entry — caller owns each pointer
        struct dirent *copy = malloc(sizeof(struct dirent));
        if (!copy)
            goto oom;
        memcpy(copy, entry, sizeof(struct dirent));

        list[count++] = copy;
    }

    closedir(dir);

    // Sort if comparator provided
    if (compar && count > 1)
        qsort(list, count, sizeof(struct dirent *), (int (*)(const void *, const void *)) compar);

    *namelist = list;
    return (int) count;

oom:
    // Free everything allocated so far
    for (size_t i = 0; i < count; i++)
        free(list[i]);
    free(list);
    closedir(dir);
    return -1;
}
