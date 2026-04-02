#!/bin/bash

# Script to list all symbols provided by your libc
# Useful for debugging missing symbol errors

BUILD_DIR="build_test"
LIBC_A="${BUILD_DIR}/libc.a"

if [ ! -f "$LIBC_A" ]; then
    echo "Error: $LIBC_A not found!"
    echo "Run ./build_libc_test.sh first to create the libc archive."
    exit 1
fi

echo "=== Symbols Provided by libc.a ==="
echo ""

CROSS="riscv64-unknown-elf-"
NM="${CROSS}nm"

echo "Global functions (T = text/code):"
echo "=================================="
$NM -g "$LIBC_A" | grep " T " | awk '{print $3}' | sort | sed 's/^/  /'

echo ""
echo "Global data symbols (D/B):"
echo "=========================="
$NM -g "$LIBC_A" | grep " [DB] " | awk '{print $3}' | sort | sed 's/^/  /'

echo ""
echo "Undefined symbols (U = needed from elsewhere):"
echo "==============================================="
$NM -g "$LIBC_A" | grep " U " | awk '{print $3}' | sort | sed 's/^/  /'

echo ""
echo "=== Summary ==="
FUNC_COUNT=$($NM -g "$LIBC_A" | grep " T " | wc -l)
DATA_COUNT=$($NM -g "$LIBC_A" | grep " [DB] " | wc -l)
UNDEF_COUNT=$($NM -g "$LIBC_A" | grep " U " | wc -l)

echo "Functions provided: $FUNC_COUNT"
echo "Data symbols: $DATA_COUNT"
echo "Undefined references: $UNDEF_COUNT"

if [ $UNDEF_COUNT -gt 0 ]; then
    echo ""
    echo "⚠ Warning: libc.a has undefined references!"
    echo "These must be provided by the kernel or will cause link errors."
fi
