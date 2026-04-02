#include "dirent.h"
#include "fcntl.h"
#include "unistd.h"
#include "stdlib.h"
#include "string.h"

struct linux_dirent64 {
    uint64_t       d_ino;
    int64_t        d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

int getdents64(int fd, void *dirp, size_t count);

DIR* opendir(const char *name) {
    int fd = open(name, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        return NULL;
    }
    
    DIR *dir = malloc(sizeof(DIR));
    if (!dir) {
        close(fd);
        return NULL;
    }
    
    dir->fd = fd;
    return dir;
}

struct dirent* readdir(DIR *dirp) {
    if (!dirp) return NULL;
    
    static char buf[1024];
    static struct linux_dirent64 *d = NULL;
    static int buf_pos = 0;
    static int buf_len = 0;
    
    if (buf_pos >= buf_len) {
        buf_len = getdents64(dirp->fd, buf, sizeof(buf));
        if (buf_len <= 0) {
            return NULL;
        }
        buf_pos = 0;
        d = (struct linux_dirent64 *)buf;
    }
    
    if (buf_pos >= buf_len) {
        return NULL;
    }
    
    d = (struct linux_dirent64 *)(buf + buf_pos);
    buf_pos += d->d_reclen;
    
    dirp->entry.d_ino = d->d_ino;
    dirp->entry.d_off = d->d_off;
    dirp->entry.d_reclen = d->d_reclen;
    dirp->entry.d_type = d->d_type;
    
    size_t name_len = 0;
    while (d->d_name[name_len] && name_len < 255) {
        dirp->entry.d_name[name_len] = d->d_name[name_len];
        name_len++;
    }
    dirp->entry.d_name[name_len] = '\0';
    
    return &dirp->entry;
}

int closedir(DIR *dirp) {
    if (!dirp) return -1;
    int ret = close(dirp->fd);
    free(dirp);
    return ret;
}

void rewinddir(DIR *dirp) {
    if (dirp) {
        lseek(dirp->fd, 0, SEEK_SET);
    }
}
