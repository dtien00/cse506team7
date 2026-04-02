#!/bin/bash

# Quick check script to verify binaries are built and packaged
# Run this after 'make' to ensure everything is ready

BUILD_DIR="../../build"
ROOTFS_BIN="${BUILD_DIR}/rootfs/bin"
TARFS="${BUILD_DIR}/rootfs.tar"

echo "=== Quick Binary Check ==="
echo ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "✗ Build directory not found. Run 'make' first."
    exit 1
fi

# Check for shell binary
if [ -f "${ROOTFS_BIN}/sh" ]; then
    SIZE=$(ls -lh "${ROOTFS_BIN}/sh" | awk '{print $5}')
    echo "✓ Shell binary: ${ROOTFS_BIN}/sh ($SIZE)"
else
    echo "✗ Shell binary MISSING: ${ROOTFS_BIN}/sh"
    echo "  → Check that bin/sh/sh.c exists and compiles"
fi

# Count utilities
if [ -d "$ROOTFS_BIN" ]; then
    COUNT=$(ls "$ROOTFS_BIN" 2>/dev/null | wc -l)
    echo "✓ Found $COUNT binaries in ${ROOTFS_BIN}/"
    ls "$ROOTFS_BIN" | sed 's/^/    /'
else
    echo "✗ No binaries directory found"
fi

echo ""

# Check tarfs
if [ -f "$TARFS" ]; then
    SIZE=$(ls -lh "$TARFS" | awk '{print $5}')
    echo "✓ Tarfs archive: $TARFS ($SIZE)"
    
    # Quick check for shell in tarfs
    if tar -tf "$TARFS" 2>/dev/null | grep -q "^bin/sh$"; then
        echo "✓ Shell is packaged in tarfs"
    else
        echo "✗ Shell NOT found in tarfs!"
    fi
else
    echo "✗ Tarfs not found: $TARFS"
fi

echo ""

# Check kernel.elf
if [ -f "${BUILD_DIR}/kernel.elf" ]; then
    SIZE=$(ls -lh "${BUILD_DIR}/kernel.elf" | awk '{print $5}')
    echo "✓ Kernel image: ${BUILD_DIR}/kernel.elf ($SIZE)"
else
    echo "✗ Kernel image not found"
fi

echo ""
echo "=== Summary ==="

# Final verdict
ERRORS=0
[ ! -f "${ROOTFS_BIN}/sh" ] && ERRORS=$((ERRORS + 1))
[ ! -f "$TARFS" ] && ERRORS=$((ERRORS + 1))
[ ! -f "${BUILD_DIR}/kernel.elf" ] && ERRORS=$((ERRORS + 1))

if [ $ERRORS -eq 0 ]; then
    echo "✓ All critical components present - ready to boot!"
else
    echo "✗ $ERRORS critical issue(s) found - fix before booting"
fi
