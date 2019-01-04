// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DYNAMIC_SIZE_H
#define BITCOIN_DYNAMIC_SIZE_H

#include <vector>
#include <stdint.h>

static const uint64_t AVG_BLK_QUARTER = 12959; // 90 days at avg 10 min blocks
static const uint64_t MEDIAN_INDEX_QUARTER = 6479; // instead of 6480 because 0 index
static const uint64_t AVG_BLK_YEAR = 52559; // 1 year at avg 10 min blocks
static const uint64_t MEDIAN_INDEX_YEAR = 26279; // instead of 26780 because 0 index

// min size as defined by zergs proposal update, aptly named zerg min size
static const uint64_t ZERG_MIN_SIZE = 3200000; // 3.2MB

// this is just a place holder for the fork activation code
static const uint64_t DYNAMIC_SIZE_FORK_BLOCK = 0;

class CBlockHistoryRange
{
public:
    const uint64_t max_num_blocks;
    const uint64_t median_index;
    // vector <value, index> where index is what block added this was
    std::vector<std::pair<uint64_t, uint64_t>> sorted_sizes;
    uint64_t index_last_added;
    uint64_t index_next_removed;
    uint64_t last_median;

private:
    void AddVectorDataPoint(uint64_t nSize);
    void RemoveVectorDataPoint();
    void RecalcuateMedian();

public:
    CBlockHistoryRange(const uint64_t _max_num_blocks, const uint64_t _median_index) :
    max_num_blocks(_max_num_blocks),
    median_index(_median_index)
    {
        ResetTrackedData();
    }
    // populate with default zerg size data points for all space except num_valid
    void PopulateDefault(int num_valid);
    void ResetTrackedData();
    void AddSizeData(uint64_t nSize);
};

class CBlockSizeTracker
{
private:

    CBlockHistoryRange quarter;
    CBlockHistoryRange year;

private:

public:
    CBlockSizeTracker() :
    quarter(AVG_BLK_QUARTER, MEDIAN_INDEX_QUARTER),
    year(AVG_BLK_YEAR, MEDIAN_INDEX_YEAR)
    {
        SetNull();
    }
    void SetNull();
    void AddBlockSize(uint64_t size); //nBlockSize of CBlock should be passed in here
    uint64_t GetMaxBlockSize();
    void Load();
    void Store(uint64_t nBlockSize);
};

extern CBlockSizeTracker sizeTracker;

#endif
