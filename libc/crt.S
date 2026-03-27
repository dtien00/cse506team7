.global _start
_start:
    # a0 = argc, a1 = argv — placed here by the kernel via exec().
    # Call main(argc, argv); the return value lands in a0.
    call main
    # Pass main's return value to exit().
    call exit
    # Unreachable, but halt in case exit() returns.
1:  j 1b
