#ifndef _SETJMP_H
#define _SETJMP_H

/* Save ra, sp, s0–s11 (14 callee-saved registers on RV64I, no FP). */
typedef long jmp_buf[14];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif
