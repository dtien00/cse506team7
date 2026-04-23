#include "lib/uaccess.h"
#include "arch/riscv.h"
#include "errno.h"
#include "mm/vmm.h"
#include "process/process.h"

/* Reject kernel/MMIO addresses that S-mode can access without faulting.
 *
 * The rule: if the page table has a *valid* mapping for this address but
 * PTE_U is NOT set, it is a kernel or device page — reject it.  S-mode with
 * SUM=1 would silently read/write those pages, bypassing fault recovery.
 *
 * Unmapped addresses (PTE absent or PTE_V=0) are allowed through: they are
 * either demand-paged user pages (stack growth, anonymous mmap) or genuinely
 * bad pointers.  The byte loop's fault recovery handles both: the page fault
 * handler grows the stack / allocates a demand page and retries, OR it calls
 * kill_current() which (with copy_fault_recover set) redirects to &&fault. */
static int check_user_ptr(const void *uptr) {
    if (!current || !uptr) return -EFAULT;
    if ((uintptr_t)uptr >= KERNBASE) return -EFAULT;
    uint64_t *pte = vmm_walk(current->pagetable, (uintptr_t)uptr, 0);
    if (pte && (*pte & PTE_V) && !(*pte & PTE_U)) return -EFAULT;
    return 0;
}

int copy_to_user(void *dst, const void *src, size_t len) {
    if (!dst || !src) return -EFAULT;
    if (len == 0) return 0;
    if (check_user_ptr(dst) < 0) return -EFAULT;

    current->copy_fault_recover = (uintptr_t)&&fault;
    set_csr_bit(sstatus, SSTATUS_SUM);

    volatile char *d = (volatile char *)dst;
    const volatile char *s = (const volatile char *)src;
    for (size_t i = 0; i < len; i++)
        d[i] = s[i];

    clear_csr_bit(sstatus, SSTATUS_SUM);
    current->copy_fault_recover = 0;
    return 0;

fault:
    clear_csr_bit(sstatus, SSTATUS_SUM);
    current->copy_fault_recover = 0;
    return -EFAULT;
}

int copy_from_user(void *dst, const void *src, size_t len) {
    if (!dst || !src) return -EFAULT;
    if (len == 0) return 0;
    if (check_user_ptr(src) < 0) return -EFAULT;

    current->copy_fault_recover = (uintptr_t)&&fault;
    set_csr_bit(sstatus, SSTATUS_SUM);

    volatile char *d = (volatile char *)dst;
    const volatile char *s = (const volatile char *)src;
    for (size_t i = 0; i < len; i++)
        d[i] = s[i];

    clear_csr_bit(sstatus, SSTATUS_SUM);
    current->copy_fault_recover = 0;
    return 0;

fault:
    clear_csr_bit(sstatus, SSTATUS_SUM);
    current->copy_fault_recover = 0;
    return -EFAULT;
}

ssize_t strncpy_from_user(char *dst, const char *src, size_t n) {
    if (!dst || !src) return -EFAULT;
    if (n == 0) return 0;
    if (check_user_ptr(src) < 0) return -EFAULT;

    current->copy_fault_recover = (uintptr_t)&&fault;
    set_csr_bit(sstatus, SSTATUS_SUM);

    const volatile char *s = (const volatile char *)src;
    size_t i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        dst[i] = c;
        if (c == '\0') break;
    }

    clear_csr_bit(sstatus, SSTATUS_SUM);
    current->copy_fault_recover = 0;
    return (ssize_t)i;

fault:
    clear_csr_bit(sstatus, SSTATUS_SUM);
    current->copy_fault_recover = 0;
    return -EFAULT;
}
