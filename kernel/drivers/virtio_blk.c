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
    printk("looking for VIRTIO_PCI_CAP_COMMON_CFG (%d)\n", VIRTIO_PCI_CAP_COMMON_CFG);
    uintptr_t common_addr = pci_virtio_cap_addr(pci, VIRTIO_PCI_CAP_COMMON_CFG, NULL);
    printk("looking for VIRTIO_PCI_CAP_NOTIFY_CFG (%d)\n", VIRTIO_PCI_CAP_NOTIFY_CFG);
    uintptr_t notify_base = pci_virtio_cap_addr(pci, VIRTIO_PCI_CAP_NOTIFY_CFG, &notify_off_mult);
    printk("looking for VIRTIO_PCI_CAP_DEVICE_CFG (%d)\n", VIRTIO_PCI_CAP_DEVICE_CFG);
    uintptr_t devcfg_addr = pci_virtio_cap_addr(pci, VIRTIO_PCI_CAP_DEVICE_CFG, NULL);
    printk("looking for VIRTIO_PCI_CAP_ISR_CFG (%d)\n", VIRTIO_PCI_CAP_ISR_CFG);
    uintptr_t isr_addr    = pci_virtio_cap_addr(pci, VIRTIO_PCI_CAP_ISR_CFG, NULL);

    if (!common_addr) {
        printk("virtio_blk: CommonCfg capability not found\n");
        return -1;
    }
    if (!notify_base) {
        printk("virtio_blk: Notify capability not found\n");
        return -1;
    }

    printk("virtio_blk: CommonCfg @ %ld, Notify @ %ld, DevCfg @ %ld, ISR @ %ld\n",
           common_addr, notify_base, devcfg_addr, isr_addr);

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

    // Step 5: Feature negotiation
    CFG_W32(0, 0);
    uint32_t dev_feat0 = CFG_R32(4);
    CFG_W32(0, 1);
    uint32_t dev_feat1 = CFG_R32(4);
    printk("virtio_blk: device features word0=%ld word1=%ld\n",
           (long)dev_feat0, (long)dev_feat1);

    CFG_W32(8, 0);
    CFG_W32(12, 0);
    CFG_W32(8, 1);
    CFG_W32(12, VIRTIO_F_VERSION_1);

    // Set FEATURES_OK
    CFG_W8(20, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    __sync_synchronize();
    uint8_t st = CFG_R8(20);
    printk("virtio_blk: device_status after FEATURES_OK = %d\n", (int)st);
    if (!(st & VIRTIO_STATUS_FEATURES_OK)) {
        printk("virtio_blk: FEATURES_OK not accepted by device\n");
        return -1;
    }

    // Step 6: Setup virtqueue 0
    // queue_select @ offset 22, queue_size @ offset 24
    CFG_W16(22, 0);
    __sync_synchronize();

    uint16_t queue_max = CFG_R16(24);
    printk("virtio_blk: num_queues=%d queue_max=%d status=%d\n",
           (int)CFG_R16(18), (int)queue_max, (int)CFG_R8(20));
    if (queue_max == 0) {
        printk("virtio_blk: queue not available\n");
        return -1;
    }

    uint16_t queue_size = (queue_max < VIRTIO_RING_SIZE) ? queue_max : VIRTIO_RING_SIZE;
    CFG_W16(24, queue_size);
    printk("virtio_blk: queue size = %d (max %d)\n", queue_size, queue_max);

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
    plic_enable_irq(PLIC_VIRTIO_IRQ);
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

    virtio_blk_req_t *req = (virtio_blk_req_t *)kmalloc(sizeof(virtio_blk_req_t));
    if (!req) return -1;

    req->type     = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector   = sector;

    uint8_t *status = (uint8_t *)kmalloc(1);
    if (!status) { kfree(req); return -1; }
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

    uint16_t avail_idx = blk_dev.avail->idx;
    blk_dev.avail->ring[avail_idx % VIRTIO_RING_SIZE] = 0;
    __sync_synchronize();
    blk_dev.avail->idx = avail_idx + 1;
    __sync_synchronize();

    // Notify device: write queue index 0 to the notify address
    *(volatile uint32_t *)blk_dev.notify_addr = 0;

    // If a process is running, sleep and let the IRQ wake us.
    // Before the scheduler starts (current == NULL), spin-poll instead.
    // if (current) { // likely want to do later
    if (0){
        proc_sleep(&blk_dev);
    } else {
        int timeout = 400000000;
        while (blk_dev.last_used_idx == blk_dev.used->idx && timeout-- > 0) {}
        if (timeout <= 0) {
            printk("virtio_blk: request timeout\n");
            kfree(req);
            kfree(status);
            return -1;
        }
        blk_dev.last_used_idx++;
    }

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

    uint16_t avail_idx = blk_dev.avail->idx;
    blk_dev.avail->ring[avail_idx % VIRTIO_RING_SIZE] = 0;
    __sync_synchronize();
    blk_dev.avail->idx = avail_idx + 1;
    __sync_synchronize();

    *(volatile uint32_t *)blk_dev.notify_addr = 0;

    if (current) {
        proc_sleep(&blk_dev);
    } else {
        int timeout = 10000000;
        while (blk_dev.last_used_idx == blk_dev.used->idx && timeout-- > 0) {}
        if (timeout <= 0) {
            printk("virtio_blk: flush timeout\n");
            kfree(req);
            kfree(status);
            return -1;
        }
        blk_dev.last_used_idx++;
    }

    int ret = (*status == VIRTIO_BLK_S_OK) ? 0 : -1;
    kfree(req);
    kfree(status);
    return ret;
}

uint64_t virtio_blk_get_capacity(void) {
    return initialized ? blk_dev.capacity : 0;
}

// Called from the external interrupt handler when the virtio IRQ fires.
// Advances last_used_idx and wakes whoever is sleeping on &blk_dev.
void virtio_blk_irq(void) {
    // Read ISR status to acknowledge the interrupt at the device level.
    // For VirtIO PCI legacy interrupts, reading the ISR register deasserts
    // the interrupt line. Without this, the PLIC re-raises the IRQ immediately
    // after plic_complete(), causing an interrupt storm that starves the timer.
    if (blk_dev.isr_addr)
        (void)*blk_dev.isr_addr;

    while (blk_dev.last_used_idx != blk_dev.used->idx) {
        blk_dev.last_used_idx++;
    }
    proc_wakeup(&blk_dev);
}
