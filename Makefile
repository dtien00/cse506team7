KLANG ?= c
KLANG_NORM := $(strip $(shell echo "$(KLANG)" | tr '[:upper:]' '[:lower:]'))
ifeq ($(KLANG_NORM),)
  KLANG_NORM := c
endif
SUBMIT_DIR ?= /submit

CROSS   := riscv64-unknown-elf-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
AR      := $(CROSS)ar
OBJCOPY := $(CROSS)objcopy
CFLAGS  := -gdwarf-4 -Wall -Werror -Os -ffreestanding -fno-builtin -nostdlib -nostdinc -isystem $(shell $(CC) -print-file-name=include) -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 -ffunction-sections -fdata-sections -fno-tree-switch-conversion -Ikernel/include -Ilibc/include
RUSTFLAGS := --target riscv64imac-unknown-none-elf -C panic=abort -C linker=$(CC) -C code-model=medium -C link-arg=-nostartfiles -C link-arg=-march=rv64imac -C link-arg=-mabi=lp64

# KERN_ASM := $(patsubst %,build/%.o,$(wildcard kernel/*.S))
KERN_ASM := $(patsubst %,build/%.o,$(shell find kernel -name '*.S'))
LIBC_OBJ := $(patsubst %,build/%.o,$(filter-out libc/crt.S,$(wildcard libc/*.c libc/*.S)))
C_BIN    := $(addprefix build/rootfs/bin/,$(shell ls -d bin/*/*.c 2>/dev/null | cut -d/ -f2 | sort -u))
RS_BIN   := $(addprefix build/rootfs/bin/,$(shell ls -d bin/*/*.rs 2>/dev/null | cut -d/ -f2 | sort -u))
USER_BIN := $(C_BIN) $(RS_BIN)

# Language-specific kernel object/library
ifeq ($(KLANG_NORM),c)
	# Exclude kernel/tests/*.c from KERN_C to avoid duplicate objects
	KERN_C   := $(patsubst %,build/%.o,$(shell find kernel -name '*.c' ! -path 'kernel/tests/*'))
	KERN_TESTS := $(patsubst %,build/%.o,$(wildcard kernel/tests/*.c))
	KERN_ALL := $(KERN_ASM) $(KERN_C) $(KERN_TESTS)
else ifeq ($(KLANG_NORM),rust)
  KERN_LIB := build/rust/riscv64imac-unknown-none-elf/debug/libsbunix.a
  KERN_ALL := $(KERN_ASM) $(KERN_LIB)
else ifeq ($(KLANG_NORM),zig)
  KERN_LIB := zig-out/lib/libsbunix.a
  KERN_ALL := $(KERN_ASM) $(KERN_LIB)
else
  $(error Unsupported KLANG='$(KLANG)'. Use one of: c, rust, zig)
endif

DISK_SIZE ?= 16
DISK_IMG  := build/disk.img

-include Makefile.local

# Sanity check: abort early if no kernel sources were found
ifeq ($(strip $(KERN_ALL)),)
  $(error No kernel object files found. Ensure kernel/ contains .c and .S source files.)
endif

all: build/kernel.elf

build/kernel/%.c.o: kernel/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Ikernel/include -c $< -o $@

build/kernel/%.S.o: kernel/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Ikernel/include -c $< -o $@

build/libc/%.c.o: libc/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Ilibc/include -c $< -o $@

build/libc/%.S.o: libc/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Ilibc/include -c $< -o $@

build/libc.a: $(LIBC_OBJ)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

# Rust kernel build
build/rust/riscv64imac-unknown-none-elf/debug/libsbunix.a: $(wildcard kernel/*.rs) Cargo.toml
	cargo build

# Zig kernel build
zig-out/lib/libsbunix.a: $(wildcard kernel/*.zig) build.zig
	zig build -Doptimize=ReleaseSafe

.SECONDEXPANSION:
$(C_BIN): build/rootfs/bin/%: $$(wildcard bin/%/*.c) build/libc/crt.S.o build/libc.a
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Ilibc/include build/libc/crt.S.o bin/$*/*.c build/libc.a -Wl,--gc-sections -o $@

$(RS_BIN): build/rootfs/bin/%: $$(wildcard bin/%/*.rs) build/libc/crt.S.o build/libc.a
	@mkdir -p $(@D)
	rustc $(RUSTFLAGS) -C link-arg=build/libc/crt.S.o -C link-arg=build/libc.a bin/$*/$*.rs -o $@

build/tarfs.o: $(USER_BIN)
	cp -a rootfs/. build/rootfs/
	tar cf build/rootfs.tar -C build/rootfs .
	cd build && $(OBJCOPY) -I binary -O elf64-littleriscv -B riscv \
		--rename-section .data=.tarfs \
		--redefine-sym _binary_rootfs_tar_start=_tarfs_start \
		--redefine-sym _binary_rootfs_tar_end=_tarfs_end \
		--redefine-sym _binary_rootfs_tar_size=_tarfs_size \
		rootfs.tar tarfs.o

build/kernel.elf: $(KERN_ALL) build/tarfs.o
	$(LD) -T kernel/kernel.ld $(KERN_ALL) build/tarfs.o -o $@

thirdparty: build/libc/crt.S.o build/libc.a
	$(MAKE) -C thirdparty install
	@rm -f build/tarfs.o
	$(MAKE) build/kernel.elf

build/tools/mkfs: tools/mkfs.c
	@mkdir -p $(@D)
	gcc -Wall -Ikernel/include -Ilibc/include -o $@ $<

$(DISK_IMG): build/tools/mkfs
	build/tools/mkfs $@ $(DISK_SIZE)

qemu: build/kernel.elf $(DISK_IMG)
	qemu-system-riscv64 -machine virt -bios default -kernel $< \
		-drive file=$(DISK_IMG),format=raw,if=none,id=hd0 \
		-device virtio-blk-pci-non-transitional,drive=hd0 \
		-device virtio-gpu-pci \
		-device virtio-net-pci-non-transitional,netdev=net0 \
		-netdev user,id=net0 \
		-nographic

audit:
	@bash tools/symbol_audit.sh

clean:
	rm -rf build zig-out .zig-cache

submit:
	@set -e; \
	STMP=$$(mktemp -d /tmp/sbunix-submit.XXXXXX); \
	cleanup() { set +e; chmod -R u+w "$$STMP" 2>/dev/null; rm -rf "$$STMP" 2>/dev/null; }; \
	trap cleanup EXIT; \
	rsync -a \
		--exclude='.git' --exclude='build/' --exclude='zig-out/' \
		--exclude='.zig-cache/' --exclude='.claude/' --exclude='thirdparty/' \
		--max-size=100K \
		. "$$STMP/"; \
	cd "$$STMP"; \
	git init -q; \
	git add -A; \
	git -c user.name=student -c user.email=student@sbunix commit -q -m "submit"; \
	rm -rf $(SUBMIT_DIR)/sbunix.2 2>/dev/null || true; \
	mv $(SUBMIT_DIR)/sbunix.1 $(SUBMIT_DIR)/sbunix.2 2>/dev/null || true; \
	mv $(SUBMIT_DIR)/sbunix $(SUBMIT_DIR)/sbunix.1 2>/dev/null || true; \
	mkdir -p $(SUBMIT_DIR)/sbunix; \
	git archive HEAD | tar -x -C $(SUBMIT_DIR)/sbunix; \
	echo "Submitted to $(SUBMIT_DIR)/sbunix"

.PRECIOUS: build/%.S.o build/%.c.o

# ********************** TEST (NOT IN ORIGINAL /shared)
test: build/disk.img
	$(MAKE) CFLAGS="$(CFLAGS) -DRUN_TESTS"
	qemu-system-riscv64 -machine virt -bios default -kernel build/kernel.elf \
		-drive file=$(DISK_IMG),format=raw,if=none,id=hd0 \
		-device virtio-blk-pci-non-transitional,drive=hd0 \
		-device virtio-gpu-pci \
		-device virtio-net-pci-non-transitional,netdev=net0 \
		-netdev user,id=net0 \
		-nographic

OS_TEST_DIR ?= tests/os-test
# Suites to build; start with process+signal since those syscalls are implemented.
# Add more (io, stdio, limits, malloc, namespace, paths) as your kernel grows.

# OS_TEST_SUITES ?= basic include limits io malloc namespace paths process pty signal stdio udp

OS_TEST_SUITES ?= include

# Compilation flags matching how user binaries are built, but without -Werror
# so os-test's own -Werror=implicit-function-declaration controls strictness.
OS_TEST_CFLAGS := -Os -ffreestanding -fno-builtin -nostdlib -nostdinc \
	-isystem $(shell $(CC) -print-file-name=include) \
	-mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 \
	-ffunction-sections -fdata-sections \
	-I$(CURDIR)/thirdparty/include \
	-I$(CURDIR)/libc/include

# crt.S.o must precede the test .o at link time so _start is defined first.
OS_TEST_LDFLAGS := $(CURDIR)/build/libc/crt.S.o $(CURDIR)/build/libc.a \
	-Wl,--gc-sections

OS_TEST_MAKE_FLAGS := \
	CC="$(CC)" \
	CC_FOR_BUILD=cc \
	CFLAGS="$(OS_TEST_CFLAGS)" \
	CFLAGS_FOR_BUILD="" \
	CPPFLAGS="" \
	CPPFLAGS_FOR_BUILD="" \
	LDFLAGS="$(OS_TEST_LDFLAGS)" \
	LDFLAGS_FOR_BUILD="" \
	EXTRA_LDFLAGS="" \
	NO_SHARED=1

$(OS_TEST_DIR):
	@echo "Cloning os-test..."
	git clone https://gitlab.com/sortix/os-test $(OS_TEST_DIR)

# Build os-test suites cross-compiled for RISC-V using the kernel's libc,
# copy the resulting ELF binaries into rootfs/os-test/, then rebuild the kernel.
# After "make qemu" you can run /os-test/<test-name> from the kernel shell.
.PHONY: os-test
os-test: build/libc/crt.S.o build/libc.a $(OS_TEST_DIR)
	cd $(OS_TEST_DIR) && $(MAKE) $(OS_TEST_MAKE_FLAGS) SUITE_LIST="$(OS_TEST_SUITES)" clean-test
	cd $(OS_TEST_DIR) && $(MAKE) $(OS_TEST_MAKE_FLAGS) SUITE_LIST="$(OS_TEST_SUITES)" all
	mkdir -p build/rootfs/os-test
	find $(OS_TEST_DIR) -type f -executable | xargs -I {} cp {} build/rootfs/os-test/
	$(MAKE) all
	cd $(OS_TEST_DIR) && $(MAKE) $(OS_TEST_MAKE_FLAGS) SUITE_LIST="$(OS_TEST_SUITES)" clean-test html

# .PHONY: os-test-html
# os-test-html: build/libc/crt.S.o build/libc.a $(OS_TEST_DIR)
# 	cd $(OS_TEST_DIR) && $(MAKE) $(OS_TEST_MAKE_FLAGS) SUITE_LIST="$(OS_TEST_SUITES)" clean-test html

# ********************** TEST (NOT IN ORIGINAL /shared)

.PHONY: all qemu clean thirdparty submit