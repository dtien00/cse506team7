#pragma once

#include <stdint.h>

/* Userspace-visible process information (mirrors kernel struct proc_info). */
struct proc_info {
    int      pid;
    int      ppid;
    char     name[16];
    int      state;      /* 0=unused,1=created,2=ready,3=running,4=blocked,5=zombie,6=stopped */
    int      priority;
    uint64_t cpu_ticks;
};
