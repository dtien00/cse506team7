#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "us_test_common.h"

void run_test_termios_tty(us_test_group_t *group) {
    ut_group_begin(group, "termios_tty");

    struct termios stdin_old;
    int gr = tcgetattr(STDIN_FILENO, &stdin_old);
    TEST(group, "tcgetattr(stdin): returns 0", gr == 0);
    TEST(group, "ioctl(stdin,TCGETS): returns 0", ioctl(STDIN_FILENO, TCGETS, &stdin_old) == 0);

    if (gr != 0) {
        SKIP(group, "tcsetattr(stdin,TCSANOW): toggles ECHO bit", "tcgetattr(stdin) failed");
        SKIP(group, "tcsetattr(stdin): restore original state", "tcgetattr(stdin) failed");
        SKIP(group, "tcsetattr(stdin,invalid): returns -1 (errno=EINVAL)", "tcgetattr(stdin) failed");
        SKIP(group, "ioctl(stdin,TCSETS): toggles ECHO bit", "tcgetattr(stdin) failed");
        SKIP(group, "ioctl(stdin,TCSETSW): accepts action", "tcgetattr(stdin) failed");
        SKIP(group, "ioctl(stdin,TCSETSF): accepts action", "tcgetattr(stdin) failed");
    } else {
        struct termios mod = stdin_old;
        mod.c_lflag ^= ECHO;

        int sw = tcsetattr(STDIN_FILENO, TCSANOW, &mod);
        TEST(group, "tcsetattr(stdin,TCSANOW): returns 0", sw == 0);

        struct termios verify;
        int gv = tcgetattr(STDIN_FILENO, &verify);
        TEST(group, "tcgetattr(stdin) after set: returns 0", gv == 0);
        TEST(group, "tcsetattr(stdin,TCSANOW): toggles ECHO bit", gv == 0 && verify.c_lflag == mod.c_lflag);

        int restore = tcsetattr(STDIN_FILENO, TCSANOW, &stdin_old);
        TEST(group, "tcsetattr(stdin): restore original state", restore == 0);

        errno = 0;
        int bad_action = tcsetattr(STDIN_FILENO, 99, &stdin_old);
        TEST(group, "tcsetattr(stdin,invalid): returns -1 (errno=EINVAL)",
             bad_action == -1 && errno == EINVAL);

        int iow = ioctl(STDIN_FILENO, TCSETSW, &mod);
        TEST(group, "ioctl(stdin,TCSETSW): accepts action", iow == 0);

        struct termios iow_verify;
        int iow_get = ioctl(STDIN_FILENO, TCGETS, &iow_verify);
        TEST(group, "ioctl(stdin,TCSETSW): state applied", iow_get == 0 && iow_verify.c_lflag == mod.c_lflag);

        int iof = ioctl(STDIN_FILENO, TCSETSF, &stdin_old);
        TEST(group, "ioctl(stdin,TCSETSF): accepts action", iof == 0);

        struct termios iof_verify;
        int iof_get = ioctl(STDIN_FILENO, TCGETS, &iof_verify);
        TEST(group, "ioctl(stdin,TCSETSF): state applied", iof_get == 0 && iof_verify.c_lflag == stdin_old.c_lflag);

        int ios = ioctl(STDIN_FILENO, TCSETS, &mod);
        TEST(group, "ioctl(stdin,TCSETS): toggles ECHO bit", ios == 0);

        struct termios io_verify;
        int io_get = ioctl(STDIN_FILENO, TCGETS, &io_verify);
        TEST(group, "ioctl(stdin,TCGETS) after TCSETS: returns 0", io_get == 0);
        TEST(group, "ioctl(stdin,TCSETS): updates c_lflag", io_get == 0 && io_verify.c_lflag == mod.c_lflag);

        int io_restore = ioctl(STDIN_FILENO, TCSETS, &stdin_old);
        TEST(group, "ioctl(stdin,TCSETS): restore original state", io_restore == 0);
    }

    struct termios t;
    TEST(group, "tcgetattr(stdout): returns 0", tcgetattr(STDOUT_FILENO, &t) == 0);
    TEST(group, "tcgetattr(stderr): returns 0", tcgetattr(STDERR_FILENO, &t) == 0);

    int regfd = open("/hello.txt", O_RDONLY);
    if (regfd < 0) {
        SKIP(group, "tcgetattr(regular file): returns -1 (errno=ENOTTY)", "open(/hello.txt) failed");
        SKIP(group, "ioctl(regular file,TCGETS): returns -1 (errno=ENOTTY)", "open(/hello.txt) failed");
    } else {
        errno = 0;
        int notty = tcgetattr(regfd, &t);
        TEST(group, "tcgetattr(regular file): returns -1 (errno=ENOTTY)",
             notty == -1 && errno == ENOTTY);
        errno = 0;
        int io_notty = ioctl(regfd, TCGETS, &t);
        TEST(group, "ioctl(regular file,TCGETS): returns -1 (errno=ENOTTY)",
             io_notty == -1 && errno == ENOTTY);
        close(regfd);
    }

    struct winsize ws;
    int gwr = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    TEST(group, "ioctl(stdout,TIOCGWINSZ): returns 0", gwr == 0);
    TEST(group, "ioctl(stdout,TIOCGWINSZ): reports non-zero rows/cols", gwr == 0 && ws.ws_row > 0 && ws.ws_col > 0);
    TEST(group, "ioctl(stdout,TIOCSWINSZ): returns 0", ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws) == 0);
    errno = 0;
    TEST(group, "ioctl(stdout,TIOCGWINSZ,NULL): returns -1 (errno=EINVAL)",
         ioctl(STDOUT_FILENO, TIOCGWINSZ, 0) == -1 && errno == EINVAL);

    ut_group_end(group);
}

