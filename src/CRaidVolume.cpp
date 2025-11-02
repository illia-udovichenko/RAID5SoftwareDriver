#include <cstdio>
#include <cmath>
#include <cstring>
#include "../include/Overhead.h"
#include "../include/CRaidVolume.h"

using namespace std;

// Constructor: initialize RAID state to stopped
CRaidVolume::CRaidVolume() {
    raid.state = RAID_STOPPED;
}

// Create RAID: write overhead info to the last sector of each disk
bool CRaidVolume::create(const TBlkDev &dev) {
    int diskCnt = dev.m_Devices;
    int lastSec = dev.m_Sectors - 1;
    int timestamp = 1;
    void *ptr = &timestamp;
    bool valid = true;

    for (int i = 0; i < diskCnt; i++)
        if (!dev.m_Write(i, lastSec, ptr, 1))
            valid = false; // failed to write overhead

    return valid;
}

// Start RAID: read overhead, detect degraded/failed state
int CRaidVolume::start(const TBlkDev &dev) {
    raid.dev = dev;
    int diskCnt = dev.m_Devices;
    int lastSec = dev.m_Sectors - 1;
    unsigned char buffer[SECTOR_SIZE];

    // Read first three disks' overhead
    Overhead overhead[3];
    for (int i = 0; i < 3; i++) {
        if (dev.m_Read(i, lastSec, buffer, 1)) {
            // Successfully read
            overhead[i] = readFromBuffer(buffer);
            if (overhead[i].state == RAID_FAILED)
                return raid.state = RAID_FAILED;
            if (overhead[i].state == RAID_DEGRADED) {
                raid.state = RAID_DEGRADED;
                raid.failedDisk = overhead[i].failedDisk;
            }
        } else if (raid.state == RAID_OK) {
            // Store failed disk
            raid.state = RAID_DEGRADED;
            raid.failedDisk = i;
        } else
            // Two disks are unreadable
            return raid.state = RAID_FAILED;
    }

    // Compare timestamps to find correct version
    int timestamps[3];
    for (int i = 0; i < 3; i++)
        timestamps[i] = overhead[i].timestamp;

    if (raid.state == RAID_DEGRADED) {
        // One degraded disk -> compare two correct disks
        if (raid.failedDisk == 0) {
            if (timestamps[1] == timestamps[2])
                raid.timestamp = timestamps[1];
            else
                return raid.state = RAID_FAILED; // two degraded disks
        } else if (raid.failedDisk == 1) {
            if (timestamps[0] == timestamps[2])
                raid.timestamp = timestamps[0];
            else
                return raid.state = RAID_FAILED;
        } else {
            if (timestamps[0] == timestamps[1])
                raid.timestamp = timestamps[0];
            else
                return raid.state = RAID_FAILED;
        }
    }

    // Determine state if all disks readable
    if (timestamps[0] == timestamps[1]) {
        raid.timestamp = timestamps[0];
        if (timestamps[1] == timestamps[2]) {
            raid.state = RAID_OK;
            raid.failedDisk = NO_DISK;
        } else {
            raid.failedDisk = 2;
            raid.state = RAID_DEGRADED;
        }
    } else if (timestamps[1] == timestamps[2]) {
        raid.timestamp = timestamps[1];
        raid.failedDisk = 0;
        raid.state = RAID_DEGRADED;
    } else if (timestamps[0] == timestamps[2]) {
        raid.timestamp = timestamps[0];
        raid.failedDisk = 1;
        raid.state = RAID_DEGRADED;
    } else
        return raid.state = RAID_FAILED;

    // Check remaining disks for consistency
    int timestamp = raid.timestamp;
    for (int i = 3; i < diskCnt; i++) {
        if (!dev.m_Read(i, lastSec, buffer, 1) || readFromBuffer(buffer).timestamp != timestamp) {
            if (raid.state == RAID_OK) {
                // Store failed disk
                raid.failedDisk = i;
                raid.state = RAID_DEGRADED;
            } else
                return raid.state = RAID_FAILED; // second failed disk
        }
    }

    return raid.state;
}

// Stop RAID: update overhead and mark stopped
int CRaidVolume::stop() {
    if (raid.state == RAID_STOPPED)
        return RAID_STOPPED;

    int diskCnt = raid.dev.m_Devices;
    int lastSec = raid.dev.m_Sectors - 1;
    raid.timestamp++;

    // Copy overhead data to buffer
    unsigned char buffer[SECTOR_SIZE];
    memcpy(buffer, &raid.timestamp, sizeof(int));
    memcpy(buffer + sizeof(int), &raid.state, sizeof(int));
    memcpy(buffer + sizeof(int) * 2, &raid.failedDisk, sizeof(int));

    // If failed, update first three disks only
    if (raid.state == RAID_FAILED) {
        raid.dev.m_Write(0, lastSec, buffer, 1);
        raid.dev.m_Write(1, lastSec, buffer, 1);
        raid.dev.m_Write(2, lastSec, buffer, 1);
        return raid.state = RAID_STOPPED;
    }

    // Write overhead to all disks except failed
    int failedDisk = raid.failedDisk;
    for (int i = 0; i < diskCnt; i++) {
        if (i != failedDisk)
            if (!raid.dev.m_Write(i, lastSec, buffer, 1)) {
                // Failed to write
                if (raid.state == RAID_DEGRADED) { // cannot write, mark failed
                    raid.state = RAID_FAILED;
                    for (int j = 0; j < 3; j++) raid.dev.m_Write(j, lastSec, buffer, 1);
                    return raid.state = RAID_STOPPED;
                } else if (raid.state == RAID_OK) {
                    raid.state = RAID_DEGRADED;
                    raid.failedDisk = i;
                    memcpy(buffer + sizeof(int), &raid.state, sizeof(int));
                    memcpy(buffer + sizeof(int) * 2, &raid.failedDisk, sizeof(int));
                    i = 0; // rewrite all disks
                }
            }
    }

    return raid.state = RAID_STOPPED;
}

// Resync RAID if degraded: rebuild missing disk
int CRaidVolume::resync() {
    if (raid.state == RAID_DEGRADED) {
        // Get previous data from remaining disks
        unsigned char previous[SECTOR_SIZE];
        unsigned char tmp[SECTOR_SIZE];
        int failedDisk = raid.failedDisk;

        // Evaluate data from remained disks and write it to new disk
        for (int sector = 0; sector < raid.dev.m_Sectors - 1; sector++) {
            // Get data from first valid disk
            if (failedDisk != 0) {
                if (!myRead(0, sector, previous))
                    break;
            } else {
                if (!myRead(1, sector, previous))
                    break;
            }

            // Next after first disk
            int disk = (failedDisk == 0) ? 2 : 1;

            // Evaluate previous data from remaining disks
            for (; disk < raid.dev.m_Devices; disk++) {
                if (disk != raid.failedDisk) {
                    // Get data from other disk
                    if (!myRead(disk, sector, tmp))
                        return raid.state;

                    // XOR it with other data
                    for (int byteNumber = 0; byteNumber < SECTOR_SIZE; ++byteNumber)
                        previous[byteNumber] ^= tmp[byteNumber];
                }
            }

            // Write evaluated previous data to renewed disk
            if (!myWrite(failedDisk, sector, previous))
                return raid.state = RAID_DEGRADED;
        }

        raid.state = RAID_OK;
        raid.failedDisk = NO_DISK;
    }

    return raid.state;
}

int CRaidVolume::status() const {
    return raid.state;
}

// Total usable sectors
int CRaidVolume::size() const {
    return (raid.dev.m_Sectors - 1) * (raid.dev.m_Devices - 1);
}

// Read RAID sectors, handle degraded/failure
bool CRaidVolume::read(int secNr, void *data, int secCnt) {
    Evaluation res{};
    int maxSec = secNr + secCnt;
    auto *dataPtr = (unsigned char *) data;

    if (raid.state == RAID_OK) {
        for (; secNr < maxSec; secNr++, dataPtr += SECTOR_SIZE) {
            res = findSector(secNr);
            if (!myRead(res.disk, res.sector, dataPtr))
                break;
        }
    }

    if (raid.state == RAID_DEGRADED) {
        for (; secNr < maxSec; secNr++, dataPtr += SECTOR_SIZE) {
            res = findSector(secNr);

            // Read from valid disk
            if (res.disk != raid.failedDisk && !myRead(res.disk, res.sector, dataPtr))
                break;

            // Read from broken disk
            if (res.disk == raid.failedDisk) {
                // Evaluate data from remaining disks
                int sector = res.sector;
                unsigned char buffer[SECTOR_SIZE];
                memset(buffer, 0, SECTOR_SIZE); // reset all bytes of the buffer

                // Iterate through each disk
                for (int disk = 0; disk < raid.dev.m_Devices; disk++) {
                    // Skip broken disk
                    if (disk == raid.failedDisk)
                        continue;

                    // Load data from the disk
                    unsigned char loadedData[SECTOR_SIZE];
                    if (!myRead(disk, sector, loadedData))
                        return false;

                    // XOR data from the disk with the buffer
                    for (int byteNumber = 0; byteNumber < SECTOR_SIZE; byteNumber++)
                        buffer[byteNumber] ^= loadedData[byteNumber];
                }

                // Write evaluated data to output data
                memcpy(dataPtr, buffer, SECTOR_SIZE);
            }
        }
    }

    if (raid.state == RAID_FAILED || raid.state == RAID_STOPPED)
        return false;

    return true;
}

// Write RAID sectors with parity updates
bool CRaidVolume::write(int secNr, const void *data, int secCnt) {
    Evaluation res{};
    unsigned char previous[SECTOR_SIZE], parity[SECTOR_SIZE], input[SECTOR_SIZE];
    const unsigned char *dataPtr = (unsigned char *) data;
    int maxSec = secNr + secCnt;

    if (raid.state == RAID_OK) {
        for (; secNr < maxSec; secNr++, dataPtr += SECTOR_SIZE) {
            res = findSector(secNr);

            // Read previous data
            if (!myRead(res.disk, res.sector, previous))
                break;

            // Read data from parity disc
            if (!myRead(res.diskParity, res.sector, parity))
                break;

            // XOR parity disc with previous data
            for (int byteNumber = 0; byteNumber < SECTOR_SIZE; ++byteNumber)
                parity[byteNumber] ^= previous[byteNumber];

            // Get input data
            memcpy(input, dataPtr, SECTOR_SIZE);

            // XOR parity disc with new data
            for (int byteNumber = 0; byteNumber < SECTOR_SIZE; ++byteNumber)
                parity[byteNumber] ^= input[byteNumber];

            // Write new parity
            if (!myWrite(res.diskParity, res.sector, parity))
                break;

            // Write new data
            if (!myWrite(res.disk, res.sector, dataPtr))
                break;
        }
    }

    if (raid.state == RAID_DEGRADED) {
        for (; secNr < maxSec; secNr++, dataPtr += SECTOR_SIZE) {
            res = findSector(secNr);

            if (res.diskParity == raid.failedDisk) {
                // Parity disk is invalid => just write new data to appropriate disk
                if (!myWrite(res.disk, res.sector, dataPtr))
                    break;
                else
                    continue;
            }

            if (res.disk == raid.failedDisk) {
                // Get previous data from remaining disks
                unsigned char tmp[SECTOR_SIZE];

                // Get disk parity
                if (!myRead(res.diskParity, res.sector, previous))
                    break;

                // XOR parity with remaining disks
                for (int i = 0; i < raid.dev.m_Devices; i++) {
                    if (i != raid.failedDisk && i != res.diskParity) {
                        // Get data from other disk
                        if (!myRead(i, res.sector, tmp))
                            return false;

                        // XOR it with parity
                        for (int byteNumber = 0; byteNumber < SECTOR_SIZE; ++byteNumber)
                            previous[byteNumber] ^= tmp[byteNumber];
                    }
                }
            } else {
                // Read previous data
                if (!myRead(res.disk, res.sector, previous))
                    break;
            }

            // Read data from parity disc
            if (!myRead(res.diskParity, res.sector, parity))
                break;

            // XOR parity disc with previous data
            for (int byteNumber = 0; byteNumber < SECTOR_SIZE; ++byteNumber)
                parity[byteNumber] ^= previous[byteNumber];

            // Get input data
            memcpy(input, dataPtr, SECTOR_SIZE);

            // XOR parity disc with new data
            for (int byteNumber = 0; byteNumber < SECTOR_SIZE; ++byteNumber)
                parity[byteNumber] ^= input[byteNumber];

            // Write new parity
            if (!myWrite(res.diskParity, res.sector, parity))
                break;

            // Write new data
            if (res.disk != raid.failedDisk) {
                if (!myWrite(res.disk, res.sector, dataPtr))
                    break;
            }
        }
    }

    if (raid.state == RAID_FAILED || raid.state == RAID_STOPPED)
        return false;

    return true;
}

// --- Private helper functions ---

// Map logical sector to physical disk/sector and parity disk
CRaidVolume::Evaluation CRaidVolume::findSector(int input) const {
    Evaluation res{};
    res.sector = input / (raid.dev.m_Devices - 1);
    res.diskParity = res.sector % raid.dev.m_Devices;
    res.disk = input % (raid.dev.m_Devices - 1);
    if (res.disk >= res.diskParity)
        res.disk++;
    return res;
}

// Read a single sector and update RAID state if read fails
bool CRaidVolume::myRead(int disk, int sector, unsigned char *data) {
    if (!raid.dev.m_Read(disk, sector, data, 1)) {
        // Failed reading
        raid.failedDisk = disk;
        if (raid.state == RAID_OK)
            raid.state = RAID_DEGRADED;
        else
            raid.state = RAID_FAILED;
        return false;
    }
    return true;
}

// Write a single sector and update RAID state if write fails
bool CRaidVolume::myWrite(int disk, int sector, const unsigned char *data) {
    if (!raid.dev.m_Write(disk, sector, data, 1)) {
        // Failed
        raid.failedDisk = disk;
        if (raid.state == RAID_OK)
            raid.state = RAID_DEGRADED;
        else
            raid.state = RAID_FAILED;
        return false;
    }
    return true;
}
