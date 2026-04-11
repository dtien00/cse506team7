#include "trap/syscall.h"
#include "arch/riscv.h"
#include "errno.h"
#include "kernel/printk.h"
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
#define MAX_PATH    256

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

        // Check if this process has a fatal pending signal before handling anything.
        // This catches processes that were signaled while running (not sleeping).
        // Fatal signals: filtered by signal_mask (except SIGKILL which can't be masked,
        // but we enforce that in sys_sigprocmask, so signal_mask bit 9 is always 0)
        uint32_t unmasked = current->pending_signals & ~current->signal_mask;
        if (unmasked & KERNEL_FATAL_SIGS) {
            current->pending_signals = 0;
            sys_exit(-1);
            return; // unreachable — sys_exit calls proc_exit which never returns
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
            ret = sys_open((const char *) arg0, (int) arg1);
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
            // printk("  [kernel] syscall_handler: dispatching SYS_sleep(%d)\n", (int)arg0);
            ret = sys_sleep((int) arg0);
            // printk("  [kernel] syscall_handler: SYS_sleep returned %d\n", ret);
            break;

        case SYS_uptime:
            // printk("  [kernel] syscall_handler: dispatching SYS_uptime\n");
            ret = sys_uptime();
            // printk("  [kernel] syscall_handler: SYS_uptime returned %d\n", ret);
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
    file_t *f = fd_get(&current->fds, fd);
    if (!f) return -1;
    if (f->type == FILE_PIPE)
        return file_read(f, buf, count);
    set_csr_bit(sstatus, SSTATUS_SUM);
    int n = file_read(f, buf, count);
    clear_csr_bit(sstatus, SSTATUS_SUM);
    return n;
}

ssize_t sys_write(int fd, const void *buf, size_t count) {
    if (count == 0) return 0;
    if (!current || fd < 0 || fd >= MAX_FDS) return -EBADF;

    file_t *f = fd_get(&current->fds, fd);
    if (!f) return -EBADF;

    /* Console path — active until vfs_write has a console driver */
    if (f->type == FILE_STDOUT || f->type == FILE_STDIN) {
        set_csr_bit(sstatus, SSTATUS_SUM);
        const char *str = (const char *)buf;
        for (size_t i = 0; i < count; i++)
            printk_char(str[i]);
        clear_csr_bit(sstatus, SSTATUS_SUM);
        return (ssize_t)count;
    }
    if (f->type == FILE_PIPE)
        return file_write(f, buf, count);
    set_csr_bit(sstatus, SSTATUS_SUM);
    ssize_t n = file_write(f, buf, count);
    clear_csr_bit(sstatus, SSTATUS_SUM);
    return n;
}

int sys_open(const char *path, int flags) {
    if (!current || !path) return -EINVAL;

    // SUM open only for the copy — nothing else
    set_csr_bit(sstatus, SSTATUS_SUM);
    char kpath[MAX_PATH];
    strncpy(kpath, path, MAX_PATH - 1);
    kpath[MAX_PATH - 1] = '\0';
    clear_csr_bit(sstatus, SSTATUS_SUM);

    // Resolve path relative to cwd before doing anything else
    char cwd[MAX_PATH];
    if (fs_struct_get_cwd(current->fs, cwd, MAX_PATH) < 0) return -ENAMETOOLONG;
    char resolved[MAX_PATH];
    if (path_resolve(cwd, kpath, resolved) < 0) return -ENAMETOOLONG;

    // If O_CREAT is set and the file doesn't exist, create it
    if (flags & O_CREAT) {
        dentry_t *d = vfs_lookup(resolved);
        if (!d) {
            if (vfs_create(resolved, 0644) < 0) return -ENOENT;
        } else {
            dentry_put(d);
        }
    }

    file_t *f = file_open(resolved);
    if (!f) return -ENOENT;

    f->flags = flags;

    // O_TRUNC: discard existing content
    if ((flags & O_TRUNC) && (flags & (O_WRONLY | O_RDWR))) {
        if (f->vnode && f->vnode->mount && f->vnode->mount->fs_type->ops.truncate)
            f->vnode->mount->fs_type->ops.truncate(f->vnode);
    }

    // O_APPEND: start writes at end of file
    if (flags & O_APPEND)
        f->offset = f->vnode ? f->vnode->size : 0;

    return fd_alloc(&current->fds, f);
}

int sys_close(int fd) {
    file_t *f = fd_get(&current->fds, fd);
    if (!f) return -1;
    fd_close(&current->fds, fd);
    return 0;
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

    file_t *f = fd_get(&current->fds, oldfd);
    if (!f) return -EBADF;

    file_ref(f);
    int newfd = fd_alloc(&current->fds, f);
    if (newfd < 0) {
        file_close(f);
        return -EMFILE;
    }

    return newfd;
}

int sys_dup2(int oldfd, int newfd) {
    if (!current) return -EBADF;
    if (newfd < 0 || newfd >= MAX_FDS) return -EBADF;

    file_t *f = fd_get(&current->fds, oldfd);
    if (!f) return -EBADF;

    if (oldfd == newfd) return newfd;

    if (fd_get(&current->fds, newfd)) {
        fd_close(&current->fds, newfd);
    }

    file_ref(f);
    current->fds.files[newfd] = f;
    return newfd;
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
    uint64_t wake = timer_ticks + (uint64_t)seconds * TICKS_PER_SEC;
    // printk("  [kernel] sys_sleep: seconds=%d timer_ticks=%lu wake_tick=%lu\n",
    //       seconds, (unsigned long)timer_ticks, (unsigned long)wake);
    current->wake_tick = wake;
    // printk("  [kernel] sys_sleep: calling proc_sleep (going to sleep)\n");
    proc_sleep(current);   // sleep on self; timer_handler calls proc_wakeup(p)
    // printk("  [kernel] sys_sleep: woke up, timer_ticks=%lu\n", (unsigned long)timer_ticks);
    return 0;
}

int sys_uptime(void) {
    // printk("  [kernel] sys_uptime: timer_ticks=%lu\n", (unsigned long)timer_ticks);
    return (int)timer_ticks;
}

int sys_kill(pid_t pid, int sig) {
    if (sig < 0 || sig > 31) return -1;

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
    if (target->state == PROC_BLOCKED)
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
    if (!current) return -1;

    file_t *f = fd_get(&current->fds, fd);
    if (!f) {
        printk("sys_lseek: invalid fd %d\n", fd);
        return -1;
    }

    // Don't allow seeking on stdin/stdout/stderr
    if (f->type == FILE_STDIN || f->type == FILE_STDOUT) {
        return -1;
    }

    return (off_t)file_lseek(f, (size_t)offset, whence);
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

    char cwd[MAX_PATH];
    if (fs_struct_get_cwd(current->fs, cwd, MAX_PATH) < 0) return -ENAMETOOLONG;
    char resolved[MAX_PATH];
    if (path_resolve(cwd, kpath, resolved) < 0) return -ENAMETOOLONG;

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

    set_csr_bit(sstatus, SSTATUS_SUM);
    char kpath[MAX_PATH];
    strncpy(kpath, path, MAX_PATH - 1);
    kpath[MAX_PATH - 1] = '\0';
    clear_csr_bit(sstatus, SSTATUS_SUM);

    return vfs_chdir(kpath);
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

    set_csr_bit(sstatus, SSTATUS_SUM);
    memcpy(buf, kbuf, cwd_len + 1);
    clear_csr_bit(sstatus, SSTATUS_SUM);

    return buf;
}

// Fills user buffer `dirp` with fs_dent records for the directory
// referenced by `fd`.  Returns bytes written, 0 at end-of-directory, -errno.
int sys_fs_getdents(int fd, void *dirp, uint32_t count)
{
    if (!current || !dirp) return -EINVAL;

    file_t *f = fd_get(&current->fds, fd);
    if (!f || f->type != FILE_VNODE)              return -EBADF;
    if (!f->vnode || f->vnode->type != VNODE_DIR) return -ENOTDIR;

    if (count == 0) return 0;

    char *kbuf = kmalloc(count);
    if (!kbuf) return -ENOMEM;

    uint32_t entries_read = 0;
    int n = vfs_readdir(f->vnode, (uint32_t)f->offset,
                        kbuf, count, &entries_read);

    if (n > 0) {
        f->offset += entries_read;

        set_csr_bit(sstatus, SSTATUS_SUM);
        memcpy(dirp, kbuf, (size_t)n);
        clear_csr_bit(sstatus, SSTATUS_SUM);
    }

    kfree(kbuf);
    return n;
}
