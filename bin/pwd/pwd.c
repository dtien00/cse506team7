#include <stdio.h>
#include <unistd.h>

int main(void) {
    char buf[1024];
    if (getcwd(buf, sizeof(buf)) != NULL) {
        printf("%s\n", buf);
        return 0;
    } else {
        printf("pwd: error getting current directory\n");
        return 1;
    }
}
