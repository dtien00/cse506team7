struct proc_info {
    int   pid;
    int   ppid;
    char  name[16];
    int   state;    // 0=unused,1=created,2=ready,3=running,4=blocked,5=zombie,6=stopped
    int   priority; // 0 if not implemented
    uint64_t cpu_ticks;
};