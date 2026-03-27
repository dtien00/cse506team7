.global mmap
.global munmap
.global brk
.global sbrk

mmap:
    li a7, 14
    ecall
    ret

munmap:
    li a7, 15
    ecall
    ret

brk:
    li a7, 16
    ecall
    ret

sbrk:
    li a7, 10
    ecall
    ret
