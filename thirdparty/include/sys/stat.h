#pragma once
#include_next <sys/stat.h>

int fstat(int fd, struct stat *st);
int chmod(const char *path, mode_t mode);
mode_t umask(mode_t mask);
int mknod(const char *path, mode_t mode, dev_t dev);
