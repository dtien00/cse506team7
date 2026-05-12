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
.global geteuid
.global getegid
.global lseek
.global sync
.global pipe
.global dup
.global dup2
.global chdir
.global getcwd
.global stat
.global lstat
.global fstat
.global fstatat
.global mkdir
.global chmod
.global fchmod
.global fs_getdents
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
.global rmdir
.global rename
.global gettimeofday
.global alarm
.global __sigaction_raw
.global sigpending
.global sigsuspend
.global tcgetattr
.global tcsetattr
.global getpgrp
.global setpgid
.global setsid
.global getpgid
.global getsid
.global tcsetpgrp
.global tcgetpgrp
.global nanosleep
.global clock_nanosleep
.globl getitimer
.globl setitimer
.global fsync
.global fdatasync
.global access
.global sysconf
.global getrlimit
.global setrlimit
.global pathconf
.global fpathconf
.global sigreturn
.global __syscall_fcntl
.global select

mmap:
    li a7, 14
    ecall
    j __syscall_ret

munmap:
    li a7, 15
    ecall
    j __syscall_ret

brk:
    li a7, 16
    ecall
    j __syscall_ret

sbrk:
    li a7, 10
    ecall
    j __syscall_ret

open:
    li a7, 5
    ecall
    j __syscall_ret

read:
    li a7, 3
    ecall
    j __syscall_ret

write:
    li a7, 4
    ecall
    j __syscall_ret

close:
    li a7, 6
    ecall
    j __syscall_ret

fork:
    li a7, 2
    ecall
    j __syscall_ret

wait:
    li a7, 7
    ecall
    j __syscall_ret

execve:
    li a7, 8
    ecall
    j __syscall_ret

getpid:
    li a7, 9
    ecall
    j __syscall_ret

getppid:
    li a7, 21
    ecall
    j __syscall_ret

getuid:
    li a7, 174   // SYS_getuid (Linux RISC-V ABI); unimplemented — kernel returns -1
    ecall
    j __syscall_ret

getgid:
    li a7, 47
    ecall
    j __syscall_ret

geteuid:
    li a7, 175   // Linux RISC-V ABI SYS_geteuid
    ecall
    j __syscall_ret

getegid:
    li a7, 177   // Linux RISC-V ABI SYS_getegid
    ecall
    j __syscall_ret

lseek:
    li a7, 17
    ecall
    j __syscall_ret

sync:
    li a7, 20
    ecall
    j __syscall_ret

pipe:
    li a7, 22
    ecall
    j __syscall_ret

dup:
    li a7, 23
    ecall
    j __syscall_ret

dup2:
    li a7, 33
    ecall
    j __syscall_ret

chdir:
    li a7, 49
    ecall
    j __syscall_ret

getcwd:
    li a7, 27
    ecall
    j __syscall_ret

stat:
    li a7, 79
    ecall
    j __syscall_ret

lstat:
    li a7, 107
    ecall
    j __syscall_ret

fstat:
    li a7, 80
    ecall
    j __syscall_ret

fstatat:
    li a7, 102
    ecall
    j __syscall_ret

mkdir:
    li a7, 19
    ecall
    j __syscall_ret

rmdir:
    li a7, 54    /* SYS_rmdir — was 52, moved to avoid conflict with SYS_setsid */
    ecall
    j __syscall_ret

rename:
    li a7, 56
    ecall
    j __syscall_ret

chmod:
    li a7, 90
    ecall
    j __syscall_ret

fchmod:
    li a7, 91
    ecall
    j __syscall_ret

fs_getdents:
    li a7, 61
    ecall
    j __syscall_ret

unlink:
    li a7, 18
    ecall
    j __syscall_ret

kill:
    li a7, 13
    ecall
    j __syscall_ret

link:
    li a7, 86
    ecall
    j __syscall_ret

symlink:
    li a7, 88
    ecall
    j __syscall_ret

readlink:
    li a7, 89
    ecall
    j __syscall_ret

sigprocmask:
    li a7, 26
    ecall
    j __syscall_ret

getrusage:
    li a7, 28
    ecall
    j __syscall_ret

nice:
    li a7, 29
    ecall
    j __syscall_ret

procinfo:
    li a7, 30
    ecall
    j __syscall_ret

sched_yield:
    li a7, 31
    ecall
    j __syscall_ret

pause:
    li a7, 32
    ecall
    j __syscall_ret

gettimeofday:
    li a7, 34
    ecall
    j __syscall_ret

alarm:
    li a7, 37
    ecall
    j __syscall_ret

tcgetattr:
    li a7, 35
    ecall
    j __syscall_ret

__sigaction_raw:
    li a7, 38
    ecall
    j __syscall_ret

tcsetattr:
    li a7, 36
    ecall
    j __syscall_ret

sigpending:
    li a7, 39
    ecall
    j __syscall_ret

sigsuspend:
    li a7, 40
    ecall
    j __syscall_ret

getpgrp:
    li a7, 50
    ecall
    j __syscall_ret


setpgid:
    li a7, 51
    ecall
    j __syscall_ret

setsid:
    li a7, 52
    ecall
    j __syscall_ret

getpgid:
    li a7, 53
    ecall
    j __syscall_ret

getsid:
    li a7, 156
    ecall
    j __syscall_ret

tcsetpgrp:
    li a7, 41
    ecall
    j __syscall_ret

tcgetpgrp:
    li a7, 42
    ecall
    j __syscall_ret

nanosleep:
    li a7, 92
    ecall
    j __syscall_ret

clock_nanosleep:
    li a7, 93
    ecall
    j __syscall_ret

getitimer:
    li a7, 94
    ecall
    j __syscall_ret

setitimer:
    li a7, 95
    ecall
    j __syscall_ret

fsync:
    li a7, 82
    ecall
    j __syscall_ret

fdatasync:
    li a7, 83
    ecall
    j __syscall_ret

sysconf:
    li a7, 96
    ecall
    j __syscall_ret

getrlimit:
    li a7, 97
    ecall
    j __syscall_ret

setrlimit:
    li a7, 98
    ecall
    j __syscall_ret

pathconf:
    li a7, 99
    ecall
    j __syscall_ret

fpathconf:
    li a7, 100
    ecall
    j __syscall_ret

__syscall_fcntl:
    li a7, 101
    ecall
    j __syscall_ret

access:
    li a7, 43
    ecall
    j __syscall_ret

select:
    li a7, 104
    ecall
    j __syscall_ret

sigreturn:
    li a7, 103
    ecall
    j __syscall_ret

__syscall_ret:
    bgez a0, 1f
    neg t0, a0
    la t1, __errno_value
    sw t0, 0(t1)
    li a0, -1
1:
    ret
