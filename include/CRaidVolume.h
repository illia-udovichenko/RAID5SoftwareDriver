#ifndef CRAIDVOLUME_H
#define CRAIDVOLUME_H

#include "TBlkDev.h"

class CRaidVolume {
public:
    CRaidVolume();

    static bool create(const TBlkDev &dev);

    int start(const TBlkDev &dev);

    int stop();

    int resync();

    int status() const;

    int size() const;

    bool read(int secNr, void *data, int secCnt);

    bool write(int secNr, const void *data, int secCnt);

private:
    struct RAID {
        TBlkDev dev;
        int state;
        int failedDisk;
        int timestamp;
    } raid;

    struct Evaluation {
        int sector;
        int disk;
        int diskParity;
    };

    Evaluation findSector(int input) const;

    bool myRead(int disk, int sector, unsigned char *data);

    bool myWrite(int disk, int sector, const unsigned char *data);
};

#endif
