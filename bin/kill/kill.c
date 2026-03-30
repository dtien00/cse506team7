#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

int kill(pid_t pid, int sig);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        write(STDERR_FILENO, "kill: usage: kill [-signal] pid\n", 32);
        return 1;
    }
    
    int sig = SIGTERM;
    int pid_idx = 1;
    
    if (argv[1][0] == '-') {
        sig = atoi(argv[1] + 1);
        pid_idx = 2;
        if (argc < 3) {
            write(STDERR_FILENO, "kill: missing pid\n", 18);
            return 1;
        }
    }
    
    pid_t pid = (pid_t)atoi(argv[pid_idx]);
    if (kill(pid, sig) < 0) {
        printf("kill: failed to send signal to %d\n", pid);
        return 1;
    }
    
    return 0;
}
