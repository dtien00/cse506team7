#include <stdio.h>
#include "process/process.h"

// Declared in libc/syscall.S -> kernel SYS_procinfo (26)
int procinfo(struct proc_info *infos, int max_count);

static const char *state_name(int state) {
    switch (state) {
        case 1: return "created";
        case 2: return "ready";
        case 3: return "running";
        case 4: return "blocked";
        case 5: return "zombie";
        case 6: return "stopped";
        default: return "?";
    }
}

int main(void) {
    struct proc_info infos[64];
    int n = procinfo(infos, 64);
    if (n < 0) {
        printf("ps: procinfo failed\n");
        return 1;
    }
    printf("%-6s %-6s %-8s %-16s\n", "PID", "PPID", "STATE", "NAME");
    for (int i = 0; i < n; i++) {
        printf("%-6d %-6d %-8s %-16s\n",
               infos[i].pid,
               infos[i].ppid,
               state_name(infos[i].state),
               infos[i].name);
    }
    return 0;
}
