#include "trap/syscall.h"
#include "drivers/rtc.h"
#include "arch/riscv.h"
#include "process/process.h"
#include "errno.h"
#include "kernel/printk.h"
#include "kernel/resource.h"
#include "lib/string.h"
#include "mm/kmalloc.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/vma.h"
#include "process/process.h"
#include "trap/trap.h"
#include "fs/block_cache.h"
#include "signal.h"


#define MAP_FAILED ((void *) -1)
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10

#define USER_HEAP_START 0x11000000UL
#define USER_HEAP_END   0x40000000UL

#define MAX_ARGS 64
#define KARG_MAX 256

void syscall_init(void) { printk("Syscall: Initialized\n"); }

void syscall_handler(trap_frame *frame) {
    // Make live trapframe accessible to exec() and sys_fork().
    // If an old heap snapshot exists (fork child bootstrap), drop it now.
    if (current) {
        if (current->trapframe && current->trapframe_owned)
            kfree(current->trapframe);
        current->trapframe = frame;
        current->trapframe_owned = 0;
    }

    if (current) {
        // SIGSTOP: cannot be masked, handle before any other signal (POSIX)
        uint32_t pending_stop = current->pending_signals & KERNEL_STOP_SIGS;
        if (pending_stop) {
            current->pending_signals &= ~pending_stop;
            proc_stop(current);
            // Execution resumes here after SIGCONT.
            // Re-evaluate signals — new ones may have arrived while stopped.
        }

        // Clear any pending signals whose disposition is SIG_IGN.
        // SIGKILL (bit 9) can never be ignored even if handlers[9] == SIG_IGN.
        {
            uint32_t tmp = current->pending_signals & ~current->signal_mask;
            while (tmp) {
                int s = kernel_ctz((uint32_t)(tmp));
                tmp &= tmp - 1;
                if (s != KERNEL_SIGKILL && s != KERNEL_SIGSTOP &&
                    current->handlers[s] == (void *)SIG_IGN)
                    current->pending_signals &= ~(1u << s);
            }
        }

        // Check if this process has a fatal pending signal before handling anything.
        // This catches processes that were signaled while running (not sleeping).
        // Fatal signals: filtered by signal_mask (except SIGKILL which can't be masked,
        // but we enforce that in sys_sigprocmask, so signal_mask bit 9 is always 0)
        uint32_t unmasked = current->pending_signals & ~current->signal_mask;
        if (unmasked & KERNEL_FATAL_SIGS) {
            current->pending_signals = 0;
            sys_exit(-kernel_ctz((uint32_t)(unmasked & KERNEL_FATAL_SIGS)));
            return; // unreachable
        }
    }

    // System call number is in a7
    const uint64_t syscall_num = frame->a7;

    // Arguments are in a0-a5
    const uint64_t arg0 = frame->a0;
    const uint64_t arg1 = frame->a1;
    const uint64_t arg2 = frame->a2;
    const uint64_t arg3 = frame->a3;
    const uint64_t arg4 = frame->a4;
    const uint64_t arg5 = frame->a5;

    // exec rewrites the entire trapframe on success; handle before the
    // generic ret path so frame->a0 is not overwritten afterwards.
    if (syscall_num == SYS_exec) {
        char kpath[KARG_MAX];
        char *kargv[MAX_ARGS + 1];
        int argc = 0;

        for (int i = 0; i <= MAX_ARGS; i++)
            kargv[i] = NULL;

        // Copy user args into kernel buffers (SUM window is minimal)
        set_csr_bit(sstatus, SSTATUS_SUM);

        strncpy(kpath, (const char *)arg0, sizeof(kpath) - 1);
        kpath[sizeof(kpath) - 1] = '\0';

        char **uargv = (char **)arg1;
        while (uargv && argc < MAX_ARGS - 1) {
            char *uarg = uargv[argc];
            if (!uarg) break;
            if ((uint64_t)uarg >= KERNBASE) break;

            kargv[argc] = (char *)kmalloc(KARG_MAX);
            if (!kargv[argc]) {
                clear_csr_bit(sstatus, SSTATUS_SUM);
                for (int i = 0; i < argc; i++)
                    kfree(kargv[i]);
                frame->a0 = (uint64_t)-1LL;
                return;
            }

            strncpy(kargv[argc], uarg, KARG_MAX - 1);
            kargv[argc][KARG_MAX - 1] = '\0';
            argc++;
        }
        kargv[argc] = NULL;

        clear_csr_bit(sstatus, SSTATUS_SUM);  // done with user memory

        // exec() only sees kernel pointers — safe to destroy old pagetable
        int rc = exec(kpath, kargv);
        for (int i = 0; i < argc; i++)
            kfree(kargv[i]);

        if (rc < 0)
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
            ret = sys_open((const char *) arg0, (int) arg1, (mode_t) arg2);
            break;

        case SYS_close:
            ret = sys_close((int) arg0);
            break;

        case SYS_wait:
            ret = sys_wait((int *) arg0);
            break;

        // case SYS_exec:
        //     ret = sys_exec((const char *) arg0, (char **) arg1);
        //     break;

        case SYS_getpid:
            ret = sys_getpid();
            break;

        case SYS_getppid:
            ret = sys_getppid();
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
            ret = sys_kill((pid_t) arg0, (int) arg1);
            break;

        case SYS_mmap:
            ret = (uint64_t) sys_mmap((void *) arg0, (size_t) arg1, (int) arg2,
                                      (int) arg3, (int) arg4, (off_t) arg5);
            break;

        case SYS_munmap:
            ret = sys_munmap((void *) arg0, (size_t) arg1);
            break;

        case SYS_brk:
            ret = sys_brk((void *) arg0);
            break;

        case SYS_lseek:
            ret = (uint64_t)(int64_t)sys_lseek((int) arg0, (off_t) arg1, (int) arg2);
            break;

        case SYS_unlink:
            ret = sys_unlink((const char *) arg0);
            break;

        case SYS_mkdir:
            ret = sys_mkdir((const char *) arg0, (mode_t) arg1);
            break;

        case SYS_sync:
            ret = sys_sync();
            break;

        case SYS_pipe:
            ret = sys_pipe((int *) arg0);
            break;

        case SYS_dup:
            ret = sys_dup((int) arg0);
            break;

        case SYS_dup2:
            ret = sys_dup2((int) arg0, (int) arg1);
            break;

        case SYS_tcgetattr:
            ret = sys_tcgetattr((int)arg0, (struct termios *)arg1);
            break;

        case SYS_tcsetattr:
            ret = sys_tcsetattr((int)arg0, (int)arg1, (const struct termios *)arg2);
            break;

        case SYS_waitpid:
            ret = sys_waitpid((pid_t) arg0, (int *) arg1, (int) arg2);
            break;

        case SYS_sigprocmask:
            ret = sys_sigprocmask((int)arg0, (uint32_t *)arg1, (uint32_t *)arg2);
            break;

        case SYS_chdir:
            ret = (uint64_t)sys_chdir((const char *)arg0);
            break;

        case SYS_getcwd:
            ret = (uint64_t)sys_getcwd((char *)arg0, (size_t)arg1);
            break;

        case SYS_fs_getdents:
            ret = sys_fs_getdents((int)arg0, (void *)arg1, (uint32_t)arg2);
            break;

        case SYS_getrusage:
            ret = sys_getrusage((int)arg0, (struct rusage *)arg1);
            break;

        case SYS_gettimeofday:
            ret = sys_gettimeofday((struct timeval_k *)arg0, (void *)arg1);
            break;

        case SYS_nice:
            ret = sys_nice((int)arg0);
            break;

        case SYS_yield:
            ret = sys_yield();
            break;

        case SYS_pause:
            ret = sys_pause();
            break;

        case SYS_procinfo:
            ret = sys_procinfo((struct proc_info *) arg0, (int) arg1);
            break;

        case SYS_stat:
            ret = sys_stat((char *)arg0, (void *)arg1);
            break;

        case SYS_fstat:
            ret = sys_fstat((int)arg0, (void *)arg1);
            break;

        case SYS_chmod:
            ret = sys_chmod((const char *)arg0, (mode_t)arg1);
            break;

        case SYS_fchmod:
            ret = sys_fchmod((int)arg0, (mode_t)arg1);
            break;

        case SYS_rmdir:
            ret = sys_rmdir((const char *)arg0);
            break;
            
        case SYS_alarm:
            ret = sys_alarm((unsigned int) arg0);
            break;

        case SYS_sigaction:
            ret = sys_sigaction((int)arg0,
                                (const struct sigaction *)arg1,
                                (struct sigaction *)arg2);
            break;

        case SYS_sigpending:
            ret = sys_sigpending((uint32_t *)arg0);
            break;

        case SYS_sigsuspend:
            ret = sys_sigsuspend((uint32_t *)arg0);
            break;

        case SYS_getpgrp:
            ret = sys_getpgrp();
            break;

        case SYS_setpgid:
            ret = sys_setpgid((pid_t)arg0, (pid_t)arg1);
            break;

        case SYS_setsid:
            ret = sys_setsid();
            break;

        case SYS_getpgid:
            ret = sys_getpgid((pid_t)arg0);
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
    if (!current) return -1;

    // Allocate and initialize new process (assigns pid, stack, context)
    struct proc *child = proc_create(current->name);
    if (!child) return -1;

    // Inherit parent's open files (including stdio) and bump file refs.
    for (int i = 0; i < MAX_FDS; i++) {
        child->fds.files[i] = current->fds.files[i];
        if (child->fds.files[i])
            file_ref(child->fds.files[i]);  // bump refcount if vfs supports it
    }

    // Copy parent's address space (page table + VMA list)
    child->pagetable = vmm_copy_pagetable(current->pagetable);
    if (!child->pagetable) {
        proc_free(child);
        return -1;
    }
    if (vma_copy_all(&child->vma_list, current->vma_list) != 0) {
        vmm_free_pagetable(child->pagetable);
        proc_free(child);
        return -1;
    }

    child->signal_mask = current->signal_mask; // Fork child inherits parent's signal mask
    // POSIX: child inherits parent's signal disposition table across fork
    memcpy(child->handlers, current->handlers, sizeof(current->handlers));
    child->priority    = current->priority;    /* POSIX: nice value is inherited across fork */
    // Child also inherits parent's process group and session IDs
    child->pgid = current->pgid;
    child->sid = current->sid;
    /* Child starts one level below the parent so a timer-preempted parent
     * re-enters at the same level as the child; LIFO insertion then keeps
     * the parent at the head of that level so it finishes forking remaining
     * children before any child gets scheduled.  Clamped to MLFQ_LEVELS-1. */
    child->mlfq_level  = (current->mlfq_level < MLFQ_LEVELS - 1)
                         ? current->mlfq_level + 1
                         : MLFQ_LEVELS - 1;
    child->fs = fs_struct_copy(current->fs);
    if (!child->fs) {
        vma_free_all(child->vma_list);
        vmm_free_pagetable(child->pagetable);
        proc_free(child);
        return -1;
    }

    // Copy parent's trap frame and register state to the child
    if (current->trapframe) {
        child->trapframe = (trap_frame *)kmalloc(sizeof(trap_frame));
        if (!child->trapframe) {
            vmm_free_pagetable(child->pagetable);
            proc_free(child);
            return -1;
        }
        child->trapframe_owned = 1;
        memcpy(child->trapframe, current->trapframe, sizeof(trap_frame));

        // Child returns 0 from fork
        child->trapframe->a0 = 0;

        // sepc was captured pointing AT the ecall instruction.
        // handle_exception adds +4 for the parent after syscall_handler returns,
        // so we must advance it here for the child.
        child->trapframe->sepc += 4;

        // Route child's first kernel-side wakeup through fork_ret, not proc_entry.
        // proc_entry would jump to the ELF entry point (current->pc); fork_ret
        // restores the full trapframe and srets to the instruction after ecall.
        child->context.ra = (uint64_t)fork_ret;

    } else {
        child->trapframe = NULL;
        child->trapframe_owned = 0;
    }
    child->pc = current->pc;
    child->sp = current->sp;
    memcpy(child->regs, current->regs, sizeof(current->regs));
    child->satp = current->satp;

    // Parent-child linking
    child->parent = current;
    child->sibling = current->first_child;
    current->first_child = child;

    // Child is now fully initialized and safe to run.
    proc_ready(child);

    // Return child pid to parent that called fork syscall
    return child->pid;
}

ssize_t sys_read(int fd, void *buf, size_t count) {
    if (!current || !buf)
        return -EINVAL;
    if (count == 0)
        return 0;

    char *kbuf = (char *) kmalloc(count);
    if (!kbuf)
        return -ENOMEM;

    int n = file_read(fd, kbuf, count);
    if (n > 0)
        copy_to_user(buf, kbuf, (size_t) n);

    kfree(kbuf);
    return n;
}

ssize_t sys_write(int fd, const void *buf, size_t count) {
    if (!current || !buf)
        return -EINVAL;
    if (count == 0)
        return 0;

    char *kbuf = (char *) kmalloc(count);
    if (!kbuf)
        return -ENOMEM;

    copy_from_user(kbuf, buf, count);
    int n = file_write(fd, kbuf, count);

    kfree(kbuf);
    return n;
}

// int sys_open(const char *path, int flags) {
//     (void) flags;
//     if (!current || !path) return -EINVAL;
//
//     char kpath[MAX_PATH];
//     strncpy_from_user(kpath, path, MAX_PATH - 1);
//     kpath[MAX_PATH - 1] = '\0';
//
//     return file_open(kpath);
// }

int sys_open(const char *path, int flags, mode_t mode) {
    if (!current || !path) return -EINVAL;

    char kpath[MAX_PATH];
    strncpy_from_user(kpath, path, MAX_PATH - 1);
    kpath[MAX_PATH - 1] = '\0';

    return file_open_with_flags(kpath, flags, mode);
}

int sys_close(int fd) {
    return file_close(fd);
}

int sys_pipe(int pipefd[2]) {
    if (!current || !pipefd) return -EFAULT;

    pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
    if (!p) return -ENOMEM;
    memset(p, 0, sizeof(*p));
    p->readers = 1;
    p->writers = 1;

    file_t *read_end = (file_t *)kmalloc(sizeof(file_t));
    if (!read_end) {
        kfree(p);
        return -ENOMEM;
    }
    memset(read_end, 0, sizeof(*read_end));
    read_end->type = FILE_PIPE;
    read_end->pipe = p;
    read_end->pipe_flags = PIPE_END_READ;
    read_end->ref_count = 1;

    file_t *write_end = (file_t *)kmalloc(sizeof(file_t));
    if (!write_end) {
        kfree(read_end);
        kfree(p);
        return -ENOMEM;
    }
    memset(write_end, 0, sizeof(*write_end));
    write_end->type = FILE_PIPE;
    write_end->pipe = p;
    write_end->pipe_flags = PIPE_END_WRITE;
    write_end->ref_count = 1;

    int read_fd = fd_alloc(&current->fds, read_end);
    if (read_fd < 0) {
        kfree(write_end);
        kfree(read_end);
        kfree(p);
        return -EMFILE;
    }

    int write_fd = fd_alloc(&current->fds, write_end);
    if (write_fd < 0) {
        current->fds.files[read_fd] = NULL;
        kfree(write_end);
        kfree(read_end);
        kfree(p);
        return -EMFILE;
    }

    set_csr_bit(sstatus, SSTATUS_SUM);
    pipefd[0] = read_fd;
    pipefd[1] = write_fd;
    clear_csr_bit(sstatus, SSTATUS_SUM);
    return 0;
}

int sys_dup(int oldfd) {
    if (!current) return -EBADF;

    file_t *f = f_get(current, oldfd);
    if (!f) return -EBADF;

    int newfd = fd_alloc(&current->fds, f);
    if (newfd < 0) {
        f_put(f);
        return -EMFILE;
    }

    return newfd;
}

int sys_dup2(int oldfd, int newfd) {
    if (!current) return -EBADF;
    if (newfd < 0 || newfd >= MAX_FDS) return -EBADF;

    file_t *f = f_get(current, oldfd);
    if (!f) return -EBADF;

    if (oldfd == newfd) {
        f_put(f);
        return newfd;
    }

    if (fd_get(&current->fds, newfd)) {
        fd_close(&current->fds, newfd);
    }

    current->fds.files[newfd] = f;
    return newfd;
}

int sys_tcgetattr(int fd, struct termios *termios_p) {
    if (!current || !termios_p)
        return -EINVAL;

    file_t *f = f_get(current, fd);
    if (!f)
        return -EBADF;

    if (f->type != FILE_STDIN && f->type != FILE_STDOUT) {
        f_put(f);
        return -ENOTTY;
    }

    struct termios kt;
    int rc = term_tcgetattr(&console_term, &kt);
    if (rc == 0)
        copy_to_user(termios_p, &kt, sizeof(kt));

    f_put(f);
    return rc;
}

int sys_tcsetattr(int fd, int action, const struct termios *termios_p) {
    if (!current || !termios_p)
        return -EINVAL;

    file_t *f = f_get(current, fd);
    if (!f)
        return -EBADF;

    if (f->type != FILE_STDIN && f->type != FILE_STDOUT) {
        f_put(f);
        return -ENOTTY;
    }

    struct termios kt;
    copy_from_user(&kt, termios_p, sizeof(kt));
    int rc = term_tcsetattr(&console_term, action, &kt);

    f_put(f);
    return rc;
}

pid_t sys_wait(int *status) {
    // Use the new proc_wait implementation for process reaping and blocking
    return proc_wait(status);
}

pid_t sys_waitpid(pid_t pid, int *status, int options) {
    return proc_waitpid(pid, status, options);
}

pid_t sys_getpid(void) {
    struct proc *p = proc_current();
    if (p != NULL) {
        return p->pid;
    }
    return -1;
}

pid_t sys_getppid(void) {
    struct proc *p = proc_current();
    if (p != NULL && p->parent != NULL) {
        return p->parent->pid;
    }
    return 0;   /* POSIX: init (PID 1) has no parent → return 0 */
}

// void *sys_sbrk(intptr_t increment) {
//     printk("sys_sbrk(%ld) not implemented\n", increment);
//     return (void *) -1;
// }

int sys_sleep(int seconds) {
    if (seconds <= 0) return 0;
    uint64_t wake_at = timer_ticks + (uint64_t)seconds * TICKS_PER_SEC;
    current->wake_tick = wake_at;
    proc_sleep(current);   // sleep on self; timer_handler calls proc_wakeup(p)
    // Clear wake_tick: if we were woken early by a signal, a stale non-zero
    // wake_tick would cause the timer to attempt proc_wakeup() on a running process.
    current->wake_tick = 0;
    // POSIX: return the number of seconds left to sleep if interrupted early.
    if (timer_ticks < wake_at) {
        uint64_t ticks_left = wake_at - timer_ticks;
        return (int)(ticks_left / TICKS_PER_SEC);
    }
    return 0;
}

int sys_uptime(void) {
    return (int)timer_ticks;
}

int sys_kill(pid_t pid, int sig) {
    if (sig < 0 || sig > 31) return -1;

    // POSIX: kill(-1, sig) broadcasts to all processes except init (PID 1) and the caller.
    // Returns -1 (ESRCH) if no eligible targets exist.
    if (pid == -1) {
        if (sig == 0) return 0;  // validity check — always succeeds
        int sent = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *p = &proctab[i];
            // Skip: not yet live, already dead, init, or self.
            // PROC_CREATED is excluded: the process has no signal context yet
            // (no trapframe, no user page table, not yet scheduled).
            if (p->state == PROC_UNUSED ||
                p->state == PROC_ZOMBIE  ||
                p->state == PROC_CREATED) continue;
            if (p->pid == 1 || p == current) continue;
            sent++;
            if (sig == KERNEL_SIGCONT) {
                p->pending_signals &= ~KERNEL_STOP_SIGS;
                if (p->state == PROC_STOPPED)
                    proc_unstop(p);
            } else if (sig == KERNEL_SIGSTOP) {
                p->pending_signals &= ~(1U << KERNEL_SIGCONT);
                if (p->state == PROC_READY || p->state == PROC_BLOCKED)
                    proc_stop(p);
                else
                    p->pending_signals |= KERNEL_STOP_SIGS;
            } else {
                p->pending_signals |= (1U << sig);
                if (p->state == PROC_BLOCKED &&
                    (sig == KERNEL_SIGKILL || !(p->signal_mask & (1U << sig))))
                    proc_unblock(p);
            }
        }
        return (sent > 0) ? 0 : -1;
    }

    /* POSIX: kill(0, sig) — send to every process in the caller's process group.
     * POSIX: kill(-pgid, sig) — send to every process in process group pgid. */
    if (pid == 0 || pid < -1) {
        /* kill(0, sig) requires a current process to determine the group. */
        if (pid == 0 && current == NULL) return -ESRCH;
        pid_t target_pgid = (pid == 0) ? current->pgid : (pid_t)(-pid);
        if (sig == 0) {
            /* Validity check: succeed if any live process in the group exists */
            for (int i = 0; i < MAX_PROCS; i++) {
                if (proctab[i].state != PROC_UNUSED &&
                    proctab[i].state != PROC_ZOMBIE &&
                    proctab[i].pgid == target_pgid)
                    return 0;
            }
            return -ESRCH;
        }
        int sent = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *p = &proctab[i];
            if (p->state == PROC_UNUSED ||
                p->state == PROC_ZOMBIE  ||
                p->state == PROC_CREATED) continue;
            if (p->pgid != target_pgid) continue;
            sent++;
            if (sig == KERNEL_SIGCONT) {
                p->pending_signals &= ~KERNEL_STOP_SIGS;
                if (p->state == PROC_STOPPED)
                    proc_unstop(p);
            } else if (sig == KERNEL_SIGSTOP) {
                p->pending_signals &= ~(1U << KERNEL_SIGCONT);
                if (p->state == PROC_READY || p->state == PROC_BLOCKED)
                    proc_stop(p);
                else
                    p->pending_signals |= KERNEL_STOP_SIGS;
            } else {
                p->pending_signals |= (1U << sig);
                if (p->state == PROC_BLOCKED &&
                    (sig == KERNEL_SIGKILL || !(p->signal_mask & (1U << sig))))
                    proc_unblock(p);
            }
        }
        return (sent > 0) ? 0 : -ESRCH;
    }

    struct proc *target = proc_find(pid);
    if (!target || target->state == PROC_UNUSED || target->state == PROC_ZOMBIE)
        return -1;

    // sig == 0: validity check only (POSIX), no signal sent
    if (sig == 0) return 0;
    
    // SIGCONT: resume a stopped process and clear any pending SIGSTOP
    if (sig == SIGCONT) {
        target->pending_signals &= ~KERNEL_STOP_SIGS;
        if (target->state == PROC_STOPPED)
            proc_unstop(target);
        return 0;
    }

    // SIGSTOP cannot be masked (POSIX). Act immediately when possible.
    if (sig == SIGSTOP) {
        target->pending_signals &= ~(1U << KERNEL_SIGCONT); // discard pending CONT
        if (target->state == PROC_READY || target->state == PROC_BLOCKED)
            proc_stop(target);
        else
            // Target is RUNNING (unreachable single-core) — defer via pending bit
            target->pending_signals |= KERNEL_STOP_SIGS;
        return 0;
    }

    // Set the signal bit in the target's pending bitmask
    target->pending_signals |= (1U << sig);

    // Wake a blocked (sleeping) target so it re-enters the scheduler
    // and checks pending_signals before resuming user mode.
    // Use proc_unblock() directly rather than proc_wakeup() because
    // proc_wakeup() only matches processes sleeping on a specific channel
    // (e.g. it would miss a process blocked on terminal I/O with chan != target).
    //
    // Only wake if the signal is not currently masked.  Waking for a masked
    // signal causes a spurious return from proc_sleep: the post-wakeup fatal-
    // signal check correctly skips the masked signal, proc_sleep returns
    // normally, and control reaches sigsuspend's mask-restore line.  With the
    // old (empty) mask now active the still-pending signal is no longer blocked
    // and kills the process at the next trap return — wrong behaviour when the
    // caller expected sigsuspend to stay asleep for that signal.
    // SIGKILL is included in the wake condition as a safety net even though
    // sys_sigprocmask already prevents it from being set in signal_mask.
    if (target->state == PROC_BLOCKED &&
        (sig == KERNEL_SIGKILL || !(target->signal_mask & (1U << sig))))
        proc_unblock(target);

    return 0;
}

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)fd;
    (void)offset;

    if (length == 0) {
        return MAP_FAILED;
    }

    if (!(flags & MAP_ANONYMOUS)) {
        printk("sys_mmap: file-backed mapping not supported yet\n");
        return MAP_FAILED;
    }

    struct proc *p = current;
    if (p == NULL || p->pagetable == NULL) {
        printk("sys_mmap: no current process or page table\n");
        return MAP_FAILED;
    }

    size_t pages = (length + PGSIZE - 1) / PGSIZE;
    size_t alloc_size = pages * PGSIZE;

    uintptr_t va;

    if ((flags & MAP_FIXED) && addr != NULL) {
        va = (uintptr_t)addr;
        if (va < USER_HEAP_START || va + alloc_size > USER_HEAP_END) {
            printk("sys_mmap: fixed address out of range\n");
            return MAP_FAILED;
        }
    } else {
        if (p->sz < USER_HEAP_START) {
            p->sz = USER_HEAP_START;
        }
        va = PGROUNDUP(p->sz);

        if (va + alloc_size > USER_HEAP_END) {
            printk("sys_mmap: out of virtual address space\n");
            return MAP_FAILED;
        }
    }

    uint64_t perm = PTE_U;
    if (prot & PROT_READ) perm |= PTE_R;
    if (prot & PROT_WRITE) perm |= PTE_W;

    for (size_t i = 0; i < pages; i++) {
        uintptr_t page_va = va + (i * PGSIZE);
        void* page_pa = pmm_alloc_page();

        if (page_pa == NULL) {
            for (size_t j = 0; j < i; j++) {
                uintptr_t cleanup_va = va + (j * PGSIZE);
                uint64_t *pte = vmm_walk(p->pagetable, cleanup_va, 0);
                if (pte && (*pte & PTE_V)) {
                    uintptr_t cleanup_pa = PTE_TO_PA(*pte);
                    vmm_unmap(p->pagetable, cleanup_va);
                    pmm_free_page((void*)cleanup_pa);
                }
            }
            return MAP_FAILED;
        }

        for (int j = 0; j < PGSIZE; j++) {
            ((char*)page_pa)[j] = 0;
        }

        if (vmm_map(p->pagetable, page_va, (uintptr_t)page_pa, perm) != 0) {
            pmm_free_page(page_pa);
            for (size_t j = 0; j < i; j++) {
                uintptr_t cleanup_va = va + (j * PGSIZE);
                uint64_t *pte = vmm_walk(p->pagetable, cleanup_va, 0);
                if (pte && (*pte & PTE_V)) {
                    uintptr_t cleanup_pa = PTE_TO_PA(*pte);
                    vmm_unmap(p->pagetable, cleanup_va);
                    pmm_free_page((void*)cleanup_pa);
                }
            }
            return MAP_FAILED;
        }
    }

    if (!(flags & MAP_FIXED)) {
        p->sz = va + alloc_size;
    }

    return (void*)va;
}

int sys_munmap(void *addr, size_t length) {
    if (addr == NULL || length == 0) {
        return -1;
    }

    struct proc *p = current;
    if (p == NULL || p->pagetable == NULL) {
        printk("sys_munmap: no current process or page table\n");
        return -1;
    }

    uintptr_t va = (uintptr_t)addr;
    size_t pages = (length + PGSIZE - 1) / PGSIZE;

    for (size_t i = 0; i < pages; i++) {
        uintptr_t page_va = va + (i * PGSIZE);

        uint64_t *pte = vmm_walk(p->pagetable, page_va, 0);
        if (pte && (*pte & PTE_V)) {
            uintptr_t pa = PTE_TO_PA(*pte);
            vmm_unmap(p->pagetable, page_va);
            pmm_free_page((void*)pa);
        }
    }

    return 0;
}

int sys_brk(void *addr) {
    struct proc *p = current;
    if (p == NULL || p->pagetable == NULL) {
        printk("sys_brk: no current process or page table\n");
        return -1;
    }

    if (p->sz < USER_HEAP_START) {
        p->sz = USER_HEAP_START;
    }

    if (addr == NULL) {
        return (int)p->sz;
    }

    uintptr_t new_break = (uintptr_t)addr;

    if (new_break < USER_HEAP_START || new_break > USER_HEAP_END) {
        return -1;
    }

    uintptr_t old_break = p->sz;

    if (new_break > old_break) {
        uintptr_t start = PGROUNDUP(old_break);
        uintptr_t end = PGROUNDUP(new_break);
        size_t pages = (end - start) / PGSIZE;

        for (size_t i = 0; i < pages; i++) {
            uintptr_t va = start + (i * PGSIZE);
            void* pa = pmm_alloc_page();

            if (pa == NULL) {
                for (size_t j = 0; j < i; j++) {
                    uintptr_t cleanup_va = start + (j * PGSIZE);
                    uint64_t *pte = vmm_walk(p->pagetable, cleanup_va, 0);
                    if (pte && (*pte & PTE_V)) {
                        uintptr_t cleanup_pa = PTE_TO_PA(*pte);
                        vmm_unmap(p->pagetable, cleanup_va);
                        pmm_free_page((void*)cleanup_pa);
                    }
                }
                return -1;
            }

            for (int j = 0; j < PGSIZE; j++) {
                ((char*)pa)[j] = 0;
            }

            if (vmm_map(p->pagetable, va, (uintptr_t)pa, PTE_R | PTE_W | PTE_U) != 0) {
                pmm_free_page(pa);
                for (size_t j = 0; j < i; j++) {
                    uintptr_t cleanup_va = start + (j * PGSIZE);
                    uint64_t *pte = vmm_walk(p->pagetable, cleanup_va, 0);
                    if (pte && (*pte & PTE_V)) {
                        uintptr_t cleanup_pa = PTE_TO_PA(*pte);
                        vmm_unmap(p->pagetable, cleanup_va);
                        pmm_free_page((void*)cleanup_pa);
                    }
                }
                return -1;
            }
        }
    } else if (new_break < old_break) {
        uintptr_t start = PGROUNDUP(new_break);
        uintptr_t end = PGROUNDUP(old_break);
        size_t pages = (end - start) / PGSIZE;

        for (size_t i = 0; i < pages; i++) {
            uintptr_t va = start + (i * PGSIZE);
            uint64_t *pte = vmm_walk(p->pagetable, va, 0);
            if (pte && (*pte & PTE_V)) {
                uintptr_t pa = PTE_TO_PA(*pte);
                vmm_unmap(p->pagetable, va);
                pmm_free_page((void*)pa);
            }
        }
    }

    p->sz = new_break;
    return 0;
}

void *sys_sbrk(intptr_t increment) {
    struct proc *p = current;
    if (p == NULL || p->pagetable == NULL) {
        printk("sys_sbrk: no current process or page table\n");
        return (void *) -1;
    }

    if (p->sz < USER_HEAP_START) {
        p->sz = USER_HEAP_START;
    }

    void* old_break = (void*)p->sz;

    if (increment == 0) {
        return old_break;
    }

    uintptr_t new_break = p->sz + increment;

    if (new_break < USER_HEAP_START || new_break > USER_HEAP_END) {
        return (void *) -1;
    }

    if (sys_brk((void*)new_break) != 0) {
        return (void *) -1;
    }

    return old_break;
}

off_t sys_lseek(int fd, off_t offset, int whence) {
    return (off_t) file_lseek(fd, (size_t) offset, whence);
}

int sys_unlink(const char *path) {
    if (!current) return -1;

    set_csr_bit(sstatus, SSTATUS_SUM);
    char kpath[MAX_PATH];
    strncpy(kpath, path, MAX_PATH - 1);
    kpath[MAX_PATH - 1] = '\0';
    clear_csr_bit(sstatus, SSTATUS_SUM);

    return vfs_unlink(kpath);
}

int sys_mkdir(const char *path, mode_t mode) {
    if (!current) return -1;

    set_csr_bit(sstatus, SSTATUS_SUM);
    char kpath[MAX_PATH];
    strncpy(kpath, path, MAX_PATH - 1);
    kpath[MAX_PATH - 1] = '\0';
    clear_csr_bit(sstatus, SSTATUS_SUM);

    /* vfs_mkdir uses vfs_find_mount() prefix matching, so it needs a
     * normalized absolute path.  dentry_normalize_path handles ".", "..",
     * double slashes, and relative-to-cwd expansion. */
    char resolved[MAX_PATH];
    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              kpath, resolved, sizeof(resolved)) < 0)
        return -ENAMETOOLONG;

    return vfs_mkdir(resolved, mode);
}

int sys_sync(void) {
    return block_cache_flush();
}

int sys_sigprocmask(int how, const uint32_t *set, uint32_t *oldset) {
    if (!current) return -1;

    // set and oldset are user-space pointers — must enable SUM before dereferencing.
    set_csr_bit(sstatus, SSTATUS_SUM);

    if (oldset)
        *oldset = current->signal_mask;

    uint32_t val = 0;
    if (set)
        val = *set;

    clear_csr_bit(sstatus, SSTATUS_SUM);

    if (!set) return 0;
    uint32_t new_mask = current->signal_mask;

    switch (how) {
        case 0: new_mask |=  val; break;   // SIG_BLOCK   = 0
        case 1: new_mask &= ~val; break;   // SIG_UNBLOCK = 1
        case 2: new_mask  =  val; break;   // SIG_SETMASK = 2
        default: return -1;
    }

    // SIGKILL (9) and SIGSTOP (19) can never be masked — POSIX requirement
    new_mask &= ~((1U << KERNEL_SIGKILL) | (1U << KERNEL_SIGSTOP));
    current->signal_mask = new_mask;
    return 0;
}

int sys_chdir(const char *path) {
    if (!current || !path) return -EINVAL;

    char kpath[MAX_PATH];
    strncpy_from_user(kpath, path, MAX_PATH - 1);
    kpath[MAX_PATH - 1] = '\0';

    return vfs_chdir(kpath);   // VFS owns everything below
}

char *sys_getcwd(char *buf, size_t size) {
    if (!current || !buf || size == 0 || !current->fs) return NULL;

    // Build the absolute path from the cwd dentry chain
    char kbuf[MAX_PATH];
    if (fs_struct_get_cwd(current->fs, kbuf, MAX_PATH) < 0)
        return NULL;
    printk("[sys_getcwd] pid=%d cwd='%s' cwd_dentry=%p\n",
           current->pid, kbuf, current->fs->cwd);

    size_t cwd_len = strlen(kbuf);
    if (cwd_len + 1 > size) return NULL;   // user buffer too small

    copy_to_user(buf, kbuf, cwd_len + 1);

    return buf;
}

// Fills user buffer `dirp` with fs_dent records for the directory
// referenced by `fd`.  Returns bytes written, 0 at end-of-directory, -errno.
int sys_fs_getdents(int fd, void *dirp, uint32_t count)
{
    if (!current || !dirp) return -EINVAL;

    file_t *f = f_get(current, fd);
    if (!f || f->type != FILE_VNODE) {
        if (f)
            f_put(f);
        return -EBADF;
    }
    if (!f->dentry || !f->vnode || f->vnode->type != VNODE_DIR) {
        f_put(f);
        return -ENOTDIR;
    }

    if (count == 0) {
        f_put(f);
        return 0;
    }

    char *kbuf = kmalloc(count);
    if (!kbuf) {
        f_put(f);
        return -ENOMEM;
    }

    uint32_t entries_read = 0;
    int n = vfs_readdir(f->dentry, (uint32_t)f->offset,
                        kbuf, count, &entries_read);

    if (n > 0) {
        f->offset += entries_read;
        copy_to_user(dirp, kbuf, (size_t) n);
    }

    kfree(kbuf);
    f_put(f);
    return n;
}
/* ------------------------------------------------------------------ */
/* sys_getrusage                                                        */
/* ------------------------------------------------------------------ */

int sys_getrusage(int who, struct rusage *usage) {
    if (!current || !usage) return -1;

    uint64_t ticks;
    if (who == RUSAGE_SELF) {
        ticks = current->utime_ticks;
    } else if (who == RUSAGE_CHILDREN) {
        ticks = current->children_utime;
    } else {
        return -1;
    }

    struct rusage ku;
    ku.ru_utime.tv_sec  = ticks / TICKS_PER_SEC;
    ku.ru_utime.tv_usec = (ticks % TICKS_PER_SEC) * (1000000ULL / TICKS_PER_SEC);
    ku.ru_stime.tv_sec  = 0;
    ku.ru_stime.tv_usec = 0;

    set_csr_bit(sstatus, SSTATUS_SUM);
    *usage = ku;
    clear_csr_bit(sstatus, SSTATUS_SUM);

    return 0;
}

/* ------------------------------------------------------------------ */
/* sys_gettimeofday                                                     */
/* ------------------------------------------------------------------ */

int sys_gettimeofday(struct timeval_k *tv, void *tz) {
    if (tv) {
        uint64_t ns = rtc_read_ns();
        struct timeval_k ktv;
        ktv.tv_sec  = ns / 1000000000ULL;
        ktv.tv_usec = (ns % 1000000000ULL) / 1000ULL;
        set_csr_bit(sstatus, SSTATUS_SUM);
        *tv = ktv;
        clear_csr_bit(sstatus, SSTATUS_SUM);
    }
    if (tz) {
        // struct timezone = { int tz_minuteswest; int tz_dsttime; }
        // Always return UTC (both fields zero).
        int zeros[2] = {0, 0};
        set_csr_bit(sstatus, SSTATUS_SUM);
        __builtin_memcpy(tz, zeros, sizeof(zeros));
        clear_csr_bit(sstatus, SSTATUS_SUM);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* sys_nice                                                             */
/* ------------------------------------------------------------------ */

/*
 * sys_nice(increment) — adjust the calling process's scheduling priority.
 *
 * Convention (matching struct proc.priority): 0 = highest, 19 = lowest,
 * which mirrors the Unix nice-value range [0, 19] used by this kernel.
 *
 * Positive increment lowers priority (background work).
 * Negative increment raises priority; clamped to 0 regardless of privilege
 * because this kernel has no uid concept.
 *
 * POSIX semantics:
 *   nice(0)  — returns current priority without changing it.
 *   Return value is the new priority on success, -1 if current == NULL.
 *
 * Overflow safety: intermediate arithmetic is widened to long (64-bit on
 * RISC-V) before clamping so that INT_MIN/INT_MAX arguments cannot wrap.
 */
int sys_nice(int increment) {
    if (!current) return -1;
    long new_prio = (long)current->priority + (long)increment;
    if (new_prio < 0)  new_prio = 0;
    if (new_prio > 19) new_prio = 19;
    int old_prio = current->priority;
    current->priority = (int)new_prio;
    /* Lowered scheduling priority — yield if a better process is waiting. */
    if ((int)new_prio > old_prio)
        proc_preempt_if_needed();
    return (int)new_prio;
}

/*
 * sys_yield() — voluntarily relinquish the CPU.
 *
 * Calls proc_yield(), which re-enqueues the calling process as READY at
 * its current MLFQ level (priority preserved, no demotion) and switches
 * to the scheduler.  The process resumes normally once the scheduler
 * selects it again.
 *
 * If no other process is runnable, proc_yield() re-selects the same
 * process immediately — one extra context switch but no functional issue.
 *
 * POSIX sched_yield() always returns 0.
 */
int sys_yield(void) {
    proc_yield();
    return 0;
}

/*
 * sys_pause() — suspend the calling process until any signal is delivered.
 *
 * Uses &current->pending_signals as the sleep channel so the timer's
 * proc_wakeup(p) call (which uses the proc pointer as channel) cannot
 * accidentally wake us. Only proc_unblock() from sys_kill wakes us.
 *
 * proc_sleep() checks SIGSTOP and KERNEL_FATAL_SIGS on wakeup and
 * handles them (stop or exit) before returning here.
 *
 * POSIX: always returns -1 (EINTR).
 */
int sys_pause(void) {
    // If a non-fatal, unmasked signal is already pending, return immediately.
    // (Fatal ones were caught at syscall entry; we'd never reach here if they were set.)
    uint32_t check = current->pending_signals & ~current->signal_mask
                     & ~KERNEL_FATAL_SIGS & ~KERNEL_STOP_SIGS;
    if (check) return -1;
    proc_sleep(&current->pending_signals);
    return -1;
}

/*
 * A POSIX-conforming ps should show at minimum: PID, PPID, state, priority (if implemented), and name
 * NOTE: As of now, PIDs are strictly incremented only. If we look to implement PID recycling, this would risk PID races,
 * and after a certain PID value limit reached, would look for the lowest available PID value.
*/
int sys_procinfo(struct proc_info *infos, int max_count) {
    int count = 0;
    // infos points into user space; enable SUM so S-mode can write U-bit pages.
    set_csr_bit(sstatus, SSTATUS_SUM);
    for (int i = 0; i < MAX_PROCS && count < max_count; i++) {
        struct proc *p = &proctab[i];
        if (p->state == PROC_UNUSED) continue;
        infos[count].pid       = p->pid;
        infos[count].ppid      = p->parent ? p->parent->pid : 0;
        strncpy(infos[count].name, p->name, 15);
        infos[count].state     = (int)p->state;
        infos[count].priority  = p->priority;
        infos[count].cpu_ticks = p->utime_ticks;
        count++;
    }
    clear_csr_bit(sstatus, SSTATUS_SUM);
    return count;
}


// trap/trapsyscall.c

int sys_stat(const char *upath, struct stat *ustat)
{
    if (!current || !upath || !ustat) return -EINVAL;

    // 1. Copy path from user
    char kpath[MAX_PATH];
    strncpy_from_user(kpath, upath, MAX_PATH - 1);
    kpath[MAX_PATH - 1] = '\0';

    // 2. Do kernel-side stat (no user memory access)
    struct stat kst;
    int ret = file_stat(kpath, &kst);
    if (ret != 0) return ret;

    // 3. Copy result back to user
    copy_to_user(ustat, &kst, sizeof(struct stat));

    return 0;
}

int sys_fstat(int fd, struct stat *ustat)
{
    if (!current || !ustat) return -EINVAL;

    // 1. Kernel-side stat by fd (no user memory needed)
    struct stat kst;
    int ret = file_fstat(fd, &kst);
    if (ret != 0) return ret;

    // 2. Copy result to user
    copy_to_user(ustat, &kst, sizeof(struct stat));
    return 0;
}

int sys_chmod(const char *upath, mode_t mode) {
    if (!current || !upath) return -EINVAL;

    char kpath[MAX_PATH];
    strncpy_from_user(kpath, upath, MAX_PATH - 1);
    kpath[MAX_PATH - 1] = '\0';

    char resolved[MAX_PATH];
    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              kpath, resolved, sizeof(resolved)) < 0)
        return -ENAMETOOLONG;

    return vfs_chmod(resolved, mode);
}

int sys_fchmod(int fd, mode_t mode) {
    if (!current) return -EINVAL;

    file_t *f = fd_get(&current->fds, fd);
    if (!f) return -EBADF;
    if (f->type != FILE_VNODE || !f->vnode) return -EBADF;

    return vfs_fchmod(f->vnode, mode);
}

int sys_rmdir(const char *path) {
    if (!current || !path) return -EINVAL;

    char kpath[MAX_PATH];
    strncpy_from_user(kpath, path, MAX_PATH - 1);
    kpath[MAX_PATH - 1] = '\0';

    char resolved[MAX_PATH];
    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              kpath, resolved, sizeof(resolved)) < 0)
        return -ENAMETOOLONG;

    return vfs_rmdir(resolved);
}

unsigned int sys_alarm(unsigned int seconds) {
    uint64_t old = current->alarm_tick;

    if (seconds == 0) {
        // alarm(0) cancels any pending alarm
        current->alarm_tick = 0;
    } else {
        current->alarm_tick = timer_ticks + (uint64_t)seconds * TICKS_PER_SEC;
    }

    // Return seconds remaining from previous alarm, or 0 if none / already fired
    if (old == 0 || old <= timer_ticks)
        return 0;
    // Ceiling division: round up so partial ticks count as a full second
    return (unsigned int)((old - timer_ticks + TICKS_PER_SEC - 1) / TICKS_PER_SEC);
}

int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    if (!current) return -1;
    if (sig < 1 || sig >= 32) return -EINVAL;
    // SIGKILL and SIGSTOP cannot be caught or ignored — POSIX
    if (sig == SIGKILL || sig == SIGSTOP) return -EINVAL;

    set_csr_bit(sstatus, SSTATUS_SUM);

    // Return previous disposition in oldact if the caller wants it
    if (oldact != NULL) {
        oldact->sa_handler = (sighandler_t)current->handlers[sig];
        oldact->sa_mask    = 0;
        oldact->sa_flags   = 0;
    }

    // Install new disposition — only SIG_DFL (0) and SIG_IGN (1) accepted;
    // custom function pointers require a signal-return trampoline (BLOCKED-8).
    if (act != NULL) {
        sighandler_t h = act->sa_handler;
        if (h != SIG_DFL && h != SIG_IGN) {
            clear_csr_bit(sstatus, SSTATUS_SUM);
            return -EINVAL;
        }
        current->handlers[sig] = (void *)h;
    }

    clear_csr_bit(sstatus, SSTATUS_SUM);
    return 0;
}

int sys_sigpending(uint32_t *set) {
    if (!current || !set) return -EINVAL;

    // set is a userspace pointer — enable SUM so the kernel can write to it,
    // matching the same pattern used by sys_sigprocmask.
    set_csr_bit(sstatus, SSTATUS_SUM);
    *set = current->pending_signals & current->signal_mask;
    clear_csr_bit(sstatus, SSTATUS_SUM);

    return 0;
}

/*
 * sys_sigsuspend() — atomically replace signal mask and suspend until a
 * signal is delivered.
 *
 * POSIX: atomically installs *mask as the process signal mask, then blocks
 * until any signal whose action would terminate the process or invoke a
 * signal-catching function is delivered.  The original mask is always
 * restored before returning, and the call always returns -1 (EINTR).
 *
 * Implementation notes:
 *   - The SUM bit is only needed to read *mask (a userspace pointer).
 *     Writing current->signal_mask is a kernel-struct write; no SUM needed.
 *   - After installing the new mask a signal that was previously blocked
 *     may now be pending.  If we called proc_sleep() unconditionally nobody
 *     would ever call proc_wakeup() for that already-set bit, causing the
 *     process to sleep forever.  We therefore run the same signal-delivery
 *     checks that proc_sleep() performs on wakeup *before* sleeping, so that
 *     pre-pending signals are handled identically to signals that arrive
 *     while we are blocked.
 */
int sys_sigsuspend(const uint32_t *mask) {
    if (!current || !mask) return -EINVAL;

    /* Read new mask from userspace (SUM needed for U-bit page access). */
    set_csr_bit(sstatus, SSTATUS_SUM);
    uint32_t new_mask = *mask;
    clear_csr_bit(sstatus, SSTATUS_SUM);

    /* Atomically install new mask; save old mask for restoration on return. */
    uint32_t old_mask = current->signal_mask;
    current->signal_mask = new_mask;

    /*
     * Pre-sleep signal sweep — mirrors proc_sleep()'s post-wakeup checks.
     * This handles signals that were pending-but-blocked under old_mask and
     * are now unmasked by new_mask, which proc_wakeup() would never fire for.
     */

    /* Step 1: SIGSTOP — cannot be masked; highest priority. */
    uint32_t pending_stop = current->pending_signals & KERNEL_STOP_SIGS;
    if (pending_stop) {
        current->pending_signals &= ~pending_stop;
        proc_stop(current);
        /* Resumes here after SIGCONT is delivered. */
    }

    /* Step 2: Clear SIG_IGN-disposed pending signals (except SIGKILL/SIGSTOP). */
    {
        uint32_t tmp = current->pending_signals & ~current->signal_mask;
        while (tmp) {
            int s = kernel_ctz((uint32_t)tmp);
            tmp &= tmp - 1;
            if (s != KERNEL_SIGKILL && s != KERNEL_SIGSTOP &&
                current->handlers[s] == (void *)SIG_IGN)
                current->pending_signals &= ~(1u << s);
        }
    }

    /* Step 3: Fatal signals already pending under the new mask — terminate. */
    uint32_t unmasked = current->pending_signals & ~current->signal_mask;
    if (unmasked & KERNEL_FATAL_SIGS) {
        current->pending_signals = 0;
        proc_exit(-kernel_ctz((uint32_t)(unmasked & KERNEL_FATAL_SIGS)));
        /* unreachable */
    }

    /* Step 4: Non-fatal pending signals → return EINTR immediately.
     * Mirrors sys_pause()'s early-return pattern.  The signal bits remain in
     * pending_signals and are visible to the caller via sigpending().
     * The original mask is restored first so the signals are correctly masked
     * or unmasked for subsequent operations. */
    uint32_t nonfatal = current->pending_signals & ~current->signal_mask
                        & ~KERNEL_FATAL_SIGS & ~KERNEL_STOP_SIGS;
    if (!nonfatal) {
        /* No immediately actionable signals: block until one is delivered. */
        proc_sleep(&current->pending_signals);
        /* proc_sleep() handles SIGSTOP, SIG_IGN, and fatal signals on wakeup. */
    }

    /* Restore original mask — POSIX requires this before sigsuspend returns. */
    current->signal_mask = old_mask;
    return -1; /* POSIX: always returns -1 (errno = EINTR) */
}

pid_t sys_getpgrp(void) {
    if (!current) return -1;
    return current->pgid;
}

/*
 * sys_setpgid — move process pid into process group pgid.
 *
 * POSIX constraints enforced:
 *   - Caller may only act on itself (pid == 0 or pid == current->pid) or a
 *     direct child that has not yet called exec (we can't track the exec bit
 *     cheaply, so we allow it on direct children unconditionally).
 *   - A session leader cannot change its process group.
 *   - The target pgid must already exist within the same session, OR pgid == 0
 *     (which means "create a new group with target->pid as the pgid").
 */
int sys_setpgid(pid_t pid, pid_t pgid) {
    if (!current) return -1;

    struct proc *target;
    if (pid == 0 || pid == current->pid) {
        target = current;
    } else {
        target = proc_find(pid);
        if (!target || target->state == PROC_UNUSED || target->state == PROC_ZOMBIE)
            return -ESRCH;
        /* Caller must be the direct parent */
        if (target->parent != current)
            return -ESRCH;
    }

    /* POSIX: cannot change the process group of a session leader */
    if (target->sid == target->pid)
        return -EPERM;

    /* pgid == 0 means "join a new group with the target's own pid" */
    pid_t new_pgid = (pgid == 0) ? target->pid : pgid;

    /* If joining an existing group, it must exist within the same session */
    if (new_pgid != target->pid) {
        int found = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *p = &proctab[i];
            if (p->state != PROC_UNUSED && p->state != PROC_ZOMBIE &&
                p->pgid == new_pgid && p->sid == target->sid) {
                found = 1;
                break;
            }
        }
        if (!found) return -EPERM;
    }

    target->pgid = new_pgid;
    return 0;
}

/*
 * sys_setsid — create a new session.
 *
 * The calling process must not already be a process group leader.
 * Returns the new session id (== current->pid) on success, or -EPERM.
 */
pid_t sys_setsid(void) {
    if (!current) return -1;
    /* POSIX: process group leader cannot call setsid */
    if (current->pgid == current->pid)
        return -EPERM;
    current->sid  = current->pid;
    current->pgid = current->pid;
    return current->sid;
}

/*
 * sys_getpgid — return the process group id of process pid.
 * pid == 0 means the calling process.
 */
pid_t sys_getpgid(pid_t pid) {
    if (!current) return -1;
    if (pid == 0 || pid == current->pid)
        return current->pgid;
    struct proc *p = proc_find(pid);
    if (!p || p->state == PROC_UNUSED || p->state == PROC_ZOMBIE)
        return -ESRCH;
    return p->pgid;
}