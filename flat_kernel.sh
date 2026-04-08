#!/usr/bin/env bash
# flatten.sh
# Copies "." into "./flat_kernel/", but flattens "./kernel/**" into "./flat_kernel/kernel/"
# Kernel header files are prefixed with "k" to avoid collisions with libc headers.

rm -rf ./flat_kernel

set -euo pipefail

SRC="."
DEST="./flat_kernel"
KERNEL_SRC="./kernel"
KERNEL_DEST="$DEST/kernel"
KERNEL_INC_DEST="$DEST/kernel/include"

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
mkdir -p "$KERNEL_INC_DEST"

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
    ext="${filename##*.}"

    # .h files go into include/ with a "k" prefix; everything else into kernel/
    if [ "$ext" = "h" ]; then
        destdir="$KERNEL_INC_DEST"
        filename="k${filename}"
    else
        destdir="$KERNEL_DEST"
    fi

    target="$destdir/$filename"

    # Handle filename collisions by appending a counter
    if [ -e "$target" ]; then
        base="${filename%.*}"
        [ "$ext" = "$filename" ] && ext=""
        counter=1
        while [ -e "$target" ]; do
            if [ -n "$ext" ]; then
                target="$destdir/${base}_${counter}.${ext}"
            else
                target="$destdir/${base}_${counter}"
            fi
            (( counter++ ))
        done
        echo "  Collision: '$filename' -> '$(basename "$target")'"
    fi

    cp "$filepath" "$target"
done

# ── Rewrite #includes in flattened kernel files ──────────────────────────────
echo "Rewriting #include paths in $KERNEL_DEST ..."

find "$KERNEL_DEST" -type f -name "*.c" | while read -r filepath; do
    # In .c files: #include "any/path/file.h" -> #include "include/kfile.h"
    perl -i -pe 's{#include\s*"[^"]*?([^"/]+\.h)"}{#include "include/k$1"}g' "$filepath"
done

find "$KERNEL_INC_DEST" -type f -name "k*.h" | while read -r filepath; do
    # In .h files: #include "any/path/file.h" -> #include "kfile.h"
    perl -i -pe 's{#include\s*"[^"]*?([^"/]+\.h)"}{#include "k$1"}g' "$filepath"
done

# ── Rewrite #includes in tools/ files that reference kernel headers ──────────
echo "Rewriting #include paths in $DEST/tools ..."

find "$DEST/tools" -type f \( -name "*.c" -o -name "*.h" \) | while read -r filepath; do
    # ../kernel/include/fs/file.h  ->  ../kernel/include/kfile.h
    perl -i -pe 's{#include\s*"\.\./kernel/[^"]*?([^"/]+\.h)"}{#include "../kernel/include/k$1"}g' "$filepath"
done

echo "Done. Flat copy is in '$DEST'."
