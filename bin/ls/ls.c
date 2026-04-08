#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "dirent.h"
#include "fcntl.h"
#include "sys/stat.h"

/* ------------------------------------------------------------------ */
/* Flags                                                               */
/* ------------------------------------------------------------------ */
static int flag_l = 0;   /* -l  long format   */
static int flag_a = 0;   /* -a  show hidden   */

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Build "path/name" into buf — returns buf */
static char *joinpath(const char *dir, const char *name, char *buf, size_t n) {
    size_t dlen = strlen(dir);
    /* avoid double slash if dir already ends with '/' */
    if (dlen > 0 && dir[dlen - 1] == '/')
        snprintf(buf, n, "%s%s", dir, name);
    else
        snprintf(buf, n, "%s/%s", dir, name);
    return buf;
}

/* Format permissions bits into rwxrwxrwx string */
static void format_mode(mode_t mode, int is_dir, char out[11]) {
    out[0] = is_dir ? 'd' : '-';
    out[1] = (mode & 0400) ? 'r' : '-';
    out[2] = (mode & 0200) ? 'w' : '-';
    out[3] = (mode & 0100) ? 'x' : '-';
    out[4] = (mode & 0040) ? 'r' : '-';
    out[5] = (mode & 0020) ? 'w' : '-';
    out[6] = (mode & 0010) ? 'x' : '-';
    out[7] = (mode & 0004) ? 'r' : '-';
    out[8] = (mode & 0002) ? 'w' : '-';
    out[9] = (mode & 0001) ? 'x' : '-';
    out[10] = '\0';
}

/* ------------------------------------------------------------------ */
/* Print one entry                                                     */
/* ------------------------------------------------------------------ */
static void print_entry(const char *dirpath, const struct dirent *e) {
    int is_dir = (e->d_type == DT_DIR);

    if (!flag_l) {
        /* Short format */
        if (is_dir)
            printf("%s/\n", e->d_name);
        else
            printf("%s\n", e->d_name);
        return;
    }

    /* Long format — needs stat */
    char fullpath[512];
    joinpath(dirpath, e->d_name, fullpath, sizeof(fullpath));

    struct stat st;
    if (stat(fullpath, &st) == 0) {
        char mode[11];
        format_mode(st.st_mode, is_dir, mode);
        /* mode  links  size  name */
        printf("%s  %3u  %8llu  %s%s\n",
               mode,
               (unsigned)st.st_nlink,
               (unsigned long long)st.st_size,
               e->d_name,
               is_dir ? "/" : "");
    } else {
        /* stat not yet implemented — degrade gracefully */
        printf("%s  ?  ?  %s%s\n",
               is_dir ? "d?????????" : "-?????????",
               e->d_name,
               is_dir ? "/" : "");
    }
}

/* ------------------------------------------------------------------ */
/* Filter: hide dot files unless -a                                    */
/* ------------------------------------------------------------------ */
static int ls_filter(const struct dirent *e) {
    if (flag_a) return 1;
    return e->d_name[0] != '.';
}

/* ------------------------------------------------------------------ */
/* List one directory                                                  */
/* ------------------------------------------------------------------ */
static void ls(const char *path) {
    struct dirent **entries;
    int n = scandir(path, &entries, ls_filter, alphasort);
    if (n < 0) {
        fprintf(stderr, "ls: cannot open: %s\n", path);
        return;
    }

    for (int i = 0; i < n; i++) {
        print_entry(path, entries[i]);
        free(entries[i]);
    }
    free(entries);
}

/* ------------------------------------------------------------------ */
/* Argument parsing                                                    */
/* ------------------------------------------------------------------ */
static int parse_flags(int argc, char *argv[], int *first_path) {
    *first_path = 1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            *first_path = i;
            return 0;
        }
        /* allow -l, -a, -la, -al */
        for (char *c = &argv[i][1]; *c; c++) {
            if      (*c == 'l') flag_l = 1;
            else if (*c == 'a') flag_a = 1;
            else {
                fprintf(stderr, "ls: unknown flag: -%c\n", *c);
                return -1;
            }
        }
        *first_path = i + 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    int first_path;
    if (parse_flags(argc, argv, &first_path) < 0)
        return 1;

    if (first_path >= argc) {
        /* no path arguments — list cwd */
        ls(".");
    } else {
        int multi = (argc - first_path) > 1;
        for (int i = first_path; i < argc; i++) {
            if (multi)
                printf("%s:\n", argv[i]);
            ls(argv[i]);
            if (multi && i + 1 < argc)
                printf("\n");
        }
    }
    return 0;
}