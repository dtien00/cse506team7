#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Remove dir and, with -p, each successive parent until one fails. */
static int rmdir_p(const char *dir) {
    char buf[512];
    int len = strlen(dir);
    if (len >= (int)sizeof(buf)) {
        dprintf(STDERR_FILENO, "rmdir: path too long: %s\n", dir);
        return 1;
    }
    strcpy(buf, dir);

    /* strip trailing slashes */
    while (len > 1 && buf[len - 1] == '/')
        buf[--len] = '\0';

    do {
        if (rmdir(buf) < 0) {
            dprintf(STDERR_FILENO, "rmdir: failed to remove '%s'\n", buf);
            return 1;
        }
        /* strip last component to get parent */
        while (len > 0 && buf[len - 1] != '/')
            len--;
        /* strip trailing slashes before next iteration */
        while (len > 1 && buf[len - 1] == '/')
            len--;
        buf[len] = '\0';
    } while (len > 0);

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        dprintf(STDERR_FILENO, "rmdir: missing operand\n");
        return 1;
    }

    int pflag = 0;
    int first_arg = 1;

    /* parse options: -p and combined forms like -p */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0')
            break;
        if (strcmp(argv[i], "--") == 0) { first_arg = i + 1; goto done_opts; }
        for (int j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
            case 'p': pflag = 1; break;
            default:
                dprintf(STDERR_FILENO, "rmdir: illegal option -- %c\n", argv[i][j]);
                return 1;
            }
        }
        first_arg = i + 1;
    }
done_opts:

    if (first_arg >= argc) {
        dprintf(STDERR_FILENO, "rmdir: missing operand\n");
        return 1;
    }

    int ret = 0;
    for (int i = first_arg; i < argc; i++) {
        if (pflag) {
            if (rmdir_p(argv[i]) != 0)
                ret = 1;
        } else {
            if (rmdir(argv[i]) < 0) {
                dprintf(STDERR_FILENO, "rmdir: failed to remove '%s'\n", argv[i]);
                ret = 1;
            }
        }
    }
    return ret;
}
