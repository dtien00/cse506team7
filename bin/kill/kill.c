#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: kill <pid>\n");
        return 1;
    }
    
    int pid = atoi(argv[1]);
    int sig = SIGTERM;
    
    if (argc > 2) {
        sig = atoi(argv[2]);
    }
    
    if (kill(pid, sig) < 0) {
        printf("kill: cannot kill process %d\n", pid);
        return 1;
    }
    
    return 0;
}
