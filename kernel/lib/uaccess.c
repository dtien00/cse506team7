#include "lib/uaccess.h"
#include "arch/riscv.h"
#include "errno.h"
#include "mm/vma.h"
#include "mm/vmm.h"
#include "process/process.h"

static int check_user_ptr(const void *uptr, size_t size, uint32_t required_prot) {
    if (!current || !uptr) return -EFAULT;
    uintptr_t start = (uintptr_t)uptr;
    uintptr_t end   = start + size;
    if (end < start || end > MAXVA) return -EFAULT;
    return vma_range_accessible(current->vma_list, start, size, required_prot) ? 0 : -EFAULT;
}

int copy_to_user(void *dst, const void *src, size_t len) {
    if (!dst || !src) return -EFAULT;
    if (len == 0) return 0;
    if (check_user_ptr(dst, len, VMA_PROT_WRITE) < 0) return -EFAULT;

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
    if (check_user_ptr(src, len, VMA_PROT_READ) < 0) return -EFAULT;

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

    current->copy_fault_recover = (uintptr_t)&&fault;
    set_csr_bit(sstatus, SSTATUS_SUM);

    const volatile char *s = (const volatile char *)src;
    size_t i;
    for (i = 0; i < n; i++) {
        // Validate incrementally so short strings near the top of the user
        // stack do not fail just because the full 'n' range crosses KERNBASE.
        if (check_user_ptr((const void *)(src + i), 1, VMA_PROT_READ) < 0)
            goto fault;
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
