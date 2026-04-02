#!/bin/bash

# Build script for testing libc linkage
# This compiles test programs against your libc to identify missing symbols

set -e

CROSS="riscv64-unknown-elf-"
CC="${CROSS}gcc"
AR="${CROSS}ar"
CFLAGS="-gdwarf-4 -Wall -Werror -Os -ffreestanding -fno-builtin -nostdlib -nostdinc -isystem $(${CC} -print-file-name=include) -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 -ffunction-sections -fdata-sections -fno-tree-switch-conversion"

BUILD_DIR="build_test"
LIBC_DIR="../../libc"
LIBC_INCLUDE="${LIBC_DIR}/include"

echo "=== Building libc test suite ==="
echo ""

# Create build directory
mkdir -p ${BUILD_DIR}

# Step 1: Build libc objects
echo "Step 1: Compiling libc sources..."
LIBC_OBJS=""
for src in ${LIBC_DIR}/*.c; do
    if [ -f "$src" ]; then
        basename=$(basename "$src" .c)
        obj="${BUILD_DIR}/libc_${basename}.o"
        echo "  Compiling $(basename $src)..."
        ${CC} ${CFLAGS} -I${LIBC_INCLUDE} -c "$src" -o "$obj"
        LIBC_OBJS="${LIBC_OBJS} $obj"
    fi
done

# Compile assembly files
for src in ${LIBC_DIR}/*.S; do
    if [ -f "$src" ]; then
        basename=$(basename "$src" .S)
        if [ "$basename" != "crt" ]; then
            obj="${BUILD_DIR}/libc_${basename}.o"
            echo "  Assembling $(basename $src)..."
            ${CC} ${CFLAGS} -I${LIBC_INCLUDE} -c "$src" -o "$obj"
            LIBC_OBJS="${LIBC_OBJS} $obj"
        fi
    fi
done

# Build crt.o separately
echo "  Assembling crt.S..."
${CC} ${CFLAGS} -I${LIBC_INCLUDE} -c ${LIBC_DIR}/crt.S -o ${BUILD_DIR}/crt.o

# Step 2: Create libc archive
echo ""
echo "Step 2: Creating libc.a archive..."
${AR} rcs ${BUILD_DIR}/libc.a ${LIBC_OBJS}
echo "  Created ${BUILD_DIR}/libc.a"

# Step 3: Try to link minimal test
echo ""
echo "Step 3: Linking minimal test (getpid, write, fork)..."
${CC} ${CFLAGS} -I${LIBC_INCLUDE} ${BUILD_DIR}/crt.o libc_test_minimal.c ${BUILD_DIR}/libc.a -Wl,--gc-sections -o ${BUILD_DIR}/libc_test_minimal 2>&1 | tee ${BUILD_DIR}/minimal_link.log
if [ ${PIPESTATUS[0]} -eq 0 ]; then
    echo "  ✓ Minimal test linked successfully!"
else
    echo "  ✗ Minimal test failed to link - check ${BUILD_DIR}/minimal_link.log for missing symbols"
fi

# Step 4: Try to link full test
echo ""
echo "Step 4: Linking comprehensive test..."
${CC} ${CFLAGS} -I${LIBC_INCLUDE} ${BUILD_DIR}/crt.o libc_test.c ${BUILD_DIR}/libc.a -Wl,--gc-sections -o ${BUILD_DIR}/libc_test 2>&1 | tee ${BUILD_DIR}/full_link.log
if [ ${PIPESTATUS[0]} -eq 0 ]; then
    echo "  ✓ Comprehensive test linked successfully!"
else
    echo "  ✗ Comprehensive test failed to link - check ${BUILD_DIR}/full_link.log for missing symbols"
fi

# Step 5: Show summary
echo ""
echo "=== Build Summary ==="
echo "Build artifacts in: ${BUILD_DIR}/"
echo "  - libc.a: Your libc library"
echo "  - crt.o: C runtime startup"
echo "  - libc_test_minimal: Minimal test binary (if successful)"
echo "  - libc_test: Comprehensive test binary (if successful)"
echo ""
echo "To identify missing symbols, check the .log files for 'undefined reference' errors"
echo ""

# Step 6: List missing symbols if any
echo "=== Checking for missing symbols ==="
if [ -f ${BUILD_DIR}/minimal_link.log ]; then
    MISSING=$(grep "undefined reference" ${BUILD_DIR}/minimal_link.log | sed "s/.*undefined reference to \`\([^']*\)'.*/\1/" | sort -u)
    if [ -n "$MISSING" ]; then
        echo "Missing symbols in minimal test:"
        echo "$MISSING" | sed 's/^/  - /'
    else
        echo "No missing symbols in minimal test!"
    fi
fi

if [ -f ${BUILD_DIR}/full_link.log ]; then
    MISSING=$(grep "undefined reference" ${BUILD_DIR}/full_link.log | sed "s/.*undefined reference to \`\([^']*\)'.*/\1/" | sort -u)
    if [ -n "$MISSING" ]; then
        echo ""
        echo "Missing symbols in comprehensive test:"
        echo "$MISSING" | sed 's/^/  - /'
    else
        echo "No missing symbols in comprehensive test!"
    fi
fi

echo ""
echo "Done!"
