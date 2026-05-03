#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fflag, rflag, dflag, vflag;
static int exit_status;

static void err(const char *msg, const char *path) {
    if (!fflag)
        dprintf(STDERR_FILENO, "rm: %s '%s'\n", msg, path);
    exit_status = 1;
}

static int is_dot_or_dotdot(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return strcmp(base, ".") == 0 || strcmp(base, "..") == 0;
}

static void rm_recursive(const char *path);

static void rm_dir_contents(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        err("cannot open directory", path);
        return;
    }

    struct dirent *de;
    char child[512];
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        int plen = strlen(path);
        int nlen = strlen(de->d_name);
        if (plen + 1 + nlen >= (int)sizeof(child)) {
            dprintf(STDERR_FILENO, "rm: path too long\n");
            exit_status = 1;
            continue;
        }
        memcpy(child, path, plen);
        child[plen] = '/';
        memcpy(child + plen + 1, de->d_name, nlen + 1);
        rm_recursive(child);
    }
    closedir(dir);
}

static void rm_recursive(const char *path) {
    /* lstat: don't follow symlinks — remove the link itself */
    struct stat st;
    if (lstat(path, &st) < 0) {
        err("cannot stat", path);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        rm_dir_contents(path);
        if (rmdir(path) < 0)
            err("cannot remove directory", path);
        else if (vflag)
            printf("removed directory '%s'\n", path);
    } else {
        if (unlink(path) < 0)
            err("cannot remove", path);
        else if (vflag)
            printf("removed '%s'\n", path);
    }
}

static void parse_flags(const char *s) {
    for (int i = 1; s[i]; i++) {
        switch (s[i]) {
        case 'f': fflag = 1; break;
        case 'r':
        case 'R': rflag = 1; break;
        case 'd': dflag = 1; break;
        case 'v': vflag = 1; break;
        default:
            dprintf(STDERR_FILENO, "rm: illegal option -- %c\n", s[i]);
            _exit(1);
        }
    }
}

int main(int argc, char *argv[]) {
    int first_arg = 1;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0')
            break;
        if (strcmp(argv[i], "--") == 0) { first_arg = i + 1; goto done_opts; }
        parse_flags(argv[i]);
        first_arg = i + 1;
    }
done_opts:

    if (first_arg >= argc) {
        if (!fflag) {
            dprintf(STDERR_FILENO, "rm: missing operand\n");
            return 1;
        }
        return 0;
    }

    for (int i = first_arg; i < argc; i++) {
        const char *path = argv[i];

        /* POSIX: refuse to remove . or .. */
        if (is_dot_or_dotdot(path)) {
            dprintf(STDERR_FILENO, "rm: refusing to remove '.' or '..'\n");
            exit_status = 1;
            continue;
        }

        struct stat st;
        if (lstat(path, &st) < 0) {
            if (!fflag) {
                dprintf(STDERR_FILENO, "rm: cannot remove '%s'\n", path);
                exit_status = 1;
            }
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (rflag) {
                rm_recursive(path);
            } else if (dflag) {
                if (rmdir(path) < 0)
                    err("cannot remove directory", path);
                else if (vflag)
                    printf("removed directory '%s'\n", path);
            } else {
                dprintf(STDERR_FILENO, "rm: cannot remove '%s': Is a directory\n", path);
                exit_status = 1;
            }
        } else {
            if (unlink(path) < 0)
                err("cannot remove", path);
            else if (vflag)
                printf("removed '%s'\n", path);
        }
    }

    return exit_status;
}
