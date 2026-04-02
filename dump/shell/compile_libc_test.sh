#!/bin/bash

# Compile script specifically for testing libc
# Pushes code to remote and runs libc test build

../../flat_kernel.sh

# Push to remote
rsync -avz --delete \
  --exclude='build/kernel' \
  ../../ 506:~/jchlee/

echo -e "\n\033[0;33m------------------------\nRunning libc test build on remote server\n------------------------\033[0m\n"

# Run the libc test build script
ssh -t 506 "cd ~/jchlee/dump/shell && chmod +x build_libc_test.sh && ./build_libc_test.sh"

EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo -e "\n\033[0;31m------------------------\nLIBC TEST BUILD FAILED (exit code $EXIT_CODE)\n------------------------\033[0m\n"
    exit 1
fi

echo -e "\n\033[0;32m------------------------\nLIBC TEST BUILD COMPLETE\n------------------------\033[0m\n"
