.section .text
.global trap_entry
.align 2
trap_entry:
    // Swap sp and sscratch.
    // If from U-mode: sp=user_sp, sscratch=kernel_sp → after swap, sp=kernel_sp
    // If from S-mode: sscratch=0 (by convention), sp=kernel_sp → after swap, sp=0 (bad)
    // So we check: if sscratch == 0, we came from S-mode and skip the swap.
    csrrw sp, sscratch, sp   // swap sp ↔ sscratch

    // Now check if we came from kernel (old sscratch was 0)
    bnez sp, 1f              // if sp != 0, we got the kernel sp → from U-mode
    csrr sp, sscratch        // from S-mode: restore sp from sscratch (it has the kernel sp)
1:
    /* allocate stack space */
    addi sp, sp, -280

    // saving 31 registers (don't need to save the zero register as it never changes)
    // each register is 64 bits or 8 bytes in the RV64

    sd ra, 0(sp) //return address register
    // Save t0 first (its original value) so we can use it as scratch below.
    sd t0, 192(sp)
    // For U-mode traps the user sp was swapped into sscratch at entry; read
    // it back so the trapframe shows the real user sp, not the kernel sp.
    // For S-mode traps sscratch holds the kernel sp (restored at line 13),
    // which is the correct sp value to record for an S-mode trap.
    csrr t0, sscratch
    sd t0, 8(sp)

    // clear sscratch — kernel is running in S-mode
    csrw  sscratch, zero

    // Save remaining GPRs
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

    // t0 already saved at 192 above; continue with remaining temporaries
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

    // --- Return from trap ---

    // Restore sepc (trap.c may have modified frame->sepc, e.g. +4 for ecall)
    ld t0, 248(sp)
    csrw sepc, t0

    // Restore sstatus
    ld t0, 256(sp)
    csrw sstatus, t0

    // Check SPP bit to decide return mode
    // t0 still has sstatus value
    andi t0, t0, (1 << 8)     // isolate SPP bit
    bnez t0, .Lkernel_return   // SPP=1 → returning to S-mode

    // --- U-mode return ---
    // Save kernel stack top in sscratch for next trap from U-mode
    addi t0, sp, 280
    csrw sscratch, t0

    // Restore all GPRs except sp and t0
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

    // Load user sp last — after this instruction sp no longer addresses the
    // trapframe, but all other registers are already fully restored.e
    ld sp, 8(sp)
    sret

.Lkernel_return:
    // Explicitly re-enforce the sscratch==0 invariant before we hand control
    csrw  sscratch, zero
    // S-mode return: restore all GPRs, keep sscratch=0
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

    // Reclaim stack frame and return to S-mode
    addi sp, sp, 280
    sret