#include "process/process.h"
#include "process/swtch.h"
#include "kernel/printk.h"
#include "trap/trap.h"
#include "arch/riscv.h"

/**
 * Default entry point for new kernel threads.
 * When swtch restores ra to this function, the new process starts here.
 */
static void proc_entry(void) {
    interrupts_on();
    printk("[proc %d] started\n", current->pid);
    while (1)
        asm volatile("wfi");
}

struct proc* proc_alloc(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proctab[i].state == PROC_ZOMBIE || proctab[i].pid == 0) {
            return &proctab[i];
        }
    }
    return NULL;
}

void proc_init(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        proctab[i].pid = 0;
        proctab[i].state = PROC_UNUSED;
        proctab[i].next = NULL;
    }

    struct proc *init = proc_alloc();
    int idx = init - proctab;
    init->pid = 1;
    strncpy(init->name, "init", MAX_NAME - 1);
    init->state = PROC_READY;
    init->context.sp = (uint64_t)&kstacks[idx][KSTACK_SIZE];
    init->context.ra = (uint64_t)proc_entry;

    proc_ready(init);
    current = NULL;
}

struct proc* proc_current(void) {
    return current;
}

struct proc* proc_find(pid_t pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proctab[i].pid == pid) return &proctab[i];
    }
    return NULL;
}

struct proc* proc_create(const char *name) {
    struct proc *p = proc_alloc();
    if (!p) return NULL;

    int idx = p - proctab;
    static uint32_t next_pid = 2;
    p->pid = next_pid++;
    strncpy(p->name, name, MAX_NAME - 1);
    p->state = PROC_CREATED;
    p->context.sp = (uint64_t)&kstacks[idx][KSTACK_SIZE];
    p->context.ra = (uint64_t)proc_entry;

    // User-mode processes allocates a trap_frame to save/restore user registers
    // p->trapframe = (trap_frame *)kmalloc(sizeof(trap_frame));
    // memset(p->trapframe, 0, sizeof(trap_frame));

    proc_ready(p);
    return p;
}

void proc_free(struct proc *p) {
    if (!p) return;
    p->pid = 0;
    p->state = PROC_UNUSED;
    p->next = NULL;
}

void proc_exit(int code) {
    if (!current) return;

    // Wake parent if it exists (guard against init having no parent)
    if (current->parent)
        proc_wakeup(current->parent);

    // Reparent children to init (pid 1) to prevent orphans
        struct proc *initproc = proc_find(1);
        struct proc *child = current->first_child;
        while (child) {
            struct proc *next_sib = child->sibling;
            child->parent = initproc;
            child->sibling = initproc->first_child;
            initproc->first_child = child;
            child = next_sib;
        }
    current->first_child = NULL;
    // free_user_pgtb() // Placeholder for when user-mode exists

    current->exit_code = code;
    current->state = PROC_ZOMBIE;
    schedule();
}

void proc_kill(struct proc *p) {
    if (p) p->state = PROC_ZOMBIE;
}

void proc_wait(struct proc *p) {
    if (p) p->state = PROC_BLOCKED;
}

void proc_dump(void) {
    printk("\nProcess table:\n");
    printk("PID  State      Name\n");
    printk("---  ---------  ----------------\n");

    for (int i = 0; i < MAX_PROCS; i++) {
        if (proctab[i].state != PROC_UNUSED) {
            const char *state_str;
            switch (proctab[i].state) {
                case PROC_CREATED:  state_str = "CREATED"; break;
                case PROC_READY:    state_str = "READY"; break;
                case PROC_RUNNING:  state_str = "RUNNING"; break;
                case PROC_BLOCKED:  state_str = "BLOCKED"; break;
                case PROC_ZOMBIE:   state_str = "ZOMBIE"; break;
                default:            state_str = "UNKNOWN"; break;
            }
            printk("%3d  %-9s  %s\n", proctab[i].pid, state_str, proctab[i].name);
        }
    }
}

void proc_start_user(struct proc *p, uint64_t entry, uint64_t user_sp) {
    // SPP=0 means sret goes to U-mode
    // SPIE=1 means interrupts re-enable after sret
    write_csr(sstatus, (read_csr(sstatus) & ~SSTATUS_SPP) | SSTATUS_SPIE);
    
    // User entry point
    write_csr(sepc, entry);
    
    // Set sscratch to this process's kernel stack top
    int idx = p - proctab;
    write_csr(sscratch, (uint64_t)&kstacks[idx][KSTACK_SIZE]);
    
    // Switch to user page table
    vmm_switch(p->pagetable);
    
    // Set user stack pointer and sret
    // (done in assembly — load user sp, then sret)
}
