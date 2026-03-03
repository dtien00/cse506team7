#include "process.h"

#define MAX_PROCS 256
struct proc proctab[MAX_PROCS]; // Array of processes of fixed length
struct proc *ready_queue = NULL; // Head of ready processes
struct proc *blocked_queue = NULL; // Head of blocked processes
struct proc *current = NULL; // Current executing process

struct proc* proc_alloc(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proctab[i].state == ZOMBIE || proctab[i].pid == 0) {
            return &proctab[i];
        }
    }
    return NULL;
}

/**
 * Adds process to the end of ready queue
 * @param p Process we look to add
 */
void proc_ready(struct proc *p){
    p->state = PROC_READY;
    p->next = NULL;
    if (ready_queue == NULL){
        ready_queue = p;
    } else {
        struct proc *tail = ready_queue;
        while (tail->next) tail = tail->next;
        tail->next = p;
    }
}

/**
 * @brief Removes process from ready queue (*ready_queue)
 * This method is only used by the scheduler to remove the first process from the ready queue
 */
struct proc* proc_next_ready(void){
    struct proc *p = ready_queue;
    if (p) {
        ready_queue = p->next;
        p->next = NULL;
    }
    return p;
}

/**
 * @brief Block a process
 * This function assigns the blocked state to a process. If this process was already in the ready queue, remove it from the ready queue.
 * Then add this process to the blocked queue
 * @param p The process to block
 */
void proc_block(struct proc *p){
    p->state = PROC_BLOCKED;
    if (ready_queue && ready_queue->pid == p->pid) {
        ready_queue = ready_queue->next;
        p->next = NULL;
    } else {
        struct proc *cur = ready_queue;
        struct proc *prev = NULL;   
        // Remove process from ready queue if previously in it
        while (cur) {
            if (cur->pid == p->pid){
                prev->next = cur->next;
                break;
            }
            prev = cur;
            cur = cur->next;
        }
    }
    // Add process to blocked queue
    if (blocked_queue == NULL){
        blocked_queue = p;
    } else {
        struct proc *tail = blocked_queue;
        while (tail->next) tail = tail->next;
        tail->next = p;
    }
}

/**
 * @brief Unblock a process
 * This function assigns the unblocked state to a process. Remove it from the blocked queue, then add it to the ready queue.
 * @param p The process to unblock
 */
void proc_unblock(struct proc *p){
    if (blocked_queue && blocked_queue->pid == p->pid) {
        blocked_queue = blocked_queue->next;
    } else {
        struct proc *c = blocked_queue;
        while (c && c->next) {
            if (c->next->pid == p->pid) {
                c->next = c->next->next;
                break;
            }
            c = c->next;
        }
    }
    p->next = NULL;
    proc_ready(p);
}

/**
 * @brief Context switch
 * This function will save the current process, then load next
 */
void context_switch(struct proc *next) {
    struct proc *prev = current;

    if (prev != NULL && prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
        proc_ready(prev);
    }

    current = next;
    current->state = PROC_RUNNING;

    // TODO: Call assembly to:
    // - Save prev's registers (if prev exists)
    // - Restore next's registers
    // - Switch stack pointer to next->sp
    // - Jump to next->pc
}

/**
 * @brief Initializes process table
 * This function will clear the process table, then create the kernel/init process of PID=1.
 */
void proc_init(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        proctab[i].pid = 0;
        proctab[i].state = PROC_UNUSED;
        proctab[i].next = NULL;
    }

    struct proc *init = proc_alloc();
    init->pid = 1;
    strncpy(init->name, "init", MAX_NAME-1);
    init->state = PROC_READY;
    init->sp = /* kernel stack top */;
    init->pc = (uint64_t)init_process;

    proc_ready(init);
    current = NULL;
}

void schedule(void) {
    struct proc *next = proc_next_ready();
    if (next) {
        context_switch(next);
    }
    // Idle or halt if no runnable process
}

/**
 * @brief Returns global current process
 */
struct proc* proc_current(void){
    return current;
}

/**
 * @brief Obtain process from process table by PID
 * This function searches through our process table for a matching PID, then returns the process associated with it.
 * Else return NULL to indicate no match was found.
 * @param pid PID of the process we want to obtain
 */
struct proc* proc_find(uint32_t pid) {
    for (int i = 0; i<MAX_PROCS; i++) {
        if (proctab[i]->pid == pid) return proctab[i];
    }
    return NULL
}

/** REDUNDANT; But cited may be useful later with added complexity with syscall integration
 * @brief Voluntarily schedules a call.
 */
void proc_yield(void){
    schedule();
}

/** CONCERN: THIS ONLY SETS PID TO 2; 
 * @brief Initializes process, allocates space for it, and queues it.
 */
struct proc* proc_create(const char *name){
    struct proc *p = proc_alloc();
    if (!p) return NULL;

    static uint32_t next_pid = 2; // PID 1 is the init process
    p->pid = next_pid++;
    strncpy(p->name, name, MAX_NAME-1);
    p->state = PROC_CREATED;

    // TODO: Allocate stack, set up sp and pc

    proc_ready(p);
    return p;
}

/**
 * @brief Cleans up process memory usage
 */
void proc_free(struct proc *p) {
    if (!p) return;

    // TODO: Free allocated memory (stack, page table, etc.)
    p->pid = 0;
    p->state = PROC_UNUSED;
    p->next = NULL;
}

/**
 * @brief Assigns exit code to current process and marks it as a zombie.
 * @param code Exit code of varying values which we can use to determine outcome of process/error.
 */
void proc_exit(int code) {
    if (current) {
        current->exit_code = code;
        current->state = PROC_ZOMBIE;
        // TODO: Wake up awaiting parents, if any
        schedule(); // Switches to next process
    }
}

/**
 * @brief Kills a specific process and makes it a zombie.
 */
void proc_kill(struct proc *p) {
    if (p) p->state = PROC_ZOMBIE;
}

/**
 * @brief Block a specific process until it exits
 */
void proc_wait(struct proc *p) {
    // TODO: Block until process exits
    if (p) p->state = PROC_BLOCKED;
}

