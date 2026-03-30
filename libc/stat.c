#include "sys/stat.h"
#include "string.h"

int stat(const char *pathname, struct stat *statbuf) {
    if (!pathname || !statbuf) return -1;
    memset(statbuf, 0, sizeof(struct stat));
    statbuf->st_mode = S_IFREG | 0644;
    return -1;
}

int fstat(int fd, struct stat *statbuf) {
    if (!statbuf) return -1;
    (void)fd;
    memset(statbuf, 0, sizeof(struct stat));
    statbuf->st_mode = S_IFREG | 0644;
    return -1;
}

int mkdir(const char *pathname, mode_t mode) {
    (void)pathname;
    (void)mode;
    return -1;
}

int chmod(const char *pathname, mode_t mode) {
    (void)pathname;
    (void)mode;
    return -1;
}

int fchmod(int fd, mode_t mode) {
    (void)fd;
    (void)mode;
    return -1;
}
