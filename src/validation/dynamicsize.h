// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DYNAMIC_SIZE_H
#define BITCOIN_DYNAMIC_SIZE_H

#include <queue>
#include <stdint.h>

static const uint64_t AVG_BLK_QUARTER = 12959; // 90 days at avg 10 min blocks
static const uint64_t AVG_BLK_YEAR = 52559; // 1 year at avg 10 min blocks
static const uint64_t MIN_MAX_SIZE = 32000000; // 32MB

class CBlockSizeTracker
{
private:
    // stats for the last quarter
    std::queue<uint64_t> quarter_blocks;
    uint64_t quarter_total;
    // stats for the last year
    std::queue<uint64_t> year_blocks;
    uint64_t year_total;

private:
    void ClearQueue(std::queue<uint64_t> &queue);

public:
    CBlockSizeTracker()
    {
        SetNull();
    }
    void SetNull();
    void AddBlockSize(uint64_t size); //nBlockSize of CBlock should be passed in here
    uint64_t GetMaxBlockSize();
    bool Load();
    void Store();
};

extern CBlockSizeTracker sizeTracker;

#endif
