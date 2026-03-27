#include "printk.h"
#include "pmm.h"
#include "vmm.h"
#include "kmalloc.h"
#include "trap.h"
#include "process.h"
#include "syscall.h"
#include "riscv.h"
#include "test.h"
#include "vfs.h"
#include "tarfs.h"
#include "tmpfs.h"


// External symbols from linker script
extern char _kernel_end[];

void run_all_tests(void) {
    mem_test_run();
    // trap_test_run();
    fs_test_run();
}

void boot(uint64_t hart_id, uint64_t dtb) {
    printk("\n");
    printk("========================================\n");
    printk("       SBUnix Operating System\n");
    printk("       RISC-V 64-bit Kernel\n");
    printk("========================================\n");
    printk("\n");

    uintptr_t kernel_start = 0x80200000;
    uintptr_t kernel_end = (uintptr_t)_kernel_end;
    size_t kernel_size = kernel_end - kernel_start;

    printk("Kernel: %p - %p (%d KB)\n",
           kernel_start, kernel_end, kernel_size / 1024);

    // Initialize physical memory manager
    // Start after kernel, manage up to 128MB
    uintptr_t mem_start = PGROUNDUP(kernel_end);
    size_t mem_size = 0x88000000 - mem_start;  // Up to 128MB total


    pmm_init(mem_start, mem_size);

    // Initialize trap handling
    trap_init();

    // Initialize virtual memory manager
    vmm_init();

    // Initialize kernel heap
    kmalloc_init();

    // Initialize process management
    proc_init();

    // // Initialize system calls
    syscall_init();

    interrupts_on();
    printk("Turned on global interrupts. Now waiting for interrupts...\n");

    // Initialize VFS and filesystems
    vfs_init();
    tarfs_init();   // Parses TAR and registers with VFS
    tmpfs_init();   // Registers tmpfs with VFS

    // Mount root filesystem (tarfs)
    vfs_mount(NULL, "/", "tarfs");

    // Mount tmpfs at /tmp
    vfs_mount(NULL, "/tmp", "tmpfs");

    printk("\n");
    printk("========================================\n");
    printk("  Kernel initialization complete!\n");
    printk("========================================\n");
    printk("\n");

    #ifdef RUN_TESTS
        run_all_tests();
    #endif

    while (1)
        asm volatile("wfi");
    
}

