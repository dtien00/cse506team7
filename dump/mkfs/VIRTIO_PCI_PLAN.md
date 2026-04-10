# VirtIO PCI Transport — Debug Log Analysis & Fix Plan

## Current status (from boot log)

```
pci: found device %04x:%04x at bus=0 dev=6900 fn=0
...
pci: assigned BAR[4] = 0x40000000 (size=0x4000)   ← BAR assignment works
virtio_blk: CommonCfg @ 0x%l, Notify @ 0x%l, DevCfg @ 0x%l  ← addresses resolved
virtio_blk: queue not available                    ← cfg->queue_size returns 0
```

BAR assignment and capability address resolution are **working correctly**.
The failure is at `cfg->queue_size` returning 0 after `cfg->queue_select = 0`.

---

## Root cause analysis

### Bug 1 (cosmetic, not functional): printk doesn't support `%x`/`%lx`/`%04x`/`%llu`

`printk.c` only supports `%c`, `%s`, `%d`, `%ld`, `%p`.

When it encounters `%l` followed by anything other than `d` (e.g. `%lx`), it:
1. Prints `%l` literally
2. Does **not** consume the va_arg argument

This means `pci_bar_base`'s debug prints are garbled, and device/notify/devcfg addresses
are printed as `0x%l` instead of their actual values. This is **cosmetic only** — it does
not corrupt the actual address values stored in variables. The logic works despite the
bad output.

Fix: either add `%x`/`%lx` support to `printk.c`, or change format strings to use
supported specifiers (`%ld`/`%p`).

### Bug 2 (root cause of init failure): VIRTIO_F_VERSION_1 not negotiated

`virtio_blk_init()` at line 65-66 negotiates **zero features**:
```c
cfg->driver_feature_select = 0;
cfg->driver_feature = features & 0x00000000UL;  // ← no features set!
```

For a `virtio-blk-pci-non-transitional` device, **`VIRTIO_F_VERSION_1` (bit 32) is
mandatory**. It lives in feature word 1 (select=1), not word 0. The driver never writes
feature word 1, so VERSION_1 is never acknowledged.

QEMU may accept FEATURES_OK without VERSION_1 (lenient), but with it un-negotiated the
device enters a legacy-compatibility or undefined state where virtqueue initialization
fails — specifically `queue_size` reads back as 0.

`VIRTIO_F_VERSION_1` is bit 32 of the full 64-bit feature set, which in the word-based
register scheme is: `driver_feature_select = 1`, `driver_feature |= (1 << 0)` (bit 0
of word 1 = bit 32 overall).

### Bug 3 (possible race): missing memory barrier between queue_select and queue_size read

RISC-V is a weakly-ordered architecture. After:
```c
cfg->queue_select = 0;
uint16_t queue_max = cfg->queue_size;  // may see stale value
```
Without a `fence` / `__sync_synchronize()`, the CPU may reorder the store and load,
or the device may not see the write before we read. The `volatile` qualifier prevents
compiler reordering but does not emit a fence instruction.

This is a secondary concern — Bug 2 is almost certainly the primary failure — but a
fence should be added for correctness.

---

## Fix plan

### Fix 1 — Negotiate VIRTIO_F_VERSION_1 in `virtio_blk_init`

File: [kernel/drivers/virtio_blk.c](../../kernel/drivers/virtio_blk.c), lines 61–75.

Replace the feature negotiation block:

```c
// Before (wrong — negotiates 0 features, misses mandatory VERSION_1):
cfg->device_feature_select = 0;
uint32_t features = cfg->device_feature;
cfg->driver_feature_select = 0;
cfg->driver_feature = features & 0x00000000UL;
```

```c
// After — negotiate VERSION_1 (mandatory) from word 1:
cfg->device_feature_select = 0;
// (read word 0 features if needed for future feature flags)
cfg->driver_feature_select = 0;
cfg->driver_feature = 0;  // no optional word-0 features

// VIRTIO_F_VERSION_1 = bit 32 = bit 0 of word 1
cfg->device_feature_select = 1;
cfg->driver_feature_select = 1;
cfg->driver_feature = (1 << 0);  // bit 32 of full feature set = VERSION_1
```

Add the define to `virtio.h`:
```c
#define VIRTIO_F_VERSION_1   (1 << 0)  // in feature word 1 (bit 32 overall)
```

### Fix 2 — Add memory barrier after queue_select write

File: [kernel/drivers/virtio_blk.c](../../kernel/drivers/virtio_blk.c), line 78.

```c
// Before:
cfg->queue_select = 0;
uint16_t queue_max = cfg->queue_size;

// After:
cfg->queue_select = 0;
__sync_synchronize();  // fence: ensure device sees queue_select before we read queue_size
uint16_t queue_max = cfg->queue_size;
```

### Fix 3 — Fix printk format strings (cosmetic, helps debugging)

Option A: Add `%x` and `%lx` support to `printk.c`.

Add a case in the `switch` after `case 'l'`:
```c
case 'x': {
    uint64_t x = va_arg(args, unsigned int);
    put_hex_buf(buf, &idx, x, 8);
    break;
}
```
And extend `case 'l':` to handle `%lx`:
```c
case 'l': {
    p++;
    if (*p == 'd') {
        const long v = va_arg(args, long);
        put_i64_buf(buf, &idx, v);
    } else if (*p == 'x') {
        const unsigned long v = va_arg(args, unsigned long);
        put_hex_buf(buf, &idx, v, 16);
    } else if (*p == 'l' && *(p+1) == 'u') {
        p++;
        const unsigned long long v = va_arg(args, unsigned long long);
        // print as decimal via put_i64_buf
        put_i64_buf(buf, &idx, (int64_t)v);
    } else {
        putc_buf(buf, &idx, '%');
        putc_buf(buf, &idx, 'l');
    }
    break;
}
```

Option B (simpler): Change format strings in pci.c and virtio_blk.c to use `%ld`/`%p`
instead of `%lx`/`%08x`/`%llu`. The values will print as decimal but at least correctly.

Option B is lower risk. Fix the 5 affected printk calls:
- `pci.c:38`: `dev=%d fn=0` is fine, but `%04x:%04x` for vid/did → use `%d:%d`
- `pci.c:75`: `0x%lx` for base → use `%ld`
- `pci.c:91`: `%08x %08x 0x%lx` → use `%ld %ld %ld`
- `pci.c:131-132`: `%ld %ld %ld` — already `%ld`, ok if those lines are uncommented
- `virtio_blk.c:43`: `%ld` — already supported
- `virtio_blk.c:141`: `%llu` for capacity → use `%ld`

---

## Expected result after fixes

```
pci: found device 6af4:4210 at bus=0 dev=27 fn=0   ← decimal now but correct
pci: assigned BAR[4] = 1073741824 (size=16384)
virtio_blk: CommonCfg @ 1073741824, Notify @ 1073754112, DevCfg @ 1073750016
virtio_blk: queue size = 256 (max 256)             ← queue_size now non-zero
virtio_blk: initialized, capacity = N sectors (M MB)
```

---

## Files to change

| File | Change |
|------|--------|
| `kernel/drivers/virtio_blk.c` | Negotiate VIRTIO_F_VERSION_1 (word 1); add `__sync_synchronize()` after queue_select |
| `kernel/include/drivers/virtio.h` | Add `VIRTIO_F_VERSION_1` define |
| `kernel/core/printk.c` | Add `%x`/`%lx`/`%llu` support (optional but useful) |
| `kernel/drivers/pci.c` | Fix format strings (optional, cosmetic) |

---

## What was already correct

- BAR detection and assignment (`pci_bar_base`, `pci_assign_bar`) — works correctly
- PCI capability list walking (`pci_virtio_cap_addr`) — works correctly
- PCI MMIO window mapped in `vmm_init` at `0x40000000–0x50000000` — already done
- Virtqueue descriptor/available/used ring layout — correct
- `virtio_pci_common_cfg_t` struct layout — matches VirtIO 1.1 spec exactly
- FEATURES_OK check — correct gating logic
