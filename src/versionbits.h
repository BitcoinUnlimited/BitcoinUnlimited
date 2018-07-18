// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_VERSIONBITS
#define BITCOIN_CONSENSUS_VERSIONBITS

#include "chain.h"
#include <map>

/** What block version to use for new blocks (pre versionbits) */
static const int32_t VERSIONBITS_LAST_OLD_BLOCK_VERSION = 4;
/** What bits to set in version for versionbits blocks */
static const int32_t VERSIONBITS_TOP_BITS = 0x20000000UL;
/** What bitmask determines whether versionbits is in use */
static const int32_t VERSIONBITS_TOP_MASK = 0xE0000000UL;
/** Size of window to use for assessing warning of unknown bits */
static const int BIT_WARNING_WINDOW = 100;
/** Threshold to use for assessing warning of unknown bits */
static const int BIT_WARNING_THRESHOLD = 50;

enum ThresholdState
{
    THRESHOLD_DEFINED,
    THRESHOLD_STARTED,
    THRESHOLD_LOCKED_IN,
    THRESHOLD_ACTIVE,
    THRESHOLD_FAILED
};

// A map that gives the state for blocks whose height is a multiple of Period().
// The map is indexed by the block's parent, however, so all keys in the map
// will either be NULL or a block with (height + 1) % Period() == 0.
typedef std::map<const CBlockIndex *, ThresholdState> ThresholdConditionCache;

struct ForkDeploymentInfo
{
    /** Deployment name */
    char *name; // bip135: removed const to allow update from CSV
    /** Whether GBT clients can safely ignore this rule in simplified usage */
    bool gbt_force;
};

extern struct ForkDeploymentInfo VersionBitsDeploymentInfo[];

/**
 * Abstract class that implements BIP135-style threshold logic, and caches results.
 */
class AbstractThresholdConditionChecker
{
protected:
    virtual bool Condition(const CBlockIndex *pindex, const Consensus::Params &params) const = 0;
    virtual int64_t BeginTime(const Consensus::Params &params) const = 0;
    virtual int64_t EndTime(const Consensus::Params &params) const = 0;
    virtual int Period(const Consensus::Params &params) const = 0;
    virtual int Threshold(const Consensus::Params &params) const = 0;
    // bip135 begin
    virtual int MinLockedBlocks(const Consensus::Params &params) const = 0;
    virtual int64_t MinLockedTime(const Consensus::Params &params) const = 0;
    // bip135 end

public:
    // Note that the function below takes a pindexPrev as input: they compute information for block B based on its
    // parent.
    ThresholdState GetStateFor(const CBlockIndex *pindexPrev,
        const Consensus::Params &params,
        ThresholdConditionCache &cache) const;
    // bip135 begin
    bool backAtDefined(ThresholdConditionCache &cache, const CBlockIndex *pindex) const;
    // bip135 end
};

struct VersionBitsCache
{
    ThresholdConditionCache caches[Consensus::MAX_VERSION_BITS_DEPLOYMENTS];

    void Clear();
};

ThresholdState VersionBitsState(const CBlockIndex *pindexPrev,
    const Consensus::Params &params,
    Consensus::DeploymentPos pos,
    VersionBitsCache &cache);
uint32_t VersionBitsMask(const Consensus::Params &params, Consensus::DeploymentPos pos);

#endif
