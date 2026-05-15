#include "sys/ioctl.h"
#include "stdarg.h"
#include "termios.h"
#include "errno.h"
#include "unistd.h"

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;

    switch (request) {
        case TCGETS: {
            va_start(ap, request);
            struct termios *t = va_arg(ap, struct termios *);
            va_end(ap);
            return tcgetattr(fd, t);
        }
        case TCSETS: {
            va_start(ap, request);
            const struct termios *t = va_arg(ap, const struct termios *);
            va_end(ap);
            return tcsetattr(fd, TCSANOW, t);
        }
        case TCSETSW: {
            va_start(ap, request);
            const struct termios *t = va_arg(ap, const struct termios *);
            va_end(ap);
            return tcsetattr(fd, TCSADRAIN, t);
        }
        case TCSETSF: {
            va_start(ap, request);
            const struct termios *t = va_arg(ap, const struct termios *);
            va_end(ap);
            return tcsetattr(fd, TCSAFLUSH, t);
        }
        case TIOCGWINSZ: {
            va_start(ap, request);
            struct winsize *ws = va_arg(ap, struct winsize *);
            va_end(ap);
            if (!ws)
            {
                errno = EINVAL;
                return -1;
            }

            ws->ws_row = 24;
            ws->ws_col = 80;
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            return 0;
        }
        case TIOCSWINSZ:
            return 0;
        default:
            errno = ENOTTY;
            return -1;
    }
}

pid_t tcgetsid(int fd) {
    struct termios t;
    if (tcgetattr(fd, &t) < 0) {   /* sets errno=ENOTTY for non-tty fds */
        return (pid_t)-1;
    }
    return getpid();               /* best approximation; correct post-setsid() */
}

// ─────────────────────────────────────────────────────────────────────────────
// POSIX termios stubs.
// Declared in <termios.h> for source-compat with TCC/busybox; behavior is
// minimal — the kernel doesn't yet implement drain/flow/flush/break/sessions.
// cf*speed accessors are real (the speed lives in the struct), tcget/setwinsize
// route through the existing TIOC*WINSZ ioctl path.
// ─────────────────────────────────────────────────────────────────────────────
speed_t cfgetispeed(const struct termios *t) { return t->c_ispeed; }
speed_t cfgetospeed(const struct termios *t) { return t->c_ospeed; }
int cfsetispeed(struct termios *t, speed_t s) { t->c_ispeed = s; return 0; }
int cfsetospeed(struct termios *t, speed_t s) { t->c_ospeed = s; return 0; }

int tcdrain(int fd)               { (void)fd; return 0; }
int tcflow(int fd, int act)       { (void)fd; (void)act; return 0; }

/*
 * tcflush — drop pending input/output queue bytes.
 *
 * SBUnix has no output queue (writes flush synchronously), so TCOFLUSH is
 * a no-op.  For TCIFLUSH/TCIOFLUSH we round-trip the current termios
 * through tcsetattr with TCSAFLUSH; kernel term_tcsetattr() honors that
 * action by calling term_flush_input() (kernel/tty/term.c:420), which
 * zeros ihead/itail/llen/line_ready.  No new syscall needed.
 */
int tcflush(int fd, int q) {
    if (q != TCIFLUSH && q != TCOFLUSH && q != TCIOFLUSH) {
        errno = EINVAL;
        return -1;
    }
    if (q == TCOFLUSH) return 0;   /* no output queue to flush */
    struct termios t;
    if (tcgetattr(fd, &t) < 0) return -1;
    return tcsetattr(fd, TCSAFLUSH, &t);
}

int tcsendbreak(int fd, int dur)  { (void)fd; (void)dur; return 0; }

int tcgetwinsize(int fd, struct winsize *ws) {
    return ioctl(fd, TIOCGWINSZ, ws);
}
int tcsetwinsize(int fd, const struct winsize *ws) {
    return ioctl(fd, TIOCSWINSZ, ws);
}
