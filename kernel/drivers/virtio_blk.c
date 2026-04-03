#include "drivers/virtio.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "mm/pmm.h"
#include "lib/kstring.h"

static virtio_blk_dev_t blk_dev;
static int initialized = 0;

// Helper: Read 32-bit MMIO register
static inline uint32_t virtio_read32(volatile uint32_t *base, uint32_t offset) {
    return base[offset / 4];
}

// Helper: Write 32-bit MMIO register
static inline void virtio_write32(volatile uint32_t *base, uint32_t offset, uint32_t value) {
    base[offset / 4] = value;
}

// Helper: Read 64-bit config value
static inline uint64_t virtio_read_config64(volatile uint32_t *base, uint32_t offset) {
    uint32_t low = virtio_read32(base, VIRTIO_MMIO_CONFIG + offset);
    uint32_t high = virtio_read32(base, VIRTIO_MMIO_CONFIG + offset + 4);
    return ((uint64_t)high << 32) | low;
}

// Initialize VirtIO block device
int virtio_blk_init(uintptr_t mmio_base) {
    printk("virtio_blk: initializing at 0x%lx\n", mmio_base);
    
    blk_dev.mmio_base = (volatile uint32_t *)mmio_base;
    
    // Step 1: Check magic value
    uint32_t magic = virtio_read32(blk_dev.mmio_base, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != 0x74726976) {
        printk("virtio_blk: invalid magic value 0x%x\n", magic);
        return -1;
    }
    
    // Step 2: Check version
    uint32_t version = virtio_read32(blk_dev.mmio_base, VIRTIO_MMIO_VERSION);
    if (version != 2) {
        printk("virtio_blk: unsupported version %d\n", version);
        return -1;
    }
    
    // Step 3: Check device ID (2 = block device)
    uint32_t device_id = virtio_read32(blk_dev.mmio_base, VIRTIO_MMIO_DEVICE_ID);
    if (device_id != 2) {
        printk("virtio_blk: wrong device ID %d (expected 2)\n", device_id);
        return -1;
    }
    
    printk("virtio_blk: found block device (version %d)\n", version);
    
    // Step 4: Reset device
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_STATUS, 0);
    
    // Step 5: Set ACKNOWLEDGE status bit
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    
    // Step 6: Set DRIVER status bit
    uint32_t status = virtio_read32(blk_dev.mmio_base, VIRTIO_MMIO_STATUS);
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER);
    
    // Step 7: Read device features (we'll accept all for simplicity)
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    uint32_t features = virtio_read32(blk_dev.mmio_base, VIRTIO_MMIO_DEVICE_FEATURES);
    printk("virtio_blk: device features = 0x%x\n", features);
    
    // Step 8: Write driver features (accept all)
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_DRIVER_FEATURES, features);
    
    // Step 9: Set FEATURES_OK
    status = virtio_read32(blk_dev.mmio_base, VIRTIO_MMIO_STATUS);
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FEATURES_OK);
    
    // Step 10: Re-read status to ensure FEATURES_OK is still set
    status = virtio_read32(blk_dev.mmio_base, VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        printk("virtio_blk: device doesn't support our features\n");
        return -1;
    }
    
    // Step 11: Setup virtqueue (queue 0)
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_SEL, 0);
    
    uint32_t queue_max = virtio_read32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_max == 0) {
        printk("virtio_blk: queue not available\n");
        return -1;
    }
    
    printk("virtio_blk: queue max size = %d\n", queue_max);
    
    // Use smaller queue size for simplicity
    uint32_t queue_size = (queue_max < VIRTIO_RING_SIZE) ? queue_max : VIRTIO_RING_SIZE;
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_NUM, queue_size);
    
    // Allocate descriptor table (16 bytes per descriptor)
    blk_dev.desc = (virtq_desc_t *)kmalloc(queue_size * sizeof(virtq_desc_t));
    if (!blk_dev.desc) {
        printk("virtio_blk: failed to allocate descriptor table\n");
        return -1;
    }
    memset(blk_dev.desc, 0, queue_size * sizeof(virtq_desc_t));
    
    // Allocate available ring (6 + 2*queue_size bytes)
    blk_dev.avail = (virtq_avail_t *)kmalloc(sizeof(virtq_avail_t));
    if (!blk_dev.avail) {
        printk("virtio_blk: failed to allocate available ring\n");
        kfree(blk_dev.desc);
        return -1;
    }
    memset(blk_dev.avail, 0, sizeof(virtq_avail_t));
    
    // Allocate used ring (6 + 8*queue_size bytes)
    blk_dev.used = (virtq_used_t *)kmalloc(sizeof(virtq_used_t));
    if (!blk_dev.used) {
        printk("virtio_blk: failed to allocate used ring\n");
        kfree(blk_dev.desc);
        kfree(blk_dev.avail);
        return -1;
    }
    memset(blk_dev.used, 0, sizeof(virtq_used_t));
    
    blk_dev.last_used_idx = 0;
    
    // Tell device about queue addresses (physical addresses)
    uintptr_t desc_pa = (uintptr_t)blk_dev.desc;
    uintptr_t avail_pa = (uintptr_t)blk_dev.avail;
    uintptr_t used_pa = (uintptr_t)blk_dev.used;
    
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_pa);
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_pa >> 32));
    
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_DRIVER_LOW, (uint32_t)avail_pa);
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_DRIVER_HIGH, (uint32_t)(avail_pa >> 32));
    
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_DEVICE_LOW, (uint32_t)used_pa);
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_DEVICE_HIGH, (uint32_t)(used_pa >> 32));
    
    // Mark queue as ready
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_READY, 1);
    
    // Step 12: Set DRIVER_OK status bit
    status = virtio_read32(blk_dev.mmio_base, VIRTIO_MMIO_STATUS);
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER_OK);
    
    // Read device configuration
    blk_dev.capacity = virtio_read_config64(blk_dev.mmio_base, 0);
    blk_dev.block_size = 512;  // Standard sector size
    
    printk("virtio_blk: initialized, capacity = %llu sectors (%llu MB)\n",
           blk_dev.capacity, (blk_dev.capacity * 512) / (1024 * 1024));
    
    initialized = 1;
    return 0;
}

// Perform a block I/O operation
static int virtio_blk_rw(uint64_t sector, void *buf, size_t count, int write) {
    if (!initialized) {
        printk("virtio_blk: device not initialized\n");
        return -1;
    }
    
    if (sector + count > blk_dev.capacity) {
        printk("virtio_blk: sector %llu out of range\n", sector);
        return -1;
    }
    
    // Allocate request header
    virtio_blk_req_t *req = (virtio_blk_req_t *)kmalloc(sizeof(virtio_blk_req_t));
    if (!req) return -1;
    
    req->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector = sector;
    
    // Allocate status byte
    uint8_t *status = (uint8_t *)kmalloc(1);
    if (!status) {
        kfree(req);
        return -1;
    }
    *status = 0xFF;
    
    // Setup descriptor chain: [header] -> [data] -> [status]
    uint16_t desc_idx = 0;
    
    // Descriptor 0: Request header (device reads)
    blk_dev.desc[0].addr = (uintptr_t)req;
    blk_dev.desc[0].len = sizeof(virtio_blk_req_t);
    blk_dev.desc[0].flags = VIRTQ_DESC_F_NEXT;
    blk_dev.desc[0].next = 1;
    
    // Descriptor 1: Data buffer (device reads for write, writes for read)
    blk_dev.desc[1].addr = (uintptr_t)buf;
    blk_dev.desc[1].len = count * 512;
    blk_dev.desc[1].flags = VIRTQ_DESC_F_NEXT | (write ? 0 : VIRTQ_DESC_F_WRITE);
    blk_dev.desc[1].next = 2;
    
    // Descriptor 2: Status byte (device writes)
    blk_dev.desc[2].addr = (uintptr_t)status;
    blk_dev.desc[2].len = 1;
    blk_dev.desc[2].flags = VIRTQ_DESC_F_WRITE;
    blk_dev.desc[2].next = 0;
    
    // Add to available ring
    uint16_t avail_idx = blk_dev.avail->idx;
    blk_dev.avail->ring[avail_idx % VIRTIO_RING_SIZE] = desc_idx;
    
    // Memory barrier to ensure descriptors are written before updating idx
    __sync_synchronize();
    
    blk_dev.avail->idx = avail_idx + 1;
    
    // Memory barrier before notifying device
    __sync_synchronize();
    
    // Notify device (write queue number to QUEUE_NOTIFY)
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    
    // Poll for completion (busy wait - not ideal but simple)
    int timeout = 100000;
    while (blk_dev.last_used_idx == blk_dev.used->idx && timeout-- > 0) {
        // Busy wait
    }
    
    if (timeout <= 0) {
        printk("virtio_blk: request timeout\n");
        kfree(req);
        kfree(status);
        return -1;
    }
    
    // Check status
    if (*status != VIRTIO_BLK_S_OK) {
        printk("virtio_blk: request failed with status %d\n", *status);
        kfree(req);
        kfree(status);
        return -1;
    }
    
    // Update last used index
    blk_dev.last_used_idx++;
    
    kfree(req);
    kfree(status);
    
    return 0;
}

// Read sectors from block device
int virtio_blk_read(uint64_t sector, void *buf, size_t count) {
    return virtio_blk_rw(sector, buf, count, 0);
}

// Write sectors to block device
int virtio_blk_write(uint64_t sector, const void *buf, size_t count) {
    return virtio_blk_rw(sector, (void *)buf, count, 1);
}

// Flush write cache
int virtio_blk_flush(void) {
    if (!initialized) return -1;
    
    // Allocate request header
    virtio_blk_req_t *req = (virtio_blk_req_t *)kmalloc(sizeof(virtio_blk_req_t));
    if (!req) return -1;
    
    req->type = VIRTIO_BLK_T_FLUSH;
    req->reserved = 0;
    req->sector = 0;
    
    // Allocate status byte
    uint8_t *status = (uint8_t *)kmalloc(1);
    if (!status) {
        kfree(req);
        return -1;
    }
    *status = 0xFF;
    
    // Setup descriptor chain: [header] -> [status]
    blk_dev.desc[0].addr = (uintptr_t)req;
    blk_dev.desc[0].len = sizeof(virtio_blk_req_t);
    blk_dev.desc[0].flags = VIRTQ_DESC_F_NEXT;
    blk_dev.desc[0].next = 1;
    
    blk_dev.desc[1].addr = (uintptr_t)status;
    blk_dev.desc[1].len = 1;
    blk_dev.desc[1].flags = VIRTQ_DESC_F_WRITE;
    blk_dev.desc[1].next = 0;
    
    // Add to available ring
    uint16_t avail_idx = blk_dev.avail->idx;
    blk_dev.avail->ring[avail_idx % VIRTIO_RING_SIZE] = 0;
    
    __sync_synchronize();
    blk_dev.avail->idx = avail_idx + 1;
    __sync_synchronize();
    
    // Notify device
    virtio_write32(blk_dev.mmio_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    
    // Poll for completion
    int timeout = 100000;
    while (blk_dev.last_used_idx == blk_dev.used->idx && timeout-- > 0) {
    }
    
    if (timeout <= 0) {
        printk("virtio_blk: flush timeout\n");
        kfree(req);
        kfree(status);
        return -1;
    }
    
    int ret = (*status == VIRTIO_BLK_S_OK) ? 0 : -1;
    blk_dev.last_used_idx++;
    
    kfree(req);
    kfree(status);
    
    return ret;
}

// Get device capacity in sectors
uint64_t virtio_blk_get_capacity(void) {
    return initialized ? blk_dev.capacity : 0;
}
