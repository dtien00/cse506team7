.section .text
.global trap_entry
.align 2
trap_entry:
    /* allocate stack space */
    addi sp, sp, -280

    // saving 31 registers (don't need to save the zero register as it never changes)
    // each register is 64 bits or 8 bytes in the RV64

    sd ra, 0(sp) //return address register
    sd sp, 8(sp)      /* save old sp (debuggable) though we overwrite sp anyway */
    sd gp, 16(sp) //global pointer for C compiler use
    sd tp, 24(sp) //thread pointer for C compiler use

    sd a0, 32(sp) //function args and return values
    sd a1, 40(sp) //function args and return values
    sd a2, 48(sp) //function args
    sd a3, 56(sp)
    sd a4, 64(sp)
    sd a5, 72(sp)
    sd a6, 80(sp)
    sd a7, 88(sp)

    sd s0, 96(sp) // frame pointer also used as a saved register
    sd s1, 104(sp) //saved registers...
    sd s2, 112(sp)
    sd s3, 120(sp)
    sd s4, 128(sp)
    sd s5, 136(sp)
    sd s6, 144(sp)
    sd s7, 152(sp)
    sd s8, 160(sp)
    sd s9, 168(sp)
    sd s10, 176(sp)
    sd s11, 184(sp)

    sd t0, 192(sp) //temporary registers
    sd t1, 200(sp)
    sd t2, 208(sp)
    sd t3, 216(sp)
    sd t4, 224(sp)
    sd t5, 232(sp)
    sd t6, 240(sp)

    // save CSRs after general purpose registers
    csrr t0, sepc
    sd   t0, 248(sp)
    csrr t0, sstatus
    sd   t0, 256(sp)
    csrr t0, scause
    sd   t0, 264(sp)
    csrr t0, stval
    sd   t0, 272(sp)

    // pass in sp which has the trap_frame struct as first argument to trap_handler
    mv   a0, sp
    call trap_handler

    //restore registers except sp

    ld ra, 0(sp)
    ld gp, 16(sp)
    ld tp, 24(sp)

    ld a0, 32(sp)
    ld a1, 40(sp)
    ld a2, 48(sp)
    ld a3, 56(sp)
    ld a4, 64(sp)
    ld a5, 72(sp)
    ld a6, 80(sp)
    ld a7, 88(sp)

    ld s0, 96(sp)
    ld s1, 104(sp)
    ld s2, 112(sp)
    ld s3, 120(sp)
    ld s4, 128(sp)
    ld s5, 136(sp)
    ld s6, 144(sp)
    ld s7, 152(sp)
    ld s8, 160(sp)
    ld s9, 168(sp)
    ld s10, 176(sp)
    ld s11, 184(sp)

    ld t0, 192(sp)
    ld t1, 200(sp)
    ld t2, 208(sp)
    ld t3, 216(sp)
    ld t4, 224(sp)
    ld t5, 232(sp)
    ld t6, 240(sp)

    // reclaim stack space
    addi sp, sp, 280

    // return from trap (this hands it over to the CPU to switch back)
    sret