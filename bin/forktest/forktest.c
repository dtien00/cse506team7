#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    write(1, "before fork\n", 12);

    int pid = fork();

    if (pid == 0) {
        write(1, "child: after fork (pid==0)\n", 27);
        _exit(0);
    } else if (pid > 0) {
        write(1, "parent: after fork (pid>0)\n", 27);
        int status;
        wait(&status);
        write(1, "parent: child exited\n", 21);
    } else {
        write(1, "fork failed\n", 12);
    }
    return 0;
}
