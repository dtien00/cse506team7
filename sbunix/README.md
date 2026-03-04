# SBUNix

This repo builds a RISC‑V kernel, embeds a tar-based root filesystem into the kernel, and can boot under QEMU with an optional GDB stub for debugging.

## Boot memory map (QEMU virt + OpenSBI)

- Physical DRAM on `qemu-system-riscv64 -machine virt` starts at `0x80000000`.
- The Makefile uses `-bios default`, so OpenSBI firmware is loaded first.
- The kernel linker script places SBUnix at `0x80200000` (`kernel/kernel.ld`).
- OpenSBI transfers control to the kernel entry point at that linked address.

So both addresses are correct:
- `0x80000000` = RAM base (and firmware region)
- `0x80200000` = kernel load/entry region in this project


## SBUnix Makefile usage

To help build a RISC‑V kernel (`build/kernel.elf`) and run it in QEMU.  
The kernel embeds `rootfs/` into the ELF as a `.tarfs` section.

### Build
```sh
# Default build (C kernel)
make KLANG=c all

# Full clean rebuild
make KLANG=c rebuild

# Build with debug symbols for debugging with gdb
make DEBUG=1 KLANG=c all
```
### Run in QEMU

Booting the kernel in QEMU.
```sh
make KLANG=c qemu
```
This will also create a disk image at build/disk.img (default size 16) using tools/mkfs.c, and attach it as a virtio block device.

Debugging with gdb on the VM. This needs the build/kernel.elf to be built with debug symbols as detailed in the build section
1. Start QEMU with GDB stub
    ```sh
    make KLANG=c qemu-gdb
    ```
2. Attach GDB : below are commands to do it from another VM terminal. Your IDE of choice may also do this for you.
    ```sh
    riscv64-unknown-elf-gdb build/kernel.elf
    (gdb) target remote :1234
    (gdb) break boot
    (gdb) continue
    ```


