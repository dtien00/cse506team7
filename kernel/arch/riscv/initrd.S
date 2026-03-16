.section .rodata
.global _tarfs_start
.global _tarfs_end

_tarfs_start:
    .incbin "rootfs/initrd.tar"
_tarfs_end: