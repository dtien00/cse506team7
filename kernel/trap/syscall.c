#include "trap/syscall.h"
#include "kernel/printk.h"
#include "process/process.h"
#include "trap/trap.h"
#include "mm/vmm.h"
#include "mm/kmalloc.h"
#include "lib/string.h"

void syscall_init(void) { printk("Syscall: Initialized\n"); }

void syscall_handler(trap_frame *frame) {
    // Make live trapframe accessible to exec() and sys_fork()
    if (current) current->trapframe = frame;

    // System call number is in a7
    const uint64_t syscall_num = frame->a7;

    // Arguments are in a0-a5
    const uint64_t arg0 = frame->a0;
    const uint64_t arg1 = frame->a1;
    const uint64_t arg2 = frame->a2;
    // uint64_t arg3 = frame->a3;
    // uint64_t arg4 = frame->a4;
    // uint64_t arg5 = frame->a5;

    // exec rewrites the entire trapframe on success; handle before the
    // generic ret path so frame->a0 is not overwritten afterwards.
    if (syscall_num == SYS_exec) {
        if (exec((const char *)arg0, (char **)arg1) < 0)
            frame->a0 = (uint64_t)-1LL;
        return;
    }

    uint64_t ret = 0;

    switch (syscall_num) {
        case SYS_exit:
            sys_exit((int) arg0);
            break;

        case SYS_fork:
            ret = sys_fork();
            break;

        case SYS_read:
            ret = sys_read((int) arg0, (void *) arg1, (size_t) arg2);
            break;

        case SYS_write:
            ret = sys_write((int) arg0, (const void *) arg1, (size_t) arg2);
            break;

        case SYS_open:
            ret = sys_open((const char *) arg0, (int) arg1);
            break;

        case SYS_close:
            ret = sys_close((int) arg0);
            break;

        case SYS_wait:
            ret = sys_wait((int *) arg0);
            break;

        case SYS_exec:
            ret = sys_exec((const char *) arg0, (char **) arg1);
            break;

        case SYS_getpid:
            ret = sys_getpid();
            break;

        case SYS_sbrk:
            ret = (uint64_t) sys_sbrk((intptr_t) arg0);
            break;

        case SYS_sleep:
            ret = sys_sleep((int) arg0);
            break;

        case SYS_uptime:
            ret = sys_uptime();
            break;

        case SYS_kill:
            ret = sys_kill((pid_t) arg0);
            break;

        default:
            printk("Unknown system call: %d\n", syscall_num);
            ret = -1;
            break;
    }

    // Return value goes in a0
    frame->a0 = ret;
}

// System call implementations (stubs for now)

void sys_exit(int status) {
    proc_exit(status);
}

pid_t sys_fork(void) {
    // Allocate and initialize new process (assigns pid, stack, context)
    struct proc *child = proc_create(current->name);
    if (!child) return -1;

    // Copy parent's address space
    child->pagetable = vmm_copy_pagetable(current->pagetable);
    if (!child->pagetable) {
        proc_free(child);
        return -1; // Unable to create child address space as copy of the parent
    }

    // Copy parent's trap frame and register state to the child
    if (current->trapframe) {
        child->trapframe = (trap_frame *)kmalloc(sizeof(trap_frame));
        if (!child->trapframe) {
            vmm_free_pagetable(child->pagetable);
            proc_free(child);
            return -1;
        }
        memcpy(child->trapframe, current->trapframe, sizeof(trap_frame));
    } else {
        child->trapframe = NULL;
    }
    child->pc = current->pc;
    child->sp = current->sp;
    memcpy(child->regs, current->regs, sizeof(current->regs));
    child->satp = current->satp;

    // Parent-child linking
    child->parent = current;
    child->sibling = current->first_child;
    current->first_child = child;

    // Return child pid to parent that called fork syscall
    return child->pid;
}

ssize_t sys_read(int fd, void *buf, size_t count) {
    printk("sys_read(%d, %p, %d) not implemented\n", fd, buf, count);
    return -1;
}

ssize_t sys_write(int fd, const void *buf, size_t count) {
    // For now, just print to console if fd is 1 (stdout)
    if (fd == 1 || fd == 2) {
        const char *str = (const char *) buf;
        for (size_t i = 0; i < count; i++) {
            printk("%c", str[i]);
        }
        return count;
    }

    printk("sys_write(%d, %p, %d) not implemented\n", fd, buf, count);
    return -1;
}

int sys_open(const char *path, int flags) {
    printk("sys_open(%s, %d) not implemented\n", path, flags);
    return -1;
}

int sys_close(int fd) {
    printk("sys_close(%d) not implemented\n", fd);
    return -1;
}

pid_t sys_wait(int *status) {
    // Use the new proc_wait implementation for process reaping and blocking
    return proc_wait(status);
}

int sys_exec(const char *path, char **argv) {
    return exec(path, argv);
}


pid_t sys_getpid(void) {
    struct proc *p = proc_current();
    if (p != NULL) {
        return p->pid;
    }
    return -1;
}

void *sys_sbrk(intptr_t increment) {
    printk("sys_sbrk(%ld) not implemented\n", increment);
    return (void *) -1;
}

int sys_sleep(int seconds) {
    printk("sys_sleep(%d) not implemented\n", seconds);
    return -1;
}

int sys_uptime(void) {
    // TODO: Return system uptime in ticks
    return 0;
}

int sys_kill(pid_t pid) {
    printk("sys_kill(%d) not implemented\n", pid);
    return -1;
}
