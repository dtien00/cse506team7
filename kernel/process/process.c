
#include "process/process.h"
#include "process/swtch.h"
#include "kernel/printk.h"
#include "trap/trap.h"
#include "arch/riscv.h"
#include "fs/vfs.h"
#include "mm/vmm.h"
#include "mm/pmm.h" // for pmm_alloc_page
#include "mm/pmm.h"
#include "process/process.h"
#include "types.h"
#include "string.h"
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#include "elf/elf.h"
#include "arch/riscv.h" // for KERNBASE, PHYSTOP

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

    // Prepare argv for /bin/echo
    static char *argv[] = { "echo", "Hello, world!", NULL };
    // Set current to init so exec uses the right process
    current = init;
    if (exec("/bin/echo", argv) < 0) {
        printk("[init] exec failed for /bin/echo\n");
    }

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

int exec(const char *path, char *const argv[]) {
    // 1. Open the binary file from the filesystem
    file_t *f = vfs_open(path);
    if (!f) return -1;

    // Read ELF header into struct
    elf_header_t header;
    if (vfs_read(f, &header, sizeof(header)) != sizeof(header)) {
        vfs_close(f);
        return -1; // Failed to read header
    }

    // 2. Read and parse the binary (e.g., ELF header)
    //    - Validate ELF magic
    if (header.e_ident[0] != 0x7F ||
        header.e_ident[1] != 'E' ||
        header.e_ident[2] != 'L' ||
        header.e_ident[3] != 'F') {
        // Not an ELF file
        return -1;
    }
    // Seek to the program header table
    if (vfs_lseek(f, header.e_phoff, 0) < 0) { // 0 = SEEK_SET
        vfs_close(f);
        return -1;
    }

    // For each program header
    for (int i = 0; i < header.e_phnum; i++) {
        elf_prog_header_t ph;
        if (vfs_read(f, &ph, sizeof(ph)) != sizeof(ph)) {
            vfs_close(f);
            return -1;
        }

        if (ph.p_type != PT_LOAD)
            continue; // Only loadable segments

        // Allocate and map memory for the segment
        size_t seg_start = ph.p_vaddr & ~(PGSIZE - 1);
        size_t seg_end = ((ph.p_vaddr + ph.p_memsz + PGSIZE - 1) & ~(PGSIZE - 1));
        for (size_t va = seg_start; va < seg_end; va += PGSIZE) {
            void *page = pmm_alloc_page();
            if (!page) {
                vfs_close(f);
                return -1;
            }
            if (vmm_map(current->pagetable, va, (uintptr_t)page, PTE_W | PTE_R | PTE_X | PTE_U) < 0) {
                vfs_close(f);
                return -1;
            }
        }

        // Seek to segment offset in file
        if (vfs_lseek(f, ph.p_offset, SEEK_SET) < 0) {
            vfs_close(f);
            return -1;
        }

        // Read segment data into memory
        if (vfs_read(f, (void*)(uintptr_t)ph.p_vaddr, ph.p_filesz) != (int)ph.p_filesz) {
            vfs_close(f);
            return -1;
        }

        // Zero out the rest if p_memsz > p_filesz
        if (ph.p_memsz > ph.p_filesz) {
            memset((void*)(uintptr_t)(ph.p_vaddr + ph.p_filesz), 0, ph.p_memsz - ph.p_filesz);
        }
    }
    //    - For each loadable segment, allocate memory and load data

    // Unmap old user memory (except kernel stack)
    vmm_unmap_range(current->pagetable, KERNBASE, KERNBASE - USER_BASE);

    // Create new pagetable
    pagetable_t new_pt = vmm_create_pagetable();
    if (!new_pt) return -1;

    // (Optional) Map kernel memory into new_pt
    for (uintptr_t va = KERNBASE; va < PHYSTOP; va += PGSIZE) {
        if (vmm_map(new_pt, va, va, PTE_R | PTE_W | PTE_X) < 0) {
            // handle error
        }
    }

    // Switch to new pagetable
    vmm_free_pagetable(current->pagetable);
    current->pagetable = new_pt;

    // Copy argv to the new user stack
    // Allocate and map stack
    uintptr_t stack_top = USER_TOP;
    uintptr_t stack_base = stack_top - USTACK_SIZE;
    for (uintptr_t va = stack_base; va < stack_top; va += PGSIZE) {
        void *page = pmm_alloc_page();
        vmm_map(current->pagetable, va, (uintptr_t)page, PTE_W | PTE_R | PTE_U);
    }

    // Set SP to top of stack
    current->sp = stack_top;

    // (Optional) Copy argv to stack
    uintptr_t sp = USER_TOP; // Top of stack start position
    
    int argc = 0;
    while (argv[argc] != NULL) argc++;
    uintptr_t argv_ptrs[argc];

    // Copy strings onto stack
    for (int i = argc - 1; i>=0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= len;
        memcpy((void*)sp, argv[i], len);
        argv_ptrs[i] = sp;
    }

    // Align stack pointer to 16 bytes, in case
    sp &= ~0xF;

    // Copy argv pointers onto stack
    sp -= (argc + 1) * sizeof(uintptr_t);
    uintptr_t *user_argv = (uintptr_t*)sp;
    for (int i = 0; i <argc; i++)
        user_argv[i] = argv_ptrs[i];
    user_argv[argc] = 0; // NULL terminator '\0'

    // Configure registers for user entry (risc-v: a0 = argc, a1 = argv)
    current->sp = sp;
    current->pc = header.e_entry;
    current->regs[10] = argc;   // a0
    current->regs[11] = sp;     // a1
    

    // Set PC to ELF entry point
    current->pc = header.e_entry;

    // 5. Set up the process's registers to start at the new entry point
    //    - Set PC to entry point
    //    - Set SP to top of new user stack

    // 6. Close the file
    vfs_close(f);

    // 7. Return to user mode at the new program's entry point (does not return)
    //    - This may require a special return or context switch

    return 0; // Only reached on error
}