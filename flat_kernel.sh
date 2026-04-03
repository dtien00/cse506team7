#!/usr/bin/env bash
# flatten.sh
# Copies "." into "./flat_kernel/", but flattens "./kernel/**" into "./flat_kernel/kernel/"

rm -rf ./flat_kernel

set -euo pipefail

SRC="."
DEST="./flat_kernel"
KERNEL_SRC="./kernel"
KERNEL_DEST="$DEST/kernel"

# ── Sanity checks ────────────────────────────────────────────────────────────
if [ ! -d "$KERNEL_SRC" ]; then
    echo "Error: '$KERNEL_SRC' directory not found." >&2
    exit 1
fi

if [ -d "$DEST" ]; then
    echo "Warning: '$DEST' already exists. Contents may be overwritten."
fi

# ── Create destination dirs ──────────────────────────────────────────────────
mkdir -p "$DEST"
mkdir -p "$KERNEL_DEST"

# ── Copy everything except ./kernel and ./flat_kernel (preserve structure) ───
echo "Copying source tree (excluding ./kernel and ./flat_kernel) ..."
rsync -a \
    --exclude="kernel" \
    --exclude="flat_kernel" \
    --exclude=".git" \
    --exclude=".gitignore" \
    --exclude=".gitmodules" \
    --exclude=".gitattributes" \
    "$SRC/" "$DEST/"

# ── Flatten ./kernel into ./flat_kernel/kernel ───────────────────────────────
echo "Flattening ./kernel -> $KERNEL_DEST ..."

find "$KERNEL_SRC" -type f | while read -r filepath; do
    filename="$(basename "$filepath")"
    target="$KERNEL_DEST/$filename"

    # Handle filename collisions by appending a counter
    if [ -e "$target" ]; then
        base="${filename%.*}"
        ext="${filename##*.}"
        # If there's no extension, ext == filename; guard against that
        [ "$ext" = "$filename" ] && ext=""
        counter=1
        while [ -e "$target" ]; do
            if [ -n "$ext" ]; then
                target="$KERNEL_DEST/${base}_${counter}.${ext}"
            else
                target="$KERNEL_DEST/${base}_${counter}"
            fi
            (( counter++ ))
        done
        echo "  Collision: '$filename' -> '$(basename "$target")'"
    fi

    cp "$filepath" "$target"
done

# ── Rewrite #includes in flattened kernel files ──────────────────────────────
echo "Rewriting #include paths in $KERNEL_DEST ..."

find "$KERNEL_DEST" -type f \( -name "*.c" -o -name "*.h" \) | while read -r filepath; do
    # Strip path prefixes from local includes, keeping only the basename
    # #include "some/nested/path/file.h"  ->  #include "file.h"
    # #include <system.h> is left untouched (angle-bracket includes)
    perl -i -pe 's{#include\s*"[^"]*?([^"/]+)"}{#include "$1"}g' "$filepath"
done

# ── Rewrite #includes in tools/ files that reference kernel headers ──────────
echo "Rewriting #include paths in $DEST/tools ..."

find "$DEST/tools" -type f \( -name "*.c" -o -name "*.h" \) | while read -r filepath; do
    # ../kernel/include/fs/file.h  ->  ../kernel/file.h
    # ../kernel/include/something/file.h  ->  ../kernel/file.h
    perl -i -pe 's{#include\s*"\.\./kernel/[^"]*?([^"/]+)"}{#include "../kernel/$1"}g' "$filepath"
done

# ── Fix Makefile include paths for flat kernel structure ─────────────────────
# kernel/ was flattened so -Ikernel/include must become -Ikernel
sed -i 's/-Ikernel\/include/-Ikernel/g' "$DEST/Makefile"

echo "Done. Flat copy is in '$DEST'."