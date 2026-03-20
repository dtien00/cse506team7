.section .text.init
.global _start
_start:
  la sp, _stack_top
  call boot
1:
  wfi
  j 1b
