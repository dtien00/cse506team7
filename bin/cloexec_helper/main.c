/*
 * cloexec_helper — helper binary for the CLOSE_ON_EXEC test.
 *
 * Usage: cloexec_helper <fd>
 *
 * Attempts read(fd, buf, 1).  Reports via exit code:
 *   0 — fd was closed (read returned < 0): O_CLOEXEC was enforced correctly.
 *   1 — fd is still open (read returned >= 0): O_CLOEXEC was NOT enforced.
 *   2 — wrong usage.
 *
 * NOTE: The raw syscall.S read() wrapper does not set errno — it returns
 * the kernel's negative error code directly.  We therefore distinguish
 * open vs. closed purely by sign of the return value.
 */

#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2)
        _exit(2);

    int fd = atoi(argv[1]);
    char buf[1];
    ssize_t r = read(fd, buf, 1);

    if (r >= 0)
        _exit(1);   /* fd still open — O_CLOEXEC not enforced */

    _exit(0);       /* fd was closed (kernel returned negative) — O_CLOEXEC worked */
}
