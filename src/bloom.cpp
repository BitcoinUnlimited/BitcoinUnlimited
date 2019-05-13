// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bloom.h"

#include "hashwrapper.h"
#include "primitives/transaction.h"
#include "random.h"
#include "script/script.h"
#include "script/standard.h"
#include "streams.h"
#include "util.h"

#include <math.h>
#include <stdlib.h>

#define LN2SQUARED 0.4804530139182014246671025263266649717305529515945455
#define LN2 0.6931471805599453094172321214581765680755001343602552
#define MIN_N_HASH_FUNC 1

using namespace std;

void CBloomFilter::setup(unsigned int nElements,
    double nFPRate,
    unsigned int nTweakIn,
    unsigned char nFlagsIn,
    bool fSizeConstrained,
    uint32_t nMaxFilterSize = SMALLEST_MAX_BLOOM_FILTER_SIZE)
{
    if (nElements == 0)
    {
        LOGA("Construction of empty CBloomFilter attempted.\n");
        nElements = 1;
    }
    unsigned int nDesiredSize = (unsigned int)(-1 / LN2SQUARED * nElements * log(nFPRate) / 8);

    if (fSizeConstrained)
        nDesiredSize = min(nDesiredSize, nMaxFilterSize);

    vData.resize(nDesiredSize, 0);
    isFull = vData.size() == 0;
    isEmpty = true;

    // It would be more accurate to round not floor this.  However more hash funcs take more time
    // so only round up if we end up calculating 0 hash funcs.
    nHashFuncs = (unsigned int)(vData.size() * 8 / nElements * LN2);
    if (nHashFuncs == 0)
        nHashFuncs = 1;

    if (fSizeConstrained)
        nHashFuncs = min(nHashFuncs, MAX_HASH_FUNCS);

    nTweak = nTweakIn;
    nFlags = nFlagsIn;
}

void CBloomFilter::setupGuaranteeFPR(unsigned int nElements,
    double nFPRate,
    unsigned int nTweakIn,
    unsigned char nFlagsIn,
    uint32_t nMaxFilterSize = SMALLEST_MAX_BLOOM_FILTER_SIZE)
{
    if (nElements == 0)
    {
        LOGA("Construction of empty CBloomFilter attempted.\n");
        nElements = 1;
    }
    unsigned int nDesiredSize = (unsigned int)(ceil(-1 / LN2SQUARED * nElements * log(nFPRate) / 8));

    vData.resize(nDesiredSize, 0);
    isFull = vData.size() == 0;
    isEmpty = true;

    nHashFuncs = (unsigned int)max(MIN_N_HASH_FUNC, int(vData.size() * 8 / nElements * LN2));

    nTweak = nTweakIn;
    nFlags = nFlagsIn;
}

CBloomFilter::CBloomFilter(unsigned int nElements,
    double nFPRate,
    unsigned int nTweakIn,
    unsigned char nFlagsIn,
    uint32_t nMaxFilterSize)
{
    setup(nElements, nFPRate, nTweakIn, nFlagsIn, true, nMaxFilterSize);
}

CBloomFilter::CBloomFilter(unsigned int nElements,
    double nFPRate,
    unsigned int nTweakIn,
    unsigned char nFlagsIn,
    bool fGuaranteeFPR,
    uint32_t nMaxFilterSize)
{
    if (fGuaranteeFPR)
        setupGuaranteeFPR(nElements, nFPRate, nTweakIn, nFlagsIn, nMaxFilterSize);
    else
        setup(nElements, nFPRate, nTweakIn, nFlagsIn, true, nMaxFilterSize);
}

// Private constructor used by CRollingBloomFilter
CBloomFilter::CBloomFilter(unsigned int nElements, double nFPRate, unsigned int nTweakIn)
{
    setup(nElements, nFPRate, nTweakIn, BLOOM_UPDATE_NONE, false);
}

inline unsigned int CBloomFilter::Hash(unsigned int nHashNum, const std::vector<unsigned char> &vDataToHash) const
{
    // 0xFBA4C795 chosen as it guarantees a reasonable bit difference between nHashNum values.
    return MurmurHash3(nHashNum * 0xFBA4C795 + nTweak, vDataToHash) % (vData.size() * 8);
}

void CBloomFilter::insert(const vector<unsigned char> &vKey)
{
    if (isFull)
        return;
    for (unsigned int i = 0; i < nHashFuncs; i++)
    {
        unsigned int nIndex = Hash(i, vKey);
        // Sets bit nIndex of vData
        vData[nIndex >> 3] |= (1 << (7 & nIndex));
    }
    isEmpty = false;
}

static std::vector<unsigned char> ToVector(const COutPoint &outpoint)
{
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << outpoint;
    return std::vector<unsigned char>(stream.begin(), stream.end());
}


void CBloomFilter::insert(const COutPoint &outpoint) { insert(ToVector(outpoint)); }
void CBloomFilter::insert(const uint256 &hash)
{
    vector<unsigned char> data(hash.begin(), hash.end());
    insert(data);
}

bool CBloomFilter::contains(const vector<unsigned char> &vKey) const
{
    if (isFull)
        return true;
    if (isEmpty)
        return false;
    for (unsigned int i = 0; i < nHashFuncs; i++)
    {
        unsigned int nIndex = Hash(i, vKey);
        // Checks bit nIndex of vData
        if (!(vData[nIndex >> 3] & (1 << (7 & nIndex))))
            return false;
    }
    return true;
}

bool CBloomFilter::contains(const COutPoint &outpoint) const { return contains(ToVector(outpoint)); }
bool CBloomFilter::contains(const uint256 &hash) const
{
    vector<unsigned char> data(hash.begin(), hash.end());
    return contains(data);
}

void CBloomFilter::clear()
{
    vData.assign(vData.size(), 0);
    isFull = vData.size() == 0;
    isEmpty = true;
}

void CBloomFilter::reset(unsigned int nNewTweak)
{
    clear();
    nTweak = nNewTweak;
}

bool CBloomFilter::IsWithinSizeConstraints() const
{
    return vData.size() <= SMALLEST_MAX_BLOOM_FILTER_SIZE && nHashFuncs <= MAX_HASH_FUNCS;
}

#ifndef ANDROID // We do not want to pull "Solver" into the Android cashlib compile
bool CBloomFilter::MatchAndInsertOutputs(const CTransactionRef &tx)
{
    bool fFound = false;
    // Match if the filter contains the hash of tx
    //  for finding tx when they appear in a block
    if (isFull)
        return true;
    if (isEmpty)
        return false;
    const uint256 &hash = tx->GetHash();
    if (contains(hash))
        fFound = true;

    for (unsigned int i = 0; i < tx->vout.size(); i++)
    {
        const CTxOut &txout = tx->vout[i];
        // Match if the filter contains any arbitrary script data element in any scriptPubKey in tx
        // If this matches, also add the specific output that was matched.
        // This means clients don't have to update the filter themselves when a new relevant tx
        // is discovered in order to find spending transactions, which avoids round-tripping and race conditions.
        CScript::const_iterator pc = txout.scriptPubKey.begin();
        vector<unsigned char> data;
        while (pc < txout.scriptPubKey.end())
        {
            opcodetype opcode;
            if (!txout.scriptPubKey.GetOp(pc, opcode, data))
                break;
            if (data.size() != 0 && contains(data))
            {
                fFound = true;
                if ((nFlags & BLOOM_UPDATE_MASK) == BLOOM_UPDATE_ALL)
                    insert(COutPoint(hash, i));
                else if ((nFlags & BLOOM_UPDATE_MASK) == BLOOM_UPDATE_P2PUBKEY_ONLY)
                {
                    txnouttype type;
                    vector<vector<unsigned char> > vSolutions;
                    if (Solver(txout.scriptPubKey, type, vSolutions) &&
                        (type == TX_PUBKEY || type == TX_MULTISIG || type == TX_CLTV))
                        insert(COutPoint(hash, i));
                }
                break;
            }
        }
    }

    return (fFound);
}

bool CBloomFilter::MatchInputs(const CTransactionRef &tx)
{
    if (isEmpty)
        return false;
    for (const CTxIn &txin : tx->vin)
    {
        // Match if the filter contains an outpoint tx spends
        if (contains(txin.prevout))
            return true;

        // Match if the filter contains any arbitrary script data element in any scriptSig in tx
        CScript::const_iterator pc = txin.scriptSig.begin();
        vector<unsigned char> data;
        while (pc < txin.scriptSig.end())
        {
            opcodetype opcode;
            if (!txin.scriptSig.GetOp(pc, opcode, data))
                break;
            if (data.size() != 0 && contains(data))
                return true;
        }
    }

    return false;
}
#endif

void CBloomFilter::UpdateEmptyFull()
{
    bool full = true;
    bool empty = true;
    for (unsigned int i = 0; i < vData.size(); i++)
    {
        full &= vData[i] == 0xff;
        empty &= vData[i] == 0;
    }
    isFull = full;
    isEmpty = empty;
}

CRollingBloomFilter::CRollingBloomFilter(unsigned int nElements, double fpRate)
{
    double logFpRate = log(fpRate);
    /* The optimal number of hash functions is log(fpRate) / log(0.5), but
     * restrict it to the range 1-50. */
    nHashFuncs = std::max(1, std::min((int)round(logFpRate / log(0.5)), 50));
    /* In this rolling bloom filter, we'll store between 2 and 3 generations of nElements / 2 entries. */
    nEntriesPerGeneration = (nElements + 1) / 2;
    uint32_t nMaxElements = nEntriesPerGeneration * 3;
    /* The maximum fpRate = pow(1.0 - exp(-nHashFuncs * nMaxElements / nFilterBits), nHashFuncs)
     * =>          pow(fpRate, 1.0 / nHashFuncs) = 1.0 - exp(-nHashFuncs * nMaxElements / nFilterBits)
     * =>          1.0 - pow(fpRate, 1.0 / nHashFuncs) = exp(-nHashFuncs * nMaxElements / nFilterBits)
     * =>          log(1.0 - pow(fpRate, 1.0 / nHashFuncs)) = -nHashFuncs * nMaxElements / nFilterBits
     * =>          nFilterBits = -nHashFuncs * nMaxElements / log(1.0 - pow(fpRate, 1.0 / nHashFuncs))
     * =>          nFilterBits = -nHashFuncs * nMaxElements / log(1.0 - exp(logFpRate / nHashFuncs))
     */
    uint32_t nFilterBits = (uint32_t)ceil(-1.0 * nHashFuncs * nMaxElements / log(1.0 - exp(logFpRate / nHashFuncs)));
    data.clear();
    /* For each data element we need to store 2 bits. If both bits are 0, the
     * bit is treated as unset. If the bits are (01), (10), or (11), the bit is
     * treated as set in generation 1, 2, or 3 respectively.
     * These bits are stored in separate integers: position P corresponds to bit
     * (P & 63) of the integers data[(P >> 6) * 2] and data[(P >> 6) * 2 + 1]. */
    data.resize(((nFilterBits + 63) / 64) << 1);
    reset();
}

/* Similar to CBloomFilter::Hash */
static inline uint32_t RollingBloomHash(unsigned int nHashNum,
    uint32_t nTweak,
    const std::vector<unsigned char> &vDataToHash)
{
    return MurmurHash3(nHashNum * 0xFBA4C795 + nTweak, vDataToHash);
}

void CRollingBloomFilter::insert(const std::vector<unsigned char> &vKey)
{
    if (nEntriesThisGeneration == nEntriesPerGeneration)
    {
        nEntriesThisGeneration = 0;
        nGeneration++;
        if (nGeneration == 4)
        {
            nGeneration = 1;
        }
        uint64_t nGenerationMask1 = -(uint64_t)(nGeneration & 1);
        uint64_t nGenerationMask2 = -(uint64_t)(nGeneration >> 1);
        /* Wipe old entries that used this generation number. */
        for (uint32_t p = 0; p < data.size(); p += 2)
        {
            uint64_t p1 = data[p], p2 = data[p + 1];
            uint64_t mask = (p1 ^ nGenerationMask1) | (p2 ^ nGenerationMask2);
            data[p] = p1 & mask;
            data[p + 1] = p2 & mask;
        }
    }
    nEntriesThisGeneration++;

    for (int n = 0; n < nHashFuncs; n++)
    {
        uint32_t h = RollingBloomHash(n, nTweak, vKey);
        int bit = h & 0x3F;
        uint32_t pos = (h >> 6) % data.size();
        /* The lowest bit of pos is ignored, and set to zero for the first bit, and to one for the second. */
        data[pos & ~1] = (data[pos & ~1] & ~(((uint64_t)1) << bit)) | ((uint64_t)(nGeneration & 1)) << bit;
        data[pos | 1] = (data[pos | 1] & ~(((uint64_t)1) << bit)) | ((uint64_t)(nGeneration >> 1)) << bit;
    }
}

void CRollingBloomFilter::insert(const uint256 &hash)
{
    vector<unsigned char> _vData(hash.begin(), hash.end());
    insert(_vData);
}

void CRollingBloomFilter::insert(const COutPoint &outpoint) { insert(ToVector(outpoint)); }
bool CRollingBloomFilter::contains(const std::vector<unsigned char> &vKey) const
{
    for (int n = 0; n < nHashFuncs; n++)
    {
        uint32_t h = RollingBloomHash(n, nTweak, vKey);
        int bit = h & 0x3F;
        uint32_t pos = (h >> 6) % data.size();
        /* If the relevant bit is not set in either data[pos & ~1] or data[pos | 1], the filter does not contain vKey */
        if (!(((data[pos & ~1] | data[pos | 1]) >> bit) & 1))
        {
            return false;
        }
    }
    return true;
}

bool CRollingBloomFilter::contains(const uint256 &hash) const
{
    vector<unsigned char> _vData(hash.begin(), hash.end());
    return contains(_vData);
}

bool CRollingBloomFilter::contains(const COutPoint &outpoint) const { return contains(ToVector(outpoint)); }
void CRollingBloomFilter::reset()
{
#ifndef ANDROID // On Android don't pick a new tweak value because we don't have GetRand
    nTweak = GetRand(std::numeric_limits<unsigned int>::max());
#endif
    nEntriesThisGeneration = 0;
    nGeneration = 1;
    for (std::vector<uint64_t>::iterator it = data.begin(); it != data.end(); it++)
    {
        *it = 0;
    }
}
