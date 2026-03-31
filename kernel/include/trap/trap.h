#pragma once
#include "types.h"

// struct to store trap-related data
typedef struct trap_frame {
    // General-purpose registers — offsets match trap.S sd instructions
    uint64_t ra; //   0
    uint64_t sp; //   8
    uint64_t gp; //  16
    uint64_t tp; //  24
    uint64_t a0; //  32
    uint64_t a1; //  40
    uint64_t a2; //  48
    uint64_t a3; //  56
    uint64_t a4; //  64
    uint64_t a5; //  72
    uint64_t a6; //  80
    uint64_t a7; //  88
    uint64_t s0; //  96
    uint64_t s1; // 104
    uint64_t s2; // 112
    uint64_t s3; // 120
    uint64_t s4; // 128
    uint64_t s5; // 136
    uint64_t s6; // 144
    uint64_t s7; // 152
    uint64_t s8; // 160
    uint64_t s9; // 168
    uint64_t s10; // 176
    uint64_t s11; // 184
    uint64_t t0; // 192
    uint64_t t1; // 200
    uint64_t t2; // 208
    uint64_t t3; // 216
    uint64_t t4; // 224
    uint64_t t5; // 232
    uint64_t t6; // 240
    // CSRs saved after GPRs
    uint64_t sepc; // 248
    uint64_t sstatus; // 256
    uint64_t scause; // 264
    uint64_t stval; // 272
} trap_frame;

// called to initialize trap handler
void trap_init(void);

void page_fault_handler(struct trap_frame *);

// called to handle all traps
void trap_handler(struct trap_frame *);

// Timer interrupt handler
void timer_handler(void);

// helpers to turn on/off interrupts globally
void interrupts_on(void);
void interrupts_off(void);

// helpers to enable certain interrupt types using the SIE.
// Pass in one of the constants defined above to turn on corresponding interrupts
void interrupts_enable(uint64_t bit);
void interrupts_disable(uint64_t bit);
