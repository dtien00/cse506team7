#!/bin/bash

./flat_kernel.sh

# push to remote
rsync -avz --delete \
  --exclude='build/kernel' \
  ./ 506:~/jchlee/

# echo -e "\n------------------------\n"

# # Build only
# ssh 506 "cd ~/jchlee && make 2>&1"
# EXIT_CODE=$?

# if [ $EXIT_CODE -ne 0 ]; then
#     echo -e "\n\033[0;31m------------------------\nBUILD FAILED (exit code $EXIT_CODE)\n------------------------\033[0m\n"
#     exit 1
# fi

echo -e "\n\033[0;32m------------------------\nBUILD SUCCESS — launching QEMU interactively\n------------------------\033[0m\n"

# Hand off terminal fully to QEMU via interactive SSH
exec ssh -t 506 "cd ~/jchlee/flat_kernel && make test"
# exec ssh -t 506 "cd ~/jchlee/flat_kernel && make qemu"