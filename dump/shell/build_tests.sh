#!/bin/bash

# Build script for testing libc implementation
# This script compiles test programs against the SBUnix libc

set -e

CROSS=riscv64-unknown-elf-
CC=${CROSS}gcc
CFLAGS="-gdwarf-4 -Wall -Werror -Os -ffreestanding -fno-builtin -nostdlib -nostdinc -isystem $(${CC} -print-file-name=include) -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 -ffunction-sections -fdata-sections -Ilibc/include"

echo "Building libc..."
make build/libc.a

echo ""
echo "Compiling test programs..."

for test in dump/test_*.c; do
    testname=$(basename "$test" .c)
    echo "  Compiling $testname..."
    
    ${CC} ${CFLAGS} build/libc/crt.S.o "$test" build/libc.a -Wl,--gc-sections -o "dump/${testname}" 2>&1 | tee "dump/${testname}.log"
    
    if [ ${PIPESTATUS[0]} -eq 0 ]; then
        echo "    ✓ ${testname} compiled successfully"
    else
        echo "    ✗ ${testname} failed to compile"
        echo "    Check dump/${testname}.log for errors"
    fi
done

echo ""
echo "Checking for undefined symbols..."
for log in dump/test_*.log; do
    if grep -q "undefined reference" "$log"; then
        echo "Found undefined symbols in $(basename $log):"
        grep "undefined reference" "$log" | sed 's/^/  /'
    fi
done

echo ""
echo "Build complete! Test binaries are in dump/"
echo "To run tests, boot the kernel with: make qemu"
