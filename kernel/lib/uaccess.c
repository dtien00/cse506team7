#include "lib/uaccess.h"
#include "arch/riscv.h"
#include "errno.h"
#include "mm/vma.h"
#include "mm/vmm.h"
#include "process/process.h"

/* Validate that [uptr, uptr+size) is a legitimate user address range.
 *
 * Fast path: the range overlaps an existing VMA (already demand-paged in).
 * Slow path: the range falls below a VMA_GROWSDOWN stack VMA's current start
 *   but within its allowed growth floor (vma->end - USTACK_SIZE).  Those pages
 *   haven't been faulted in yet but are valid — copy_to/from_user will trigger
 *   demand paging via copy_fault_recover if needed. */
static int check_user_ptr(const void *uptr, size_t size) {
    if (!current || !uptr) return -EFAULT;
    uintptr_t start = (uintptr_t)uptr;
    uintptr_t end   = start + size;
    if (end < start || start >= KERNBASE) return -EFAULT;

    if (vma_find_overlap(current->vma_list, start, end)) return 0;

    /* Growsdown: start may be below the VMA's current start but within the
     * stack's maximum growth range.  Mirror the floor check in trap.c. */
    struct vm_area *gd = vma_find_growsdown(current->vma_list, start);
    if (gd) {
        if (start >= vma_stack_floor(gd) && end <= gd->end) return 0;
    }

    return -EFAULT;
}

int copy_to_user(void *dst, const void *src, size_t len) {
    if (!dst || !src) return -EFAULT;
    if (len == 0) return 0;
    if (check_user_ptr(dst, len) < 0) return -EFAULT;

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
    if (check_user_ptr(src, len) < 0) return -EFAULT;

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
    if (check_user_ptr(src, n) < 0) return -EFAULT;

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
