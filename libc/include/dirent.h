#ifndef _DIRENT_H
#define _DIRENT_H

#include "types.h"

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

#define NAME_MAX 255
#define DIRENT_BUF_SIZE 4096

struct dirent {
    ino_t d_ino;
    unsigned char d_type;
    char d_name[NAME_MAX + 1];
};

typedef struct {
    int fd;
    char buf[DIRENT_BUF_SIZE]; // raw kernel entries (fs_dent_t records)
    int buf_pos; // read cursor within buf
    int buf_len; // valid bytes currently in buf
    struct dirent entry; // storage readdir() returns a pointer to
} DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
void rewinddir(DIR *dirp);
int dirfd(DIR *dirp);
DIR *fdopendir(int fd);
int alphasort(const struct dirent **, const struct dirent **);
int scandir(const char *, struct dirent ***, int (*)(const struct dirent *),
            int (*)(const struct dirent **, const struct dirent **));
int nostat_filter(const struct dirent *); // bonus, non-POSIX name

#endif
