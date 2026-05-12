#include "drivers/virtio.h"
#include "drivers/pci.h"
#include "drivers/plic.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "process/process.h"
#include "arch/riscv.h"

// VirtIO PCI vendor/device IDs for modern non-transitional block device
#define VIRTIO_PCI_VENDOR_ID    0x1AF4
#define VIRTIO_PCI_DEV_BLK      0x1042

static virtio_blk_dev_t blk_dev;
static int initialized = 0;

// Initialize VirtIO block device via PCI transport
int virtio_blk_init(void) {
    // Step 1: Find the device on the PCIe bus
    pci_addr_t pci = pci_find_device(VIRTIO_PCI_VENDOR_ID, VIRTIO_PCI_DEV_BLK);
    if (!pci.valid) {
        printk("virtio_blk: PCI device %d:%d not found\n",
               VIRTIO_PCI_VENDOR_ID, VIRTIO_PCI_DEV_BLK);
        return -1;
    }

    // Step 2: Locate CommonCfg, Notify, and DeviceCfg capability regions
    uint32_t notify_off_mult = 4;
    uintptr_t common_addr = pci_virtio_cap_addr(pci, VIRTIO_PCI_CAP_COMMON_CFG, NULL);
    uintptr_t notify_base = pci_virtio_cap_addr(pci, VIRTIO_PCI_CAP_NOTIFY_CFG, &notify_off_mult);
    uintptr_t devcfg_addr = pci_virtio_cap_addr(pci, VIRTIO_PCI_CAP_DEVICE_CFG, NULL);
    uintptr_t isr_addr    = pci_virtio_cap_addr(pci, VIRTIO_PCI_CAP_ISR_CFG, NULL);

    if (!common_addr) {
        printk("virtio_blk: CommonCfg capability not found\n");
        return -1;
    }
    if (!notify_base) {
        printk("virtio_blk: Notify capability not found\n");
        return -1;
    }

    blk_dev.dev_cfg  = (volatile uint32_t *)devcfg_addr;
    blk_dev.isr_addr = (volatile uint8_t *)isr_addr;

    // Access CommonCfg by byte offset to avoid volatile-packed-struct miscompilation.
    // Offsets per VirtIO 1.1 spec section 4.1.4.3:
    //   +0  device_feature_select (u32)   +4  device_feature (u32)
    //   +8  driver_feature_select (u32)   +12 driver_feature (u32)
    //   +16 config_msix_vector (u16)      +18 num_queues (u16)
    //   +20 device_status (u8)            +21 config_generation (u8)
    //   +22 queue_select (u16)            +24 queue_size (u16)
    //   +26 queue_msix_vector (u16)       +28 queue_enable (u16)
    //   +30 queue_notify_off (u16)        +32 queue_desc (u64)
    //   +40 queue_driver (u64)            +48 queue_device (u64)
    #define CFG_R8(off)       (*(volatile uint8_t  *)(common_addr + (off)))
    #define CFG_R16(off)      (*(volatile uint16_t *)(common_addr + (off)))
    #define CFG_R32(off)      (*(volatile uint32_t *)(common_addr + (off)))
    #define CFG_W8(off, val)  do { *(volatile uint8_t  *)(common_addr + (off)) = (val); } while(0)
    #define CFG_W16(off, val) do { *(volatile uint16_t *)(common_addr + (off)) = (val); } while(0)
    #define CFG_W32(off, val) do { *(volatile uint32_t *)(common_addr + (off)) = (val); } while(0)
    #define CFG_W64(off, val) do { *(volatile uint64_t *)(common_addr + (off)) = (val); } while(0)

    // Step 3: Reset device (device_status @ 20)
    CFG_W8(20, 0);
    while (CFG_R8(20) != 0) {}

    // Step 4: ACKNOWLEDGE + DRIVER
    CFG_W8(20, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    // Step 5: Feature negotiation (read device features, negotiate VERSION_1)
    CFG_W32(0, 0); CFG_R32(4);  // word 0 (unused)
    CFG_W32(0, 1); CFG_R32(4);  // word 1 (unused)
    CFG_W32(8, 0);
    CFG_W32(12, 0);
    CFG_W32(8, 1);
    CFG_W32(12, VIRTIO_F_VERSION_1);

    // Set FEATURES_OK
    CFG_W8(20, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    __sync_synchronize();
    uint8_t st = CFG_R8(20);
    if (!(st & VIRTIO_STATUS_FEATURES_OK)) {
        printk("virtio_blk: FEATURES_OK not accepted by device\n");
        return -1;
    }

    // Step 6: Setup virtqueue 0
    // queue_select @ offset 22, queue_size @ offset 24
    CFG_W16(22, 0);
    __sync_synchronize();

    // Disable MSI-X for config and queue 0 to force INTx mode.
    // Without this the device defaults to MSI-X vector 0, bypassing the INTx
    // line and preventing the PLIC from ever seeing an interrupt.
    CFG_W16(16, 0xFFFF);  // config_msix_vector = VIRTIO_MSI_NO_VECTOR
    CFG_W16(26, 0xFFFF);  // queue_msix_vector  = VIRTIO_MSI_NO_VECTOR
    __sync_synchronize();
    uint16_t queue_max = CFG_R16(24);
    if (queue_max == 0) {
        printk("virtio_blk: queue not available\n");
        return -1;
    }

    uint16_t queue_size = (queue_max < VIRTIO_RING_SIZE) ? queue_max : VIRTIO_RING_SIZE;
    CFG_W16(24, queue_size);

    // Allocate descriptor table, available ring, used ring
    // Must be page-aligned so the device (DMA) can access them correctly.
    blk_dev.desc = (volatile virtq_desc_t *)pmm_alloc_page();
    if (!blk_dev.desc) { printk("virtio_blk: alloc desc failed\n"); return -1; }
    memset((void *)blk_dev.desc, 0, PGSIZE);

    blk_dev.avail = (volatile virtq_avail_t *)pmm_alloc_page();
    if (!blk_dev.avail) {
        printk("virtio_blk: alloc avail failed\n");
        return -1;
    }
    memset((void *)blk_dev.avail, 0, PGSIZE);

    blk_dev.used = (volatile virtq_used_t *)pmm_alloc_page();
    if (!blk_dev.used) {
        printk("virtio_blk: alloc used failed\n");
        return -1;
    }
    memset((void *)blk_dev.used, 0, PGSIZE);

    blk_dev.last_used_idx = 0;

    // Write queue addresses into CommonCfg
    // queue_desc @ 32, queue_driver @ 40, queue_device @ 48
    CFG_W64(32, (uint64_t)(uintptr_t)blk_dev.desc);
    CFG_W64(40, (uint64_t)(uintptr_t)blk_dev.avail);
    CFG_W64(48, (uint64_t)(uintptr_t)blk_dev.used);

    // Compute the notify address for queue 0 (queue_notify_off @ 30)
    uint16_t queue_notify_off = CFG_R16(30);
    blk_dev.notify_addr = notify_base + (uintptr_t)queue_notify_off * notify_off_mult;

    // Enable the queue (queue_enable @ 28)
    CFG_W16(28, 1);

    // Step 7: DRIVER_OK (device_status @ 20)
    CFG_W8(20, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
              | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    // Step 8: Read device capacity from device config region
    // Device config: first 8 bytes = capacity (little-endian 64-bit)
    if (blk_dev.dev_cfg) {
        uint32_t cap_lo = blk_dev.dev_cfg[0];
        uint32_t cap_hi = blk_dev.dev_cfg[1];
        blk_dev.capacity = ((uint64_t)cap_hi << 32) | cap_lo;
    } else {
        blk_dev.capacity = 0;
    }
    blk_dev.block_size = 512;

    printk("virtio_blk: initialized, capacity = %ld sectors (%ld MB)\n",
           (long)blk_dev.capacity, (long)((blk_dev.capacity * 512) / (1024 * 1024)));

    initialized = 1;

    // Compute the real PLIC IRQ using QEMU virt PCIe INTx swizzling:
    //   PLIC_IRQ = PLIC_PCIE_INTA_IRQ + (slot + pin - 1) % 4
    // where pin comes from PCI_INTERRUPT_PIN (1=INTA..4=INTD, 0=none).
    // PCI_INTERRUPT_LINE is a firmware hint that QEMU leaves as 0 for PCIe,
    // so we never rely on it.
    uint8_t interrupt_pin  = pci_read8(pci.bus, pci.dev, pci.fn, PCI_INTERRUPT_PIN);
    uint8_t interrupt_line = pci_read8(pci.bus, pci.dev, pci.fn, PCI_INTERRUPT_LINE);
    printk("virtio_blk: PCI slot=%u interrupt_pin=%u interrupt_line=%u\n",
           pci.dev, interrupt_pin, interrupt_line);

    if (interrupt_pin == 0) {
        printk("virtio_blk: WARNING: INTERRUPT_PIN=0 (no INTx), falling back to IRQ 32\n");
        blk_dev.irq = PLIC_PCIE_INTA_IRQ;
    } else {
        blk_dev.irq = PLIC_PCIE_INTA_IRQ + ((pci.dev + interrupt_pin - 1) % 4);
    }
    printk("virtio_blk: computed PLIC IRQ = %u (slot=%u pin=%u)\n",
           blk_dev.irq, pci.dev, interrupt_pin);

    // Enable all four PCIe INTx sources (32-35) so a swizzling mis-estimate
    // still gets handled — the unexpected-IRQ path in trap.c will print which
    // one actually fires if our formula is off.
    for (uint32_t i = PLIC_PCIE_INTA_IRQ; i < PLIC_PCIE_INTA_IRQ + 4; i++)
        plic_enable_irq(i);
    #undef CFG_R8
    #undef CFG_R16
    #undef CFG_R32
    #undef CFG_W8
    #undef CFG_W16
    #undef CFG_W32
    #undef CFG_W64
    return 0;
}

// Perform a block I/O operation
static int virtio_blk_rw(uint64_t sector, void *buf, size_t count, int write) {
    if (!initialized) {
        printk("virtio_blk: device not initialized\n");
        return -1;
    }

    if (sector + count > blk_dev.capacity) {
        printk("virtio_blk: sector %ld out of range\n", (long)sector);
        return -1;
    }

    if (scheduler_live && current) {
        interrupts_off();
        while (blk_dev.busy)
            proc_sleep(&blk_dev.busy);
        blk_dev.busy = 1;
        interrupts_on();
    }

    virtio_blk_req_t *req = (virtio_blk_req_t *)kmalloc(sizeof(virtio_blk_req_t));
    if (!req) {
        blk_dev.busy = 0;
        proc_wakeup(&blk_dev.busy);
        return -1;
    }

    req->type     = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector   = sector;

    uint8_t *status = (uint8_t *)kmalloc(1);
    if (!status) {
        kfree(req);
        blk_dev.busy = 0;
        proc_wakeup(&blk_dev.busy);
        return -1;
    }
    *status = 0xFF;
    // Descriptor 0: request header (device reads)
    blk_dev.desc[0].addr  = (uintptr_t)req;
    blk_dev.desc[0].len   = sizeof(virtio_blk_req_t);
    blk_dev.desc[0].flags = VIRTQ_DESC_F_NEXT;
    blk_dev.desc[0].next  = 1;

    // Descriptor 1: data buffer
    blk_dev.desc[1].addr  = (uintptr_t)buf;
    blk_dev.desc[1].len   = count * 512;
    blk_dev.desc[1].flags = VIRTQ_DESC_F_NEXT | (write ? 0 : VIRTQ_DESC_F_WRITE);
    blk_dev.desc[1].next  = 2;

    // Descriptor 2: status byte (device writes)
    blk_dev.desc[2].addr  = (uintptr_t)status;
    blk_dev.desc[2].len   = 1;
    blk_dev.desc[2].flags = VIRTQ_DESC_F_WRITE;
    blk_dev.desc[2].next  = 0;

    // expected_used_idx is the used->idx value we expect after this request completes.
    uint16_t expected_used_idx = (uint16_t)(blk_dev.used->idx + 1);

    uint16_t avail_idx = blk_dev.avail->idx;
    blk_dev.avail->ring[avail_idx % VIRTIO_RING_SIZE] = 0;
    __sync_synchronize();
    blk_dev.avail->idx = avail_idx + 1;
    __sync_synchronize();

    // Re-arm the IRQ for this submission (disabled by virtio_blk_irq after
    // the previous completion to prevent INTx level-trigger storms).
    plic_enable_irq(blk_dev.irq);

    // Notify device: write queue index 0 to the notify address
    *(volatile uint32_t *)blk_dev.notify_addr = 0;


    if (scheduler_live && current) {
        // Check condition with interrupts disabled to close the race where the
        // device completes and IRQ fires proc_wakeup() BEFORE proc_sleep() blocks.
        // Without this, wakeup is lost and we stall until the timer poll fires.
        interrupts_off();
        int sleep_count = 0;
        while (blk_dev.used->idx != expected_used_idx) {
            // proc_sleep: interrupts_off (no-op, already off) → set chan →
            // interrupts_on → swtch → returns with interrupts ON.
            // Re-disable after return before re-checking the condition.
            proc_sleep(&blk_dev);
            interrupts_off();
            sleep_count++;
            if (sleep_count > 50) {
                printk("virtio_blk: STUCK! %s sector=%llu used->idx=%u expected=%u sstatus=%lx sie=%lx\n",
                       write ? "write" : "read", (unsigned long long)sector,
                       blk_dev.used->idx, expected_used_idx, read_csr(sstatus), read_csr(sie));
                interrupts_on();
                int timeout = 400000000;
                while (blk_dev.used->idx != expected_used_idx && timeout-- > 0) {}
                printk("virtio_blk: spin-wait done, used->idx=%u timeout=%d\n",
                       blk_dev.used->idx, timeout);
                break;
            }
        }
        interrupts_on();
        blk_dev.last_used_idx = blk_dev.used->idx;
        // printk("virtio_blk: woke up for %s sector=%llu\n",
        //        write ? "write" : "read", (unsigned long long)sector);
    } else {
        int timeout = 400000000;
        while (blk_dev.used->idx != expected_used_idx && timeout-- > 0) {}
        if (timeout <= 0) {
            printk("virtio_blk: request timeout\n");
            kfree(req);
            kfree(status);
            blk_dev.busy = 0;
            proc_wakeup(&blk_dev.busy);
            return -1;
        }
        blk_dev.last_used_idx = blk_dev.used->idx;
    }

    blk_dev.busy = 0;
    proc_wakeup(&blk_dev.busy);

    if (*status != VIRTIO_BLK_S_OK) {
        printk("virtio_blk: request failed with status %d\n", *status);
        kfree(req);
        kfree(status);
        return -1;
    }

    kfree(req);
    kfree(status);
    return 0;
}

int virtio_blk_read(uint64_t sector, void *buf, size_t count) {
    return virtio_blk_rw(sector, buf, count, 0);
}

int virtio_blk_write(uint64_t sector, const void *buf, size_t count) {
    return virtio_blk_rw(sector, (void *)buf, count, 1);
}

int virtio_blk_flush(void) {
    if (!initialized) return -1;

    virtio_blk_req_t *req = (virtio_blk_req_t *)kmalloc(sizeof(virtio_blk_req_t));
    if (!req) return -1;

    req->type     = VIRTIO_BLK_T_FLUSH;
    req->reserved = 0;
    req->sector   = 0;

    uint8_t *status = (uint8_t *)kmalloc(1);
    if (!status) { kfree(req); return -1; }
    *status = 0xFF;

    blk_dev.desc[0].addr  = (uintptr_t)req;
    blk_dev.desc[0].len   = sizeof(virtio_blk_req_t);
    blk_dev.desc[0].flags = VIRTQ_DESC_F_NEXT;
    blk_dev.desc[0].next  = 1;

    blk_dev.desc[1].addr  = (uintptr_t)status;
    blk_dev.desc[1].len   = 1;
    blk_dev.desc[1].flags = VIRTQ_DESC_F_WRITE;
    blk_dev.desc[1].next  = 0;

    uint16_t expected_used_idx = (uint16_t)(blk_dev.used->idx + 1);

    uint16_t avail_idx = blk_dev.avail->idx;
    blk_dev.avail->ring[avail_idx % VIRTIO_RING_SIZE] = 0;
    __sync_synchronize();
    blk_dev.avail->idx = avail_idx + 1;
    __sync_synchronize();

    plic_enable_irq(blk_dev.irq);
    *(volatile uint32_t *)blk_dev.notify_addr = 0;

    if (scheduler_live && current) {
        interrupts_off();
        while (blk_dev.used->idx != expected_used_idx) {
            proc_sleep(&blk_dev);
            interrupts_off();
        }
        interrupts_on();
        blk_dev.last_used_idx = blk_dev.used->idx;
    } else {
        int timeout = 10000000;
        while (blk_dev.used->idx != expected_used_idx && timeout-- > 0) {}
        if (timeout <= 0) {
            printk("virtio_blk: flush timeout\n");
            kfree(req);
            kfree(status);
            return -1;
        }
        blk_dev.last_used_idx = blk_dev.used->idx;
    }

    int ret = (*status == VIRTIO_BLK_S_OK) ? 0 : -1;
    kfree(req);
    kfree(status);
    return ret;
}

uint64_t virtio_blk_get_capacity(void) {
    return initialized ? blk_dev.capacity : 0;
}

// Polled from timer_handler: if device completed a request but IRQ was missed,
// advance last_used_idx and wake any blocked waiters.
void virtio_blk_poll(void) {
    if (!initialized) return;
    if (blk_dev.last_used_idx == blk_dev.used->idx) return;
    while (blk_dev.last_used_idx != blk_dev.used->idx)
        blk_dev.last_used_idx++;
    proc_wakeup(&blk_dev);
}

uint32_t virtio_blk_get_irq(void) {
    return blk_dev.irq;
}

// Called from the external interrupt handler when the virtio IRQ fires.
// Advances last_used_idx and wakes whoever is sleeping on &blk_dev.
// NOTE: was not called before plic_enable_irq fix (irq/32 word index was wrong)
void virtio_blk_irq(void) {
    // Disable this PLIC source immediately so plic_complete() in the caller
    // cannot re-deliver the interrupt if the device INTx line is still asserted
    // (which happens when isr_addr is NULL and the ISR register goes unread).
    // The source is re-armed in virtio_blk_rw() before the next submission.
    plic_disable_irq(blk_dev.irq);

    // One-shot confirmation that interrupt-driven I/O is working.
    static int irq_confirmed = 0;
    if (!irq_confirmed) {
        irq_confirmed = 1;
        printk("virtio_blk: IRQ confirmed (first IRQ fired)\n");
    }

    // Read ISR status to deassert the device-level interrupt line.
    if (blk_dev.isr_addr)
        (void)*blk_dev.isr_addr;

    while (blk_dev.last_used_idx != blk_dev.used->idx) {
        blk_dev.last_used_idx++;
    }
    proc_wakeup(&blk_dev);
}
