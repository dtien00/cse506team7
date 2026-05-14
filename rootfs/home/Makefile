# Fixture for FEEDBACK_CHDIR.md reproduction.
#
# Two targets so we can separate kernel-side from compiler-side concerns:
#
#   make probe   — needs NO compiler.  Just stat's hello.c via its bare
#                  name (pdpmake does this implicitly when checking
#                  prerequisites) and runs `echo` if the prereq exists.
#                  If pdpmake prints "don't know how to make hello.c",
#                  that's the audit symptom.
#
#   make         — default target: compile hello.c → /mnt/hello.
#                  Requires /bin/cc (run `make -C thirdparty tcc-install`
#                  on the host before the kernel build).

CC ?= cc

all: /mnt/hello

/mnt/hello: hello.c
	$(CC) -o $@ hello.c

probe: hello.c
	@echo "make: prereq hello.c resolved via cwd OK"

.PHONY: all probe clean
clean:
	rm -f /mnt/hello
