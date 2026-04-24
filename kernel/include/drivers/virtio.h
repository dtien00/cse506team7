#pragma once
#include "kernel/types.h"

// VirtIO MMIO Register Layout (from VirtIO spec 1.0)
#define VIRTIO_MMIO_MAGIC_VALUE         0x000  // 0x74726976 ('virt')
#define VIRTIO_MMIO_VERSION             0x004  // Version (should be 2)
#define VIRTIO_MMIO_DEVICE_ID           0x008  // Device ID (2 = block device)
#define VIRTIO_MMIO_VENDOR_ID           0x00c  // Vendor ID
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010  // Device features
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014  // Device features selector
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020  // Driver features
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024  // Driver features selector
#define VIRTIO_MMIO_QUEUE_SEL           0x030  // Queue selector
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034  // Max queue size
#define VIRTIO_MMIO_QUEUE_NUM           0x038  // Queue size
#define VIRTIO_MMIO_QUEUE_READY         0x044  // Queue ready
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050  // Queue notify
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060  // Interrupt status
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064  // Interrupt acknowledge
#define VIRTIO_MMIO_STATUS              0x070  // Device status
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080  // Queue descriptor low 32 bits
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084  // Queue descriptor high 32 bits
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW    0x090  // Queue driver low 32 bits
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH   0x094  // Queue driver high 32 bits
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW    0x0a0  // Queue device low 32 bits
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH   0x0a4  // Queue device high 32 bits
#define VIRTIO_MMIO_CONFIG_GENERATION   0x0fc  // Configuration atomicity
#define VIRTIO_MMIO_CONFIG              0x100  // Device-specific config

// VirtIO status bits
#define VIRTIO_STATUS_ACKNOWLEDGE       1
#define VIRTIO_STATUS_DRIVER            2
#define VIRTIO_STATUS_DRIVER_OK         4
#define VIRTIO_STATUS_FEATURES_OK       8
#define VIRTIO_STATUS_FAILED            128

// VirtIO transport feature bits (in feature word 1)
#define VIRTIO_F_VERSION_1              (1 << 0)  // bit 32 of full feature set; select=1, bit=0

// VirtIO block device feature bits
#define VIRTIO_BLK_F_SIZE_MAX           1
#define VIRTIO_BLK_F_SEG_MAX            2
#define VIRTIO_BLK_F_GEOMETRY           4
#define VIRTIO_BLK_F_RO                 5
#define VIRTIO_BLK_F_BLK_SIZE           6
#define VIRTIO_BLK_F_FLUSH              9
#define VIRTIO_BLK_F_TOPOLOGY           10
#define VIRTIO_BLK_F_CONFIG_WCE         11

// VirtIO block request types
#define VIRTIO_BLK_T_IN                 0  // Read
#define VIRTIO_BLK_T_OUT                1  // Write
#define VIRTIO_BLK_T_FLUSH              4  // Flush

// VirtIO block status codes
#define VIRTIO_BLK_S_OK                 0
#define VIRTIO_BLK_S_IOERR              1
#define VIRTIO_BLK_S_UNSUPP             2

// Virtqueue descriptor flags
#define VIRTQ_DESC_F_NEXT               1  // Buffer continues via next field
#define VIRTQ_DESC_F_WRITE              2  // Device writes (vs reads)
#define VIRTQ_DESC_F_INDIRECT           4  // Buffer contains list of descriptors

// Virtqueue used flags
#define VIRTQ_USED_F_NO_NOTIFY          1

// Virtqueue available flags
#define VIRTQ_AVAIL_F_NO_INTERRUPT      1

#define VIRTIO_RING_SIZE                8  // Number of queue entries

// VirtIO queue descriptor
typedef struct virtq_desc {
    uint64_t addr;   // Physical address
    uint32_t len;    // Length
    uint16_t flags;  // Flags (VIRTQ_DESC_F_*)
    uint16_t next;   // Next descriptor index (if VIRTQ_DESC_F_NEXT)
} virtq_desc_t;

// VirtIO available ring
typedef struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_RING_SIZE];
    uint16_t used_event;  // Only if VIRTIO_F_EVENT_IDX
} virtq_avail_t;

// VirtIO used ring element
typedef struct virtq_used_elem {
    uint32_t id;     // Descriptor chain head
    uint32_t len;    // Bytes written
} virtq_used_elem_t;

// VirtIO used ring
typedef struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTIO_RING_SIZE];
    uint16_t avail_event;  // Only if VIRTIO_F_EVENT_IDX
} virtq_used_t;

// VirtIO block request header
typedef struct virtio_blk_req {
    uint32_t type;      // VIRTIO_BLK_T_*
    uint32_t reserved;
    uint64_t sector;    // 512-byte sector number
} virtio_blk_req_t;

// VirtIO block device info
typedef struct virtio_blk_config {
    uint64_t capacity;      // Device capacity in 512-byte sectors
    uint32_t size_max;      // Max segment size
    uint32_t seg_max;       // Max number of segments
    uint16_t cylinders;     // Geometry cylinders
    uint8_t heads;          // Geometry heads
    uint8_t sectors;        // Geometry sectors
    uint32_t blk_size;      // Block size
} virtio_blk_config_t;

// VirtIO PCI CommonCfg layout (VirtIO spec 1.1 section 4.1.4.3)
typedef struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t config_msix_vector;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_driver;
    uint64_t queue_device;
} __attribute__((packed)) virtio_pci_common_cfg_t;

// VirtIO block device state
typedef struct virtio_blk_device {
    volatile virtio_pci_common_cfg_t *common_cfg;  // PCI CommonCfg region
    uintptr_t notify_addr;                          // Notify register address
    volatile uint32_t *dev_cfg;                    // Device-specific config region
    volatile uint8_t *isr_addr;                    // ISR status register (read clears interrupt)
    volatile virtq_desc_t *desc;   // Descriptor table
    volatile virtq_avail_t *avail; // Available ring
    volatile virtq_used_t *used;   // Used ring
    uint16_t last_used_idx;        // Last processed used index
    uint64_t capacity;             // Device capacity in sectors
    uint32_t block_size;           // Block size (usually 512)
    int busy;                      // 1 while a request is in flight
    uint32_t irq;                  // PLIC IRQ number (read from PCI interrupt_line)
} virtio_blk_dev_t;

// Function declarations
int virtio_blk_init(void);
int virtio_blk_read(uint64_t sector, void *buf, size_t count);
int virtio_blk_write(uint64_t sector, const void *buf, size_t count);
int virtio_blk_flush(void);
uint64_t virtio_blk_get_capacity(void);
void virtio_blk_irq(void);      // called by trap handler on virtio IRQ
void virtio_blk_poll(void);     // called from timer to catch missed IRQs
uint32_t virtio_blk_get_irq(void); // return PLIC IRQ number for trap handler
