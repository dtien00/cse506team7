#!/bin/bash

# Script to verify what's packaged in your tarfs
# This helps ensure your shell and utilities are actually in the kernel image

set -e

BUILD_DIR="../../build"
TARFS="${BUILD_DIR}/rootfs.tar"

echo "=== Verifying TARFS Contents ==="
echo ""

if [ ! -f "$TARFS" ]; then
    echo "Error: $TARFS not found!"
    echo "You need to build the kernel first: cd ../.. && make"
    exit 1
fi

echo "Contents of rootfs.tar:"
echo "======================"
tar -tvf "$TARFS"

echo ""
echo "=== Critical Files Check ==="

# Check for shell
if tar -tf "$TARFS" | grep -q "bin/sh"; then
    echo "✓ Shell binary found: bin/sh"
else
    echo "✗ Shell binary MISSING: bin/sh"
    echo "  This is critical - your OS cannot start an interactive session!"
fi

# Check for basic utilities
REQUIRED_BINS=("ls" "cat" "echo" "pwd" "mkdir" "rm" "cp")
for bin in "${REQUIRED_BINS[@]}"; do
    if tar -tf "$TARFS" | grep -q "bin/$bin"; then
        echo "✓ Utility found: bin/$bin"
    else
        echo "✗ Utility MISSING: bin/$bin"
    fi
done

# Check for init script
if tar -tf "$TARFS" | grep -q "etc/rc"; then
    echo "✓ Init script found: etc/rc"
else
    echo "✗ Init script MISSING: etc/rc"
fi

echo ""
echo "=== Binary Size Check ==="
tar -xOf "$TARFS" bin/sh > /tmp/sh_binary 2>/dev/null && ls -lh /tmp/sh_binary && rm /tmp/sh_binary || echo "Could not extract bin/sh"

echo ""
echo "=== Makefile Verification ==="
echo "Checking what binaries the Makefile is configured to build..."
cd ../..
make -n 2>&1 | grep "build/rootfs/bin/" | head -10

echo ""
echo "Done!"
