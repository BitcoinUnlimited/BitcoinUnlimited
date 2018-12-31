// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dynamicsize.h"
#include "txdb.h"

extern CCoinsViewDB *pcoinsdbview;

CBlockSizeTracker sizeTracker;

void CBlockSizeTracker::SetNull()
{
    ClearQueue(quarter_blocks);
    quarter_total = 0;
    ClearQueue(year_blocks);
    year_total = 0;
}

void CBlockSizeTracker::ClearQueue(std::queue<uint64_t> &queue)
{
   std::queue<uint64_t> empty;
   std::swap(queue, empty);
}

void CBlockSizeTracker::AddBlockSize(uint64_t nBlockSize)
{
    //quarter stats
    quarter_blocks.push(nBlockSize);
    // check how many blocks we have values for, if too many pop front
    // we should only ever at most 1 more than we are supposed to
    if(quarter_blocks.size() > AVG_BLK_QUARTER)
    {
        quarter_total = quarter_total - quarter_blocks.front();
        quarter_blocks.pop();
    }
    quarter_total = quarter_total + nBlockSize;

    // year stats
    year_blocks.push(nBlockSize);
    // check how many blocks we have values for, if too many pop front
    // we should only ever at most 1 more than we are supposed to
    if(year_blocks.size() > AVG_BLK_YEAR)
    {
        year_total = year_total - year_blocks.front();
        year_blocks.pop();
    }
    year_total = year_total + nBlockSize;

}

uint64_t CBlockSizeTracker::GetMaxBlockSize()
{
    // if we dont have enough for a quarter, we dont have enough for a year
    if(quarter_blocks.size() != AVG_BLK_QUARTER)
    {
        return MIN_MAX_SIZE;
    }
    uint64_t avg_quarter = quarter_total / quarter_blocks.size();
    uint64_t avg_max = avg_quarter;

    // dont have enough for a year
    if(year_blocks.size() != AVG_BLK_YEAR)
    {
        // adjust avg max for x10 multiplier
        avg_max = avg_max * 10;
        // now compare
        if(avg_max > MIN_MAX_SIZE)
        {
            return avg_max;
        }
    }
    else
    {
        uint64_t avg_year = year_total / year_blocks.size();
        avg_max = std::max(avg_quarter, avg_year);
        // adjust avg_max for multiplier
        avg_max = avg_max * 10;
        // now compare
        if(avg_max > MIN_MAX_SIZE)
        {
            return avg_max;
        }
    }
    return MIN_MAX_SIZE;
}

bool CBlockSizeTracker::Load()
{
    return pcoinsdbview->GetBlockSizes(quarter_total, year_total);
}

void CBlockSizeTracker::Store()
{
    pcoinsdbview->WriteBlockSizes(quarter_total, year_total);
}
