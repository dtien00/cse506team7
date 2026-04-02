#include <stdio.h>
#include <unistd.h>
#include <stddef.h>

int main(void) {
    char buf[1024];
    if (getcwd(buf, sizeof(buf)) != NULL) {
        printf("%s\n", buf);
        return 0;
    }
    return 1;
}
