#include "dirent.h"
#include "fcntl.h"
#include "unistd.h"
#include "stdlib.h"

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
    
    ssize_t n = read(dirp->fd, &dirp->entry, sizeof(struct dirent));
    if (n <= 0) {
        return NULL;
    }
    
    return &dirp->entry;
}

int closedir(DIR *dirp) {
    if (!dirp) return -1;
    
    int ret = close(dirp->fd);
    free(dirp);
    return ret;
}

void rewinddir(DIR *dirp) {
    if (!dirp) return;
    lseek(dirp->fd, 0, SEEK_SET);
}

int chdir(const char *path) {
    (void)path;
    return -1;
}

char* getcwd(char *buf, size_t size) {
    if (!buf || size == 0) return NULL;
    buf[0] = '/';
    buf[1] = '\0';
    return buf;
}
