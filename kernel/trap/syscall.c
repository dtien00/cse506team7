#include "trap/syscall.h"
#include "kernel/printk.h"
#include "process/process.h"
#include "trap/trap.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "arch/riscv.h"
#include "fs/file.h"
#include "fs/vfs.h"
#include "fs/tmpfs.h"
#include "fs/block_cache.h"

// File open flags
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

// Seek whence values
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define MAP_FAILED ((void *) -1)
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10

#define USER_HEAP_START 0x10000000UL
#define USER_HEAP_END   0x40000000UL

void syscall_init(void) { printk("Syscall: Initialized\n"); }

void syscall_handler(trap_frame *frame) {
    // System call number is in a7
    const uint64_t syscall_num = frame->a7;

    // Arguments are in a0-a5
    const uint64_t arg0 = frame->a0;
    const uint64_t arg1 = frame->a1;
    const uint64_t arg2 = frame->a2;
    const uint64_t arg3 = frame->a3;
    const uint64_t arg4 = frame->a4;
    const uint64_t arg5 = frame->a5;

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
            ret = sys_lseek((int) arg0, (off_t) arg1, (int) arg2);
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
    // Allocate and initialize new process (assigns pid, stack, context)
    struct proc *child = proc_create(current->name);
    if (!child) return -1;

    // Copy parent's address space
    child->pagetable = vmm_copy_pagetable(current->pagetable);
    if (!child->pagetable) {
        proc_free(child);
        return -1;
    }
    
    // Copy parent's VMA list
    if (vma_copy_all(&child->vma_list, current->vma_list) != 0) {
        vmm_free_pagetable(child->pagetable);
        proc_free(child);
        return -1;
    }
    
    // Copy sz field
    child->sz = current->sz;

    // Copy parent's file descriptor table (shares open files)
    if (fd_copy(&child->fd_table, &current->fd_table) != 0) {
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
    if (!current) return -1;

    // Get the file from the fd table
    file_t *f = fd_get(&current->fd_table, fd);
    if (!f) {
        printk("sys_read: invalid fd %d\n", fd);
        return -1;
    }

    // Handle stdin (not implemented yet)
    if (f->type == FILE_STDIN) {
        printk("sys_read: stdin not implemented\n");
        return -1;
    }

    // For regular files, use vfs_read
    // buf is a user-space pointer, enable SUM bit to access it
    set_csr_bit(sstatus, SSTATUS_SUM);
    int bytes_read = vfs_read(f, buf, count);
    clear_csr_bit(sstatus, SSTATUS_SUM);

    return bytes_read;
}

ssize_t sys_write(int fd, const void *buf, size_t count) {
    if (!current) return -1;

    // Get the file from the fd table
    file_t *f = fd_get(&current->fd_table, fd);
    if (!f) {
        printk("sys_write: invalid fd %d\n", fd);
        return -1;
    }

    // Handle stdout/stderr - print to console
    if (f->type == FILE_STDOUT) {
        const char *str = (const char *) buf;
        for (size_t i = 0; i < count; i++) {
            printk("%c", str[i]);
        }
        return count;
    }

    // For regular files, use vfs_write
    // buf is a user-space pointer, enable SUM bit to access it
    set_csr_bit(sstatus, SSTATUS_SUM);
    int bytes_written = vfs_write(f, buf, count);
    clear_csr_bit(sstatus, SSTATUS_SUM);

    return bytes_written;
}

int sys_open(const char *path, int flags) {
    if (!current) return -1;

    // path is a user-space pointer, enable SUM bit to access it
    set_csr_bit(sstatus, SSTATUS_SUM);
    
    // Try to open existing file
    file_t *f = vfs_open(path, flags);
    
    // If file doesn't exist and O_CREAT is set, create it
    if (!f && (flags & O_CREAT)) {
        mode_t mode = 0644;  // Default permissions (rw-r--r--)
        if (vfs_create(path, mode) == 0) {
            // Try to open again after creation
            f = vfs_open(path, flags);
        }
    }
    
    clear_csr_bit(sstatus, SSTATUS_SUM);

    if (!f) {
        printk("sys_open: failed to open %s\n", path);
        return -1;
    }

    // Handle O_TRUNC - truncate file to zero length
    if ((flags & O_TRUNC) && (flags & (O_WRONLY | O_RDWR))) {
        if (f->type == FILE_TMPFS && f->vnode && f->vnode->fs_data) {
            tmpfs_inode_t *inode = (tmpfs_inode_t *)f->vnode->fs_data;
            inode->size = 0;
            f->vnode->size = 0;
        }
        // TODO: Add truncate support for ext2
    }
    
    // Handle O_APPEND - set offset to end of file
    if (flags & O_APPEND) {
        f->offset = f->vnode->size;
    }

    // Allocate a file descriptor
    int fd = fd_alloc(&current->fd_table, f);
    if (fd < 0) {
        vfs_close(f);
        return -1;
    }

    return fd;
}

int sys_close(int fd) {
    if (!current) return -1;

    // Validate fd
    if (fd < 0 || fd >= MAX_FDS) {
        return -1;
    }

    // Get the file to check if it exists
    file_t *f = fd_get(&current->fd_table, fd);
    if (!f) {
        return -1;
    }

    // Close the file descriptor
    fd_close(&current->fd_table, fd);
    return 0;
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

// void *sys_sbrk(intptr_t increment) {
//     printk("sys_sbrk(%ld) not implemented\n", increment);
//     return (void *) -1;
// }

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
    
    file_t *f = fd_get(&current->fd_table, fd);
    if (!f) {
        printk("sys_lseek: invalid fd %d\n", fd);
        return -1;
    }
    
    // Don't allow seeking on stdin/stdout/stderr
    if (f->type == FILE_STDIN || f->type == FILE_STDOUT) {
        return -1;
    }
    
    return vfs_lseek(f, offset, whence);
}

int sys_unlink(const char *path) {
    if (!current) return -1;
    
    set_csr_bit(sstatus, SSTATUS_SUM);
    int ret = vfs_unlink(path);
    clear_csr_bit(sstatus, SSTATUS_SUM);
    
    return ret;
}

int sys_mkdir(const char *path, mode_t mode) {
    if (!current) return -1;
    
    set_csr_bit(sstatus, SSTATUS_SUM);
    int ret = vfs_mkdir(path, mode);
    clear_csr_bit(sstatus, SSTATUS_SUM);
    
    return ret;
}

int sys_sync(void) {
    return block_cache_flush();
}
