// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dynamicsize.h"
#include "txdb.h"

#include <algorithm>

extern CCoinsViewDB *pcoinsdbview;
extern CChain chainActive;
extern bool fPruneMode;

CBlockSizeTracker sizeTracker;

struct LessThanComparator
{
public:
    bool operator()(const std::pair<uint64_t, uint64_t> &a, const std::pair<uint64_t, uint64_t> &b)
    {
        return a.second < b.second;
    }
};

void CBlockHistoryRange::AddVectorDataPoint(uint64_t nSize)
{
    sorted_sizes.push_back(std::make_pair(nSize, index_last_added));
    index_last_added = index_last_added + 1;
    // we need to sort the data again, a removal doesnt break sorting, adding does
    std::sort(sorted_sizes.begin(), sorted_sizes.end(), LessThanComparator());
}

void CBlockHistoryRange::RemoveVectorDataPoint()
{
    // we use both a vector_index and a bool to prevent signed unsigned comparison
    size_t vector_index = 0;
    bool found = false;

    for(size_t i = 0; i < sorted_sizes.size(); i++)
    {
        if(sorted_sizes[i].second == index_next_removed)
        {
            vector_index = i;
            found = true;
            break;
        }
    }
    if(found)
    {
        sorted_sizes.erase(sorted_sizes.begin() + vector_index);
        // only increment value if we actually remove
        index_next_removed = index_next_removed + 1;
    }
}

void CBlockHistoryRange::RecalcuateMedian()
{
    last_median = sorted_sizes[median_index].first;
}

void CBlockHistoryRange::PopulateDefault(int num_valid)
{
    if (num_valid < 0)
    {
        return;
    }
    size_t num_default = max_num_blocks - (size_t)num_valid;
    while (sorted_sizes.size() < num_default)
    {
        sorted_sizes.push_back(std::make_pair(ZERG_MIN_SIZE, index_last_added));
        index_last_added = index_last_added + 1;
        // no need to sort here because everying is the same
    }
}

void CBlockHistoryRange::ResetTrackedData()
{
    sorted_sizes.clear();
    index_last_added = 0;
    index_next_removed = 0;
    last_median = 0;
}

void CBlockHistoryRange::AddSizeData(uint64_t nBlockSize)
{
    if(nBlockSize < ZERG_MIN_SIZE)
    {
        nBlockSize = ZERG_MIN_SIZE;
    }
    AddVectorDataPoint(nBlockSize);

    // since we can only add one size at a time, we should only
    // ever be over num_blocks by 1

    if(sorted_sizes.size() > max_num_blocks)
    {
        RemoveVectorDataPoint();
    }
    RecalcuateMedian();
}

void CBlockSizeTracker::SetNull()
{
    quarter.ResetTrackedData();
    year.ResetTrackedData();
}

void CBlockSizeTracker::AddBlockSize(uint64_t nBlockSize)
{
    quarter.AddSizeData(nBlockSize);
    year.AddSizeData(nBlockSize);
}

uint64_t CBlockSizeTracker::GetMaxBlockSize()
{
    return std::max(quarter.last_median, year.last_median);
}

// this function is in no way optiimized yet
void CBlockSizeTracker::Load()
{
    // not yet activated
    if(chainActive.Tip()->nHeight < DYNAMIC_SIZE_FORK_BLOCK)
    {
        return;
    }
    if (!fPruneMode) // no prune
    {
        CBlockIndex *pindex = chainActive[DYNAMIC_SIZE_FORK_BLOCK];
        int difference = chainActive.Tip()->nHeight - pindex->nHeight;
        quarter.PopulateDefault(difference);
        year.PopulateDefault(difference);
        // do the current index
        uint64_t nBlockSize;
        pcoinsdbview->GetBlockSize(pindex->nHeight, nBlockSize);
        AddBlockSize(nBlockSize);
        // do indexes until tip
        // we will get to nullptr when we are at tip
        while (chainActive.Next(pindex) != nullptr)
        {
            pcoinsdbview->GetBlockSize(pindex->nHeight, nBlockSize);
            AddBlockSize(nBlockSize);
        }
    }
    else // pruned mode
    {
        // 0 means fill completely with default data
        quarter.PopulateDefault(0);
        year.PopulateDefault(0);
        CBlockIndex *pindex = chainActive.Tip();
        while (pindex->pprev)
        {
            pindex = pindex->pprev;
        }
        // do the current index
        uint64_t nBlockSize;
        pcoinsdbview->GetBlockSize(pindex->nHeight, nBlockSize);
        AddBlockSize(nBlockSize);
        // do indexes until tip
        // we will get to nullptr when we are at tip
        while (chainActive.Next(pindex) != nullptr)
        {
            pcoinsdbview->GetBlockSize(pindex->nHeight, nBlockSize);
            AddBlockSize(nBlockSize);
        }
    }
}

void CBlockSizeTracker::Store(uint64_t nBlockSize)
{
    pcoinsdbview->WriteBlockSize(chainActive.Tip()->nHeight, nBlockSize);
}
