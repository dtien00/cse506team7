#ifndef _SIGFRAME_H
#define _SIGFRAME_H
struct sigframe {
    uint64_t sepc;          /* PC to resume after sigreturn */
    uint64_t ra;
    uint64_t sp;
    uint64_t gp;
    uint64_t tp;
    uint64_t t[7];          /* t0–t6 */
    uint64_t s[12];         /* s0–s11 */
    uint64_t a[8];          /* a0–a7 */
    uint32_t saved_mask;    /* signal_mask in effect before dispatch */
    uint32_t sig;           /* signal number passed to handler in a0 */
};
#endif // _SIGFRAME_H