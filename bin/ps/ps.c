#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("PID\n");
    printf("%d\n", getpid());
    return 0;
}
