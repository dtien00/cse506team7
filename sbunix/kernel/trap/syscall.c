#include <printk.h>
#include <trap/syscall.h>
#include <trap/trap.h>

void syscall_init(void) { printk("Syscall: Initialized\n"); }

void syscall_handler(trap_frame *frame) {
    // System call number is in a7
    const uint64_t syscall_num = frame->a7;

    // Arguments are in a0-a5
    const uint64_t arg0 = frame->a0;
    const uint64_t arg1 = frame->a1;
    const uint64_t arg2 = frame->a2;
    // uint64_t arg3 = frame->a3;
    // uint64_t arg4 = frame->a4;
    // uint64_t arg5 = frame->a5;

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
    // Implement when processes are up
    // process_t *p = proc_current();
    // if (p != NULL) {
    //     printk("Process %d exiting with status %d\n", p->pid, status);
    //     p->state = PROC_ZOMBIE;
    //     p->exit_status = status;
    //     // Wake up parent, schedule next process
    // }
}

pid_t sys_fork(void) {
    printk("sys_fork() not implemented\n");
    return -1;
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
    printk("sys_wait(%p) not implemented\n", status);
    return -1;
}

int sys_exec(const char *path, char **argv) {
    printk("sys_exec(%s) not implemented\n", path);
    return -1;
}

pid_t sys_getpid(void) {
    // implement when processes are up
    // process_t *p = proc_current();
    // if (p != NULL) {
    //     return p->pid;
    // }
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
