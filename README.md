# CRaidVolume

Software implementation of a **RAID 5 controller** in **C++14**, providing block-level read/write operations with resilience against single-disk failure. This project simulates RAID 5 functionality using provided disk I/O functions.

---

## Features

* Supports RAID 5 on **n ≥ 3 disks**
* Handles disk failure with **degraded mode**
* Parity distributed evenly across all disks
* Provides **block-level read and write operations**
* Supports RAID initialization, start/stop, and resynchronization
* Fully tested with simulated disks

---

## Functionality Overview

* **Initialization** — create RAID and write overhead blocks (`create`)
* **Start / Stop** — assemble or pause RAID (`start`, `stop`)
* **Read / Write** — sector-level operations with automatic parity handling (`read`, `write`)
* **Resync** — recover data on a replaced or failed disk (`resync`)
* **Status & Capacity** — query RAID state and usable sector count (`status`, `size`)

---

## Example Usage

```cpp
TBlkDev dev = createDisks();

// Create RAID metadata on disks
assert(CRaidVolume::create(dev));

CRaidVolume vol;
assert(vol.start(dev) == RAID_OK);
assert(vol.status() == RAID_OK);

// Read and write all sectors
for (int i = 0; i < vol.size(); i++) {
    char buffer[SECTOR_SIZE];
    assert(vol.read(i, buffer, 1));
    assert(vol.write(i, buffer, 1));
}
assert(vol.status() == RAID_OK);

assert(vol.stop() == RAID_STOPPED);
assert(vol.status() == RAID_STOPPED);

doneDisks();
```

---

## RAID States

* **RAID_OK** — all disks functional
* **RAID_DEGRADED** — one disk failed; data recoverable
* **RAID_FAILED** — two or more disks failed; data lost
* **RAID_STOPPED** — RAID not started or stopped

---

## Build & Run

### Requirements

* `g++` with **C++14** support
* `make`

### Commands

```bash
make test     # Build and run unit tests
make clean    # Remove build artifacts
```

---

## Notes

* All I/O operations are **sector-based** (`SECTOR_SIZE` = 512B)
* Parity is **evenly distributed** to balance I/O load
* Degraded reads/writes are **automatically reconstructed using XOR parity**
* Capacity is `(num_disks - 1) * (sectors_per_disk - 1)`
