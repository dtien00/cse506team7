.global mmap
.global munmap
.global brk
.global sbrk
.global open
.global read
.global write
.global close
.global fork
.global wait
.global execve
.global getpid
.global getppid
.global getuid
.global getgid
.global lseek
.global pipe
.global dup
.global dup2
.global chdir
.global getcwd
.global stat
.global fstat
.global mkdir
.global chmod
.global fchmod
.global getdents64
.global unlink
.global kill
.global link
.global symlink
.global readlink
.global sigprocmask
.global getrusage
.global nice
.global sched_yield
.global pause
.global procinfo

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

fork:
    li a7, 2
    ecall
    ret

wait:
    li a7, 7
    ecall
    ret

execve:
    li a7, 8
    ecall
    ret

getpid:
    li a7, 9
    ecall
    ret

getppid:
    li a7, 21
    ecall
    ret

getuid:
    li a7, 174   // SYS_getuid (Linux RISC-V ABI); unimplemented — kernel returns -1
    ecall
    ret

getgid:
    li a7, 47
    ecall
    ret

lseek:
    li a7, 18
    ecall
    ret

pipe:
    li a7, 22
    ecall
    ret

dup:
    li a7, 23
    ecall
    ret

dup2:
    li a7, 33
    ecall
    ret

chdir:
    li a7, 49
    ecall
    ret

getcwd:
    li a7, 19
    ecall
    ret

stat:
    li a7, 79
    ecall
    ret

fstat:
    li a7, 80
    ecall
    ret

mkdir:
    li a7, 83
    ecall
    ret

chmod:
    li a7, 90
    ecall
    ret

fchmod:
    li a7, 91
    ecall
    ret

getdents64:
    li a7, 61
    ecall
    ret

unlink:
    li a7, 87
    ecall
    ret

kill:
    li a7, 13
    ecall
    ret

link:
    li a7, 86
    ecall
    ret

symlink:
    li a7, 88
    ecall
    ret

readlink:
    li a7, 89
    ecall
    ret

sigprocmask:
    li a7, 20
    ecall
    ret
getrusage:
    li a7, 24
    ecall
    ret

nice:
    li a7, 25
    ecall
    ret

sched_yield:
    li a7, 27
    ecall
    ret

pause:
    li a7, 28
    ecall
    ret

procinfo:
    li a7, 26
    ecall
    ret