// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include "clientversion.h"
#include "uint256.h"

#include <map>
#include <string>

namespace Consensus
{
enum DeploymentPos
{
    // bip135 begin
    // List of deployment bits. Known allocated bits should be described by a
    // name, even if their deployment logic is not implemented by the client.
    // (their info is nevertheless useful for awareness and event logging)
    // When a bit goes back to being unused, it should be renamed to
    // DEPLOYMENT_UNASSIGNED_BIT_x .
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    DEPLOYMENT_CSV = 0, // bit 0 - deployment of BIP68, BIP112, and BIP113.
    // begin unassigned bits. Rename bits when allocated.
    DEPLOYMENT_128MB_BLOCK,
    DEPLOYMENT_MUL_SHIFT_INVERT,
    DEPLOYMENT_UNLIMITED_SCRIPT_OPCODES,
    DEPLOYMENT_CHECKDATASIG,
    DEPLOYMENT_100BYTE_MIN_TX,
    DEPLOYMENT_CTOR,
    DEPLOYMENT_SCRIPTSIG_PUSH_ONLY,
    DEPLOYMENT_UNASSIGNED_BIT_8,
    DEPLOYMENT_UNASSIGNED_BIT_9,
    DEPLOYMENT_UNASSIGNED_BIT_10,
    DEPLOYMENT_UNASSIGNED_BIT_11,
    DEPLOYMENT_UNASSIGNED_BIT_12,
    DEPLOYMENT_UNASSIGNED_BIT_13,
    DEPLOYMENT_UNASSIGNED_BIT_14,
    DEPLOYMENT_UNASSIGNED_BIT_15,
    DEPLOYMENT_UNASSIGNED_BIT_16,
    DEPLOYMENT_UNASSIGNED_BIT_17,
    DEPLOYMENT_UNASSIGNED_BIT_18,
    DEPLOYMENT_UNASSIGNED_BIT_19,
    DEPLOYMENT_UNASSIGNED_BIT_20,
    DEPLOYMENT_UNASSIGNED_BIT_21,
    DEPLOYMENT_UNASSIGNED_BIT_22,
    DEPLOYMENT_UNASSIGNED_BIT_23,
    DEPLOYMENT_UNASSIGNED_BIT_24,
    DEPLOYMENT_UNASSIGNED_BIT_25,
    DEPLOYMENT_UNASSIGNED_BIT_26,
    DEPLOYMENT_UNASSIGNED_BIT_27,
    DEPLOYMENT_TESTDUMMY, // bit 28 - used for deployment testing purposes
    // bip135 end
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP135.
 */
struct ForkDeployment
{
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;
    // bip135 begin added parameters
    /** Window size (in blocks) for generalized versionbits signal tallying */
    int windowsize;
    /** Threshold (in blocks / window) for generalized versionbits lock-in */
    int threshold;
    /** Minimum number of blocks to remain in locked-in state */
    int minlockedblocks;
    /** Minimum duration (in seconds based on MTP) to remain in locked-in state */
    int64_t minlockedtime;
    // bip135 end added parameters
};

/**
 * Parameters that influence chain consensus.
 */
struct Params
{
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /** Block height at which BIP16 becomes active */
    int BIP16Height;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /**
     * Deployment parameters for the 29 bits (0..28) defined by bip135
     */
    // bip135 begin
    // fully initialize array
    ForkDeployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS] = {
        {0, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 0
        {1, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 1
        {2, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 2
        {3, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 3
        {4, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 4
        {5, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 5
        {6, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 6
        {7, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 7
        {8, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 8
        {9, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 9
        {10, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 10
        {11, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 11
        {12, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 12
        {13, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 13
        {14, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 14
        {15, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 15
        {16, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 16
        {17, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 17
        {18, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 18
        {19, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 19
        {20, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 20
        {21, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 21
        {22, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 22
        {23, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 23
        {24, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 24
        {25, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 25
        {26, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 26
        {27, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 27
        {28, 0LL, 0LL, 0, 0, 0, 0LL}, // deployment on bit 28
    };
    // bip135 end
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    /** UAHF Aug 1st 2017 block height */
    int uahfHeight;
    /** Block height at which the new DAA becomes active */
    int daaHeight;
    /** May 15, 2018 block height at which the fork activated */
    int may2018Height;
    /** Nov 15, 2018 activation height */
    int nov2018Height;
    /** May 15, 2019 actication time will be 12:00:00 UTC */
    int may2019ActivationTime;
    /** Nov 15, 2018 SV activation height */
    int sv2018Height;
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
