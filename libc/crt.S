.global _start
_start:
    # Initialize the global pointer for GP-relative data access.
    # The .option directives prevent the assembler from relaxing this
    # la instruction itself (which would be circular).
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    # a0 = argc, a1 = argv — placed here by the kernel via exec().
    # Call main(argc, argv); the return value lands in a0.
    call main
    # Pass main's return value to exit().
    call exit
    # Unreachable, but halt in case exit() returns.
1:  j 1b
