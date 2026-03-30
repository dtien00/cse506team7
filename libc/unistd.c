#include "unistd.h"
#include "time.h"

#define SYS_exit 1

void _exit(int status) {
    register long a0 asm("a0") = status;
    register long a7 asm("a7") = SYS_exit;
    asm volatile("ecall" :: "r"(a0), "r"(a7) : "memory");
    while (1);
}

unsigned int sleep(unsigned int seconds) {
    struct timespec req;
    req.tv_sec = seconds;
    req.tv_nsec = 0;
    
    register long a0 asm("a0") = (long)&req;
    register long a1 asm("a1") = 0;
    register long a7 asm("a7") = 101;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a7) : "memory");
    
    return 0;
}

int usleep(unsigned int usec) {
    struct timespec req;
    req.tv_sec = usec / 1000000;
    req.tv_nsec = (usec % 1000000) * 1000;
    
    register long a0 asm("a0") = (long)&req;
    register long a1 asm("a1") = 0;
    register long a7 asm("a7") = 101;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a7) : "memory");
    
    return 0;
}
