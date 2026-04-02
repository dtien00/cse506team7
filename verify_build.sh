#!/bin/bash

# Complete verification script
# Tests libc, builds kernel, verifies packaging

./flat_kernel.sh

rsync -avz --delete \
  --exclude='build/kernel' \
  ./ 506:~/jchlee/

echo -e "\n\033[0;33m=== Step 1: Testing Libc ===\033[0m\n"
ssh 506 "cd ~/jchlee/dump/shell && chmod +x build_libc_test.sh && ./build_libc_test.sh"

if [ $? -ne 0 ]; then
    echo -e "\n\033[0;31mLibc test failed! Fix missing symbols before continuing.\033[0m\n"
    exit 1
fi

echo -e "\n\033[0;33m=== Step 2: Building Kernel ===\033[0m\n"
ssh 506 "cd ~/jchlee && make clean && make 2>&1"

if [ $? -ne 0 ]; then
    echo -e "\n\033[0;31mKernel build failed!\033[0m\n"
    exit 1
fi

echo -e "\n\033[0;33m=== Step 3: Verifying Tarfs ===\033[0m\n"
ssh 506 "cd ~/jchlee/dump/shell && chmod +x verify_tarfs.sh check_binaries.sh && ./verify_tarfs.sh && echo '' && ./check_binaries.sh"

echo -e "\n\033[0;32m=== Verification Complete ===\033[0m\n"
echo "If all checks passed, you can now run: ./compile.sh to boot in QEMU"
