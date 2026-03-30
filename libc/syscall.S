.global mmap
.global munmap
.global brk
.global sbrk
.global open
.global read
.global write
.global close

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

open:
    li a7, 5
    ecall
    ret

read:
    li a7, 3
    ecall
    ret

write:
    li a7, 4
    ecall
    ret

close:
    li a7, 6
    ecall
    ret

