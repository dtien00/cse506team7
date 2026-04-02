#!/bin/bash

# Quick script to test libc on remote server
# Usage: ./test_libc.sh

./flat_kernel.sh

rsync -avz --delete \
  --exclude='build/kernel' \
  ./ 506:~/jchlee/

echo -e "\n\033[0;33m=== Testing Libc ==>\033[0m\n"

ssh -t 506 "cd ~/jchlee/dump/shell && chmod +x build_libc_test.sh check_binaries.sh && ./build_libc_test.sh"
