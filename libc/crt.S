.global _start
_start:
    # Initialize the global pointer for GP-relative data access.
    # The .option directives prevent the assembler from relaxing this
    # la instruction itself (which would be circular).
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    # a0 = argc, a1 = argv, a2 = envp — placed here by the kernel via exec().
    # Publish envp to the POSIX 'environ' global before calling main so that
    # getenv() can see the kernel-provided environment from the first instruction.
    la   t0, environ
    sd   a2, 0(t0)
    # Call main(argc, argv, envp); a0/a1/a2 are already in the right registers.
    call main
    # Pass main's return value to exit().
    call exit
    # Unreachable, but halt in case exit() returns.
1:  j 1b