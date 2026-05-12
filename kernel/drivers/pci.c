#include "drivers/pci.h"
#include "kernel/printk.h"

// Compute the ECAM config-space address for (bus, dev, fn, offset).
static volatile void *ecam_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t offset) {
    uintptr_t base = ECAM_BASE
                   + ((uint32_t)bus  << 20)
                   + ((uint32_t)dev  << 15)
                   + ((uint32_t)fn   << 12)
                   + offset;
    return (volatile void *)base;
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t offset) {
    return *(volatile uint8_t *)ecam_addr(bus, dev, fn, offset);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t offset) {
    return *(volatile uint16_t *)ecam_addr(bus, dev, fn, offset);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t offset) {
    return *(volatile uint32_t *)ecam_addr(bus, dev, fn, offset);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)ecam_addr(bus, dev, fn, offset) = val;
}

pci_addr_t pci_find_device(uint16_t vendor, uint16_t device) {
    pci_addr_t result = {0, 0, 0, 0};
    for (uint8_t d = 0; d < 32; d++) {
        uint32_t id = pci_read32(0, d, 0, PCI_VENDOR_ID);
        uint16_t vid = (uint16_t)(id & 0xFFFF);
        uint16_t did = (uint16_t)(id >> 16);
        if (vid == 0xFFFF) continue;  // no device
        if (vid == vendor && did == device) {
            printk("pci: found device %d:%d at bus=0 dev=%d fn=0\n", vid, did, d);
            result.bus = 0;
            result.dev = d;
            result.fn  = 0;
            result.valid = 1;
            return result;
        }
    }
    return result;
}

#define PCI_MMIO_BASE 0x40000000UL
static uintptr_t pci_mmio_next = PCI_MMIO_BASE;

// Size-probe, assign, and write back an unassigned BAR. Returns the base address.
static uintptr_t pci_assign_bar(pci_addr_t a, uint8_t bar_idx, uint32_t bar_lo, uint8_t type) {
    uint32_t bar_off = PCI_BAR0 + bar_idx * 4;

    // Size-probe: write all-ones, read back size mask, restore
    pci_write32(a.bus, a.dev, a.fn, bar_off, 0xFFFFFFFF);
    uint32_t size_mask_lo = pci_read32(a.bus, a.dev, a.fn, bar_off);
    uint32_t size_lo = ~(size_mask_lo & ~0xFU) + 1;
    if (size_lo == 0) size_lo = 0x1000;  // fallback: 4KB minimum

    if (type == 2) {
        // 64-bit: zero out the high BAR
        pci_write32(a.bus, a.dev, a.fn, bar_off + 4, 0xFFFFFFFF);
        pci_write32(a.bus, a.dev, a.fn, bar_off + 4, 0);
    }

    // Align to size
    uintptr_t base = (pci_mmio_next + size_lo - 1) & ~((uintptr_t)size_lo - 1);
    pci_mmio_next = base + size_lo;

    // Write assigned address back (preserve type bits)
    pci_write32(a.bus, a.dev, a.fn, bar_off, (uint32_t)base | (bar_lo & 0xF));

    printk("pci: assigned BAR[%d] = %ld (size=%ld)\n", bar_idx, (long)base, (long)size_lo);
    return base;
}

// Return the physical base address for a BAR (32-bit or 64-bit memory BAR).
static uintptr_t pci_bar_base(pci_addr_t a, uint8_t bar_idx) {
    uint32_t bar_off = PCI_BAR0 + bar_idx * 4;
    uint32_t bar = pci_read32(a.bus, a.dev, a.fn, bar_off);

    if (bar & 1) return 0;  // I/O BAR, unsupported

    uint8_t type = (bar >> 1) & 0x3;
    if (type == 2) {
        // 64-bit BAR
        uint32_t bar_hi = pci_read32(a.bus, a.dev, a.fn, bar_off + 4);
        uintptr_t addr = (uintptr_t)(((uint64_t)bar_hi << 32) | (bar & ~0xFUL));
        printk("pci_bar_base: bar_idx=%d bar=%ld bar_hi=%ld addr=%ld\n",
            bar_idx, (long)bar, (long)bar_hi, (long)addr);
        if (addr == 0)
            addr = pci_assign_bar(a, bar_idx, bar, type);
        return addr;
    }

    // 32-bit BAR
    uintptr_t addr = (uintptr_t)(bar & ~0xFUL);
    if (addr == 0)
        addr = pci_assign_bar(a, bar_idx, bar, type);
    return addr;
}

uintptr_t pci_virtio_cap_addr(pci_addr_t a, uint8_t cfg_type,
                               uint32_t *notify_off_multiplier) {
    if (notify_off_multiplier) *notify_off_multiplier = 4;

    // Enable bus-mastering and memory space decoding; clear Interrupt Disable
    // (bit 10) so the device can assert INTx.  The bit-OR approach would
    // leave a firmware-set Interrupt Disable bit in place, so mask it out.
    uint32_t cmd = pci_read32(a.bus, a.dev, a.fn, PCI_COMMAND);
    pci_write32(a.bus, a.dev, a.fn, PCI_COMMAND,
                (cmd | PCI_CMD_BUS_MASTER | PCI_CMD_MEM_SPACE) & ~PCI_CMD_INT_DISABLE);

    // Walk the PCI capability list
    uint8_t cap_ptr = pci_read8(a.bus, a.dev, a.fn, PCI_CAP_PTR) & 0xFC;
    while (cap_ptr != 0) {
        uint8_t cap_id   = pci_read8(a.bus, a.dev, a.fn, cap_ptr + 0);
        uint8_t cap_next = pci_read8(a.bus, a.dev, a.fn, cap_ptr + 1);

        if (cap_id == PCI_CAP_ID_VNDR) {
            uint8_t  ct  = pci_read8 (a.bus, a.dev, a.fn, cap_ptr + 3);
            uint8_t  bar = pci_read8 (a.bus, a.dev, a.fn, cap_ptr + 4);
            uint32_t off = pci_read32(a.bus, a.dev, a.fn, cap_ptr + 8);

            if (ct == cfg_type) {
                uintptr_t bar_base = pci_bar_base(a, bar);
                if (notify_off_multiplier && cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG)
                    *notify_off_multiplier = pci_read32(a.bus, a.dev, a.fn, cap_ptr + 16);
                return bar_base + off;
            }
        }

        cap_ptr = cap_next & 0xFC;
    }
    return 0;
}
