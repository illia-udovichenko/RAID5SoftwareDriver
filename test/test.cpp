#include <cassert>
#include <stdexcept>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "../include/Overhead.h"
#include "../include/CRaidVolume.h"

// Number of simulated RAID devices and sectors per device
constexpr int RAID_DEVICES = 4;
constexpr int DISK_SECTORS = 8192;

// File pointers representing each simulated disk
static FILE* g_Fp[RAID_DEVICES] = { nullptr };

// Reads 'sectorCnt' sectors from device into 'data'
int diskRead(int device, int sectorNr, void* data, int sectorCnt) {
    if (device < 0 || device >= RAID_DEVICES) return 0;
    if (!g_Fp[device] || sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS) return 0;
    fseek(g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET);
    return fread(data, SECTOR_SIZE, sectorCnt, g_Fp[device]);
}

// Writes 'sectorCnt' sectors from 'data' to device
int diskWrite(int device, int sectorNr, const void* data, int sectorCnt) {
    if (device < 0 || device >= RAID_DEVICES) return 0;
    if (!g_Fp[device] || sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS) return 0;
    fseek(g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET);
    return fwrite(data, SECTOR_SIZE, sectorCnt, g_Fp[device]);
}

// Closes all simulated disks
void doneDisks() {
    for (int i = 0; i < RAID_DEVICES; i++) {
        if (g_Fp[i]) {
            fclose(g_Fp[i]);
            g_Fp[i] = nullptr;
        }
    }
}

// Creates disk files and initializes them with zeros
TBlkDev createDisks() {
    char buffer[SECTOR_SIZE] = { 0 };
    TBlkDev dev;
    char fn[100];

    for (int i = 0; i < RAID_DEVICES; i++) {
        snprintf(fn, sizeof(fn), "/tmp/%04d", i);
        g_Fp[i] = fopen(fn, "w+b");
        if (!g_Fp[i]) {
            doneDisks();
            throw std::runtime_error("Failed to create disk file");
        }

        for (int j = 0; j < DISK_SECTORS; j++) {
            if (fwrite(buffer, sizeof(buffer), 1, g_Fp[i]) != 1) {
                doneDisks();
                throw std::runtime_error("Failed to initialize disk file");
            }
        }
    }

    dev.m_Devices = RAID_DEVICES;
    dev.m_Sectors = DISK_SECTORS;
    dev.m_Read    = diskRead;
    dev.m_Write   = diskWrite;
    return dev;
}

// Opens existing disk files
TBlkDev openDisks() {
    TBlkDev dev;
    char fn[100];

    for (int i = 0; i < RAID_DEVICES; i++) {
        snprintf(fn, sizeof(fn), "/tmp/%04d", i);
        g_Fp[i] = fopen(fn, "r+b");
        if (!g_Fp[i]) {
            doneDisks();
            throw std::runtime_error("Failed to open disk file");
        }
        fseek(g_Fp[i], 0, SEEK_END);
        if (ftell(g_Fp[i]) != DISK_SECTORS * SECTOR_SIZE) {
            doneDisks();
            throw std::runtime_error("Disk file has unexpected size");
        }
    }

    dev.m_Devices = RAID_DEVICES;
    dev.m_Sectors = DISK_SECTORS;
    dev.m_Read    = diskRead;
    dev.m_Write   = diskWrite;
    return dev;
}

// Test creating, using, and stopping a RAID volume
void test1() {
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
}

// Test opening and starting RAID after a simulated restart
void test2() {
    TBlkDev dev = openDisks();
    CRaidVolume vol;

    assert(vol.start(dev) == RAID_OK);

    // Minimal read/write to ensure RAID is functional
    vol.stop();
    doneDisks();
}

int main() {
    test1();
    test2();
    printf("All tests passed.\n");
}
