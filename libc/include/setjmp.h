#ifndef _SETJMP_H
#define _SETJMP_H

/* Save ra, sp, s0–s11 (14 callee-saved registers on RV64I, no FP). */
typedef long jmp_buf[14];

/*
 * sigjmp_buf needs room for the same register set plus a savemask flag and
 * a stub signal-mask word.  16 longs gives two extra slots for those fields
 * without changing the layout visible to setjmp.S assembly stubs.
 */
typedef long sigjmp_buf[16];

int      setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

/* CX: signal-safe variants */
int      sigsetjmp(sigjmp_buf env, int savemask);
_Noreturn void siglongjmp(sigjmp_buf env, int val);

#endif
