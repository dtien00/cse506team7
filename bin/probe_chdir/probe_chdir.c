/*
 * probe_chdir.c — locate divergence between absolute and bare-name path
 * resolution after chdir().  Companion to FEEDBACK_CHDIR.md §9.
 *
 * Usage:
 *   probe_chdir [dir [name]]
 *
 * Defaults: dir = "/home", name = "hello.c".  The probe chdir's into `dir`,
 * then issues stat / access / open against the bare name and the absolute
 * path "<dir>/<name>".  It then fork+execs itself to repeat the probe in a
 * child so cwd inheritance across fork/exec can be checked.
 *
 * Expected on a correct kernel: every line ends in success and the two ino
 * values match.  Whichever line diverges from that shape locates the bug.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>

static void probe(const char *tag, const char *dir, const char *name) {
    struct stat st;
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, "?");
    printf("[%s] cwd=%s\n", tag, cwd);

    char abs[512];
    snprintf(abs, sizeof(abs), "%s/%s", dir, name);

    int r1 = stat(name, &st);
    printf("[%s] stat(\"%s\")        = %d  ino=%lu\n",
           tag, name, r1, r1 == 0 ? (unsigned long)st.st_ino : 0UL);

    int r2 = stat(abs, &st);
    printf("[%s] stat(\"%s\")  = %d  ino=%lu\n",
           tag, abs, r2, r2 == 0 ? (unsigned long)st.st_ino : 0UL);

    int r3 = access(name, F_OK);
    printf("[%s] access(\"%s\", F_OK) = %d\n", tag, name, r3);

    int fd = open(name, O_RDONLY, 0);
    printf("[%s] open(\"%s\", RDONLY) = %d\n", tag, name, fd);
    if (fd >= 0) close(fd);
}

int main(int argc, char **argv) {
    const char *dir  = (argc > 1) ? argv[1] : "/home";
    const char *name = (argc > 2) ? argv[2] : "hello.c";

    if (chdir(dir) < 0) {
        printf("chdir(%s) failed\n", dir);
        return 1;
    }

    probe("parent", dir, name);

    pid_t pid = fork();
    if (pid == 0) {
        probe("child", dir, name);
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return 0;
}
