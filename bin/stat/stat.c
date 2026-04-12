#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void format_mode(mode_t mode, char out[11]) {
    out[0]  = S_ISDIR(mode)  ? 'd' : '-';
    out[1]  = (mode & 0400) ? 'r' : '-';
    out[2]  = (mode & 0200) ? 'w' : '-';
    out[3]  = (mode & 0100) ? 'x' : '-';
    out[4]  = (mode & 0040) ? 'r' : '-';
    out[5]  = (mode & 0020) ? 'w' : '-';
    out[6]  = (mode & 0010) ? 'x' : '-';
    out[7]  = (mode & 0004) ? 'r' : '-';
    out[8]  = (mode & 0002) ? 'w' : '-';
    out[9]  = (mode & 0001) ? 'x' : '-';
    out[10] = '\0';
}

static int stat_file(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) {
        fprintf(stderr, "stat: cannot stat '%s'\n", path);
        return 1;
    }
    char mode_str[11];
    format_mode(st.st_mode, mode_str);
    printf("  File: %s\n",   path);
    printf("  Size: %llu\n", (unsigned long long)st.st_size);
    printf(" Inode: %llu\n", (unsigned long long)st.st_ino);
    printf(" Links: %u\n",   (unsigned)st.st_nlink);
    printf("Access: (%04o/%s)\n", (unsigned)(st.st_mode & 0xFFF), mode_str);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: stat <file> [file...]\n");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++)
        rc |= stat_file(argv[i]);
    return rc;
}
