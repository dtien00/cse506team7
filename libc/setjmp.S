.global setjmp
setjmp:
    # Save callee-saved registers + ra + sp into jmp_buf (a0)
    sd  ra,   0*8(a0)
    sd  sp,   1*8(a0)
    sd  s0,   2*8(a0)
    sd  s1,   3*8(a0)
    sd  s2,   4*8(a0)
    sd  s3,   5*8(a0)
    sd  s4,   6*8(a0)
    sd  s5,   7*8(a0)
    sd  s6,   8*8(a0)
    sd  s7,   9*8(a0)
    sd  s8,  10*8(a0)
    sd  s9,  11*8(a0)
    sd  s10, 12*8(a0)
    sd  s11, 13*8(a0)
    li  a0, 0
    ret

.global longjmp
longjmp:
    # Restore all saved registers from jmp_buf (a0), return val (a1, min 1)
    ld  ra,   0*8(a0)
    ld  sp,   1*8(a0)
    ld  s0,   2*8(a0)
    ld  s1,   3*8(a0)
    ld  s2,   4*8(a0)
    ld  s3,   5*8(a0)
    ld  s4,   6*8(a0)
    ld  s5,   7*8(a0)
    ld  s6,   8*8(a0)
    ld  s7,   9*8(a0)
    ld  s8,  10*8(a0)
    ld  s9,  11*8(a0)
    ld  s10, 12*8(a0)
    ld  s11, 13*8(a0)
    seqz a0, a1       # if val == 0 force return value to 1
    add  a0, a0, a1
    ret
