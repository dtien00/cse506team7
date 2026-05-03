#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int rm_recursive(const char *path);

static int rm_dir_contents(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        printf("rm: cannot open '%s'\n", path);
        return 1;
    }

    struct dirent *de;
    char child[512];
    int err = 0;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        int len = strlen(path);
        if (len + 1 + (int)strlen(de->d_name) >= (int)sizeof(child)) {
            printf("rm: path too long\n");
            err = 1;
            continue;
        }
        memcpy(child, path, len);
        child[len] = '/';
        strcpy(child + len + 1, de->d_name);
        if (rm_recursive(child) != 0)
            err = 1;
    }
    closedir(dir);
    return err;
}

static int rm_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) {
        printf("rm: cannot stat '%s'\n", path);
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (rm_dir_contents(path) != 0)
            return 1;
        if (rmdir(path) < 0) {
            printf("rm: cannot remove dir '%s'\n", path);
            return 1;
        }
    } else {
        if (unlink(path) < 0) {
            printf("rm: cannot remove '%s'\n", path);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        write(STDERR_FILENO, "rm: missing operand\n", 20);
        return 1;
    }

    int recursive = 0;
    int first_arg = 1;

    if (argc >= 2 && strcmp(argv[1], "-r") == 0) {
        recursive = 1;
        first_arg = 2;
    }

    if (first_arg >= argc) {
        write(STDERR_FILENO, "rm: missing operand\n", 20);
        return 1;
    }

    for (int i = first_arg; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) < 0) {
            printf("rm: cannot stat '%s'\n", argv[i]);
            return 1;
        }

        if (S_ISDIR(st.st_mode)) {
            if (!recursive) {
                printf("rm: cannot remove '%s': is a directory\n", argv[i]);
                return 1;
            }
            if (rm_recursive(argv[i]) != 0)
                return 1;
        } else {
            if (unlink(argv[i]) < 0) {
                printf("rm: cannot remove '%s'\n", argv[i]);
                return 1;
            }
        }
    }

    return 0;
}
