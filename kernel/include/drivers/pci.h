#pragma once
#include "kernel/types.h"

// QEMU virt machine PCIe ECAM base address
#define ECAM_BASE  0x30000000UL

// Standard PCI config space header offsets
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_HEADER_TYPE     0x0E
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_CAP_PTR         0x34
#define PCI_INTERRUPT_LINE  0x3C

// PCI command register bits
#define PCI_CMD_BUS_MASTER  (1 << 2)
#define PCI_CMD_MEM_SPACE   (1 << 1)

// VirtIO PCI capability ID (vendor-specific)
#define PCI_CAP_ID_VNDR     0x09

// VirtIO PCI capability cfg_type values
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4
#define VIRTIO_PCI_CAP_PCI_CFG      5

// Identifies a PCI device by bus/device/function
typedef struct {
    uint8_t bus;
    uint8_t dev;
    uint8_t fn;
    int     valid;  // non-zero if found
} pci_addr_t;

// ECAM MMIO read/write helpers
uint8_t  pci_read8 (uint8_t bus, uint8_t dev, uint8_t fn, uint32_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t offset);
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t offset);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t offset, uint32_t val);

// Find a device by vendor + device ID on bus 0
pci_addr_t pci_find_device(uint16_t vendor, uint16_t device);

// Resolve the physical address of a VirtIO PCI capability region.
// cfg_type: VIRTIO_PCI_CAP_COMMON_CFG, VIRTIO_PCI_CAP_NOTIFY_CFG, etc.
// notify_off_multiplier is filled in (only meaningful for NOTIFY cap).
uintptr_t pci_virtio_cap_addr(pci_addr_t addr, uint8_t cfg_type,
                               uint32_t *notify_off_multiplier);
