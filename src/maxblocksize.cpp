// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "maxblocksize.h"

#include "chain.h"
#include "util.h"
#include <boost/lexical_cast.hpp>
#include <string>

uint64_t GetNextMaxBlockSize(const CBlockIndex *pindexLast, const Consensus::Params &params)
{
    // BIP100 not active, return legacy max size
    if (pindexLast == NULL || pindexLast->nHeight < params.bip100ActivationHeight)
        return BLOCKSTREAM_CORE_MAX_BLOCK_SIZE;

    uint64_t nMaxBlockSize = pindexLast->nMaxBlockSize;

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight + 1) % params.DifficultyAdjustmentInterval() != 0)
    {
        return nMaxBlockSize;
    }

    std::vector<uint64_t> votes(params.DifficultyAdjustmentInterval());
    const CBlockIndex *pindexWalk = pindexLast;
    for (int64_t i = 0; i < params.DifficultyAdjustmentInterval(); i++)
    {
        assert(pindexWalk);
        assert(pindexWalk->nMaxBlockSize == nMaxBlockSize);
        votes[i] = pindexWalk->nMaxBlockSizeVote ? pindexWalk->nMaxBlockSizeVote : pindexWalk->nMaxBlockSize;
        pindexWalk = pindexWalk->pprev;
    }

    std::sort(votes.begin(), votes.end());
    uint64_t lowerValue = votes.at(params.nMaxBlockSizeChangePosition - 1);
    uint64_t raiseValue = votes.at(params.DifficultyAdjustmentInterval() - params.nMaxBlockSizeChangePosition);

    assert(lowerValue >= 1000000); // minimal vote supported is 1MB
    assert(lowerValue >= raiseValue); // lowerValue comes from a higher sorted position

    uint64_t raiseCap = nMaxBlockSize * 105 / 100;
    raiseValue = (raiseValue > raiseCap ? raiseCap : raiseValue);
    if (raiseValue > nMaxBlockSize)
    {
        nMaxBlockSize = raiseValue;
    }
    else
    {
        uint64_t lowerFloor = nMaxBlockSize * 100 / 105;
        lowerValue = (lowerValue < lowerFloor ? lowerFloor : lowerValue);
        if (lowerValue < nMaxBlockSize)
            nMaxBlockSize = lowerValue;
    }

    if (nMaxBlockSize != pindexLast->nMaxBlockSize)
    {
        LogPrintf("GetNextMaxBlockSize RETARGET\n");
        LogPrintf("Before: %d\n", pindexLast->nMaxBlockSize);
        LogPrintf("After:  %d\n", nMaxBlockSize);
    }

    return nMaxBlockSize;
}

static uint32_t FindVote(const std::string &coinbase)
{
    bool eb_vote = false;
    uint32_t ebVoteMB = 0;
    std::vector<char> curr;
    bool bip100vote = false;
    bool started = false;

    for (char s : coinbase)
    {
        if (s == '/')
        {
            started = true;
            // End (or beginning) of a potential vote string.

            if (curr.size() < 2) // Minimum vote string length is 2
            {
                bip100vote = false;
                curr.clear();
                continue;
            }

            if (std::string(begin(curr), end(curr)) == "BIP100")
            {
                bip100vote = true;
                curr.clear();
                continue;
            }

            // Look for a B vote.
            if (bip100vote && curr[0] == 'B')
            {
                try
                {
                    return boost::lexical_cast<uint32_t>(std::string(begin(curr) + 1, end(curr)));
                }
                catch (const std::exception &e)
                {
                    LogPrintf("Invalid coinbase B-vote: %s\n", e.what());
                }
            }

            // Look for a EB vote. Keep it, but continue to look for a BIP100/B vote.
            if (!eb_vote && curr[0] == 'E' && curr[1] == 'B')
            {
                try
                {
                    ebVoteMB = boost::lexical_cast<uint32_t>(std::string(begin(curr) + 2, end(curr)));
                    eb_vote = true;
                }
                catch (const std::exception &e)
                {
                    LogPrintf("Invalid coinbase EB-vote: %s\n", e.what());
                }
            }

            bip100vote = false;
            curr.clear();
            continue;
        }
        else if (!started)
            continue;
        else
            curr.push_back(s);
    }
    return ebVoteMB;
}

uint64_t GetMaxBlockSizeVote(const CScript &coinbase, int32_t nHeight)
{
    // Skip encoded height if found at start of coinbase
    CScript expect = CScript() << nHeight;
    int searchStart = coinbase.size() >= expect.size() && std::equal(expect.begin(), expect.end(), coinbase.begin()) ?
                          expect.size() :
                          0;

    std::string s(coinbase.begin() + searchStart, coinbase.end());

    if (s.length() < 5) // shortest vote is /EB1/
        return 0;


    return static_cast<uint64_t>(FindVote(s)) * 1000000;
}
