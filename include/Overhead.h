#ifndef OVERHEAD_H
#define OVERHEAD_H

#include "TBlkDev.h"
#include <cstring>

struct Overhead {
    int state;
    int failedDisk;
    int timestamp;
};

inline Overhead readFromBuffer(const unsigned char buffer[SECTOR_SIZE]) {
    Overhead overhead{};
    std::memcpy(&(overhead.timestamp), buffer, sizeof(int));
    std::memcpy(&(overhead.state), buffer + sizeof(int), sizeof(int));
    std::memcpy(&(overhead.failedDisk), buffer + sizeof(int) * 2, sizeof(int));
    return overhead;
}

#endif
