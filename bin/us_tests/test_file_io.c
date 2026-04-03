#include <fcntl.h>
#include <unistd.h>
#include "us_test_common.h"

void run_test_file_io(us_test_group_t *group) {
    int fd;
    char buf[32];
    int n;

    ut_group_begin(group, "fcntl+unistd");

    fd = open("/hello.txt", O_RDONLY);
    TEST(group, "open existing tarfs file", fd >= 0);

    if (fd >= 0) {
        n = (int)read(fd, buf, sizeof(buf) - 1);
        TEST(group, "read returns positive bytes", n > 0);
        if (n > 0) {
            buf[n] = '\0';
            TEST(group, "read buffer NUL-terminated locally", buf[n] == '\0');
        }
        TEST(group, "close succeeds", close(fd) == 0);
    }

    fd = open("/definitely_missing_file", O_RDONLY);
    TEST(group, "open missing file fails", fd < 0);

    ut_group_end(group);
}
