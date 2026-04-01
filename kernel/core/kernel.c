#include "kernel/printk.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/kmalloc.h"
#include "trap/trap.h"
#include "process/process.h"
#include "trap/syscall.h"
#include "arch/riscv.h"
#include "fs/vfs.h"
#include "fs/tarfs.h"
#include "fs/tmpfs.h"
#include "kernel/types.h"
#include "drivers/drivers.h"
#include "kernel/console.h"

#include "tests/test.h"
#include "tty/term.h"

// External symbols from linker script
extern char _kernel_end[];

void run_all_tests(void) {
    mem_test_run();
    // trap_test_run();
    fs_test_run();
    
    sched_test_run();
    sched_timeslice_test_run();
    sched_priority_test_run();
    sched_fairness_test_run();
    sched_idle_test_run();
    proc_test_run();

    // printk("%d passed, %d failed\n", passed, failed);
}

void boot(uint64_t hart_id, uint64_t dtb) {

    // initialize console
    console_init(); // this will fall back to using the sbi driver by default
    printk("console: using %s\n", console_active_name());

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

    // initialize drivers once memory is mapped
    drivers_init();

    //initialize the terminal
    term_global_init();

    // switch to uart once drivers are initialized
    if (console_set_driver("uart") == 0)
        printk("console: switched to uart\n");

    // VFS and filesystems must be ready before proc_init because
    // proc_init calls exec(), which calls vfs_open().
    // Initialize VFS and filesystems
    vfs_init();
    tarfs_init();
    tmpfs_init();
    // Mount root filesystem (tarfs)
    vfs_mount(NULL, "/", "tarfs");
    // Mount tmpfs at /tmp
    vfs_mount(NULL, "/tmp", "tmpfs");

    syscall_init();

    // set up process 0 aka swapper as idle loop
    scheduler_init();
    // proc_init sets up the init process and loads its ELF via exec().
    proc_init();

    interrupts_on();
    printk("Turned on global interrupts. Now waiting for interrupts...\n");

    printk("\n");
    printk("========================================\n");
    printk("  Kernel initialization complete!\n");
    printk("========================================\n");
    printk("\n");

    #ifdef RUN_TESTS
        run_all_tests();
    #else
        // Hand off to the scheduler; runs the init process (pid 1).
        schedule();
        printk("Kernel panic.\n"); // should never reach here as schedule will always default to its own idle loop
        while (1)
            asm volatile("wfi");
    #endif
}

