// Copyright (c) 2016-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "blockrelay/blockrelay_common.h"
#include "blockrelay/compactblock.h"
#include "blockstorage/blockstorage.h"
#include "chainparams.h"
#include "connmgr.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "dosman.h"
#include "expedited.h"
#include "hash.h"
#include "main.h"
#include "net.h"
#include "parallel.h"
#include "policy/policy.h"
#include "pow.h"
#include "random.h"
#include "requestManager.h"
#include "streams.h"
#include "timedata.h"
#include "txadmission.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "util.h"
#include "utiltime.h"
#include "validation/validation.h"


static bool ReconstructBlock(CNode *pfrom, const bool fXVal, int &missingCount, int &unnecessaryCount);


uint64_t GetShortID(const uint64_t &shorttxidk0, const uint64_t &shorttxidk1, const uint256 &txhash)
{
    static_assert(CompactBlock::SHORTTXIDS_LENGTH == 6, "shorttxids calculation assumes 6-byte shorttxids");
    return SipHashUint256(shorttxidk0, shorttxidk1, txhash) & 0xffffffffffffL;
}

#define MIN_TRANSACTION_SIZE (::GetSerializeSize(CTransaction(), SER_NETWORK, PROTOCOL_VERSION))

CompactBlock::CompactBlock(const CBlock &block, const CRollingFastFilter<4 * 1024 * 1024> *inventoryKnown)
    : nonce(GetRand(std::numeric_limits<uint64_t>::max())), header(block)
{
    FillShortTxIDSelector();

    if (block.vtx.empty())
        throw std::invalid_argument(__func__ + std::string(" expects coinbase tx"));

    //< Index of a prefilled tx is its diff from last index.
    size_t prevIndex = 0;
    prefilledtxn.push_back(PrefilledTransaction{0, *block.vtx[0]});
    for (size_t i = 1; i < block.vtx.size(); i++)
    {
        const CTransaction &tx = *block.vtx[i];
        if (inventoryKnown && !inventoryKnown->contains(tx.GetHash()))
        {
            prefilledtxn.push_back(PrefilledTransaction{static_cast<uint16_t>(i - (prevIndex + 1)), tx});
            prevIndex = i;
        }
        else
        {
            shorttxids.push_back(GetShortID(tx.GetHash()));
        }
    }
}

void CompactBlock::FillShortTxIDSelector() const
{
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << header << nonce;
    CSHA256 hasher;
    hasher.Write((unsigned char *)&(*stream.begin()), stream.end() - stream.begin());
    uint256 shorttxidhash;
    hasher.Finalize(shorttxidhash.begin());
    shorttxidk0 = shorttxidhash.GetUint64(0);
    shorttxidk1 = shorttxidhash.GetUint64(1);
}

uint64_t CompactBlock::GetShortID(const uint256 &txhash) const
{
    return ::GetShortID(shorttxidk0, shorttxidk1, txhash);
}

void validateCompactBlock(const CompactBlock &cmpctblock)
{
    if (cmpctblock.header.IsNull() || (cmpctblock.shorttxids.empty() && cmpctblock.prefilledtxn.empty()))
        throw std::invalid_argument("empty data in compact block");

    int32_t lastprefilledindex = -1;
    for (size_t i = 0; i < cmpctblock.prefilledtxn.size(); i++)
    {
        if (cmpctblock.prefilledtxn[i].tx.IsNull())
            throw std::invalid_argument("null tx in compact block");

        lastprefilledindex += cmpctblock.prefilledtxn[i].index + 1; // index is a uint16_t, so cant overflow here
        if (lastprefilledindex > std::numeric_limits<uint16_t>::max())
            throw std::invalid_argument("tx index overflows");

        if (static_cast<uint32_t>(lastprefilledindex) > cmpctblock.shorttxids.size() + i)
        {
            // If we are inserting a tx at an index greater than our full list of shorttxids
            // plus the number of prefilled txn we've inserted, then we have txn for which we
            // have neither a prefilled txn or a shorttxid!
            throw std::invalid_argument("invalid index for tx");
        }
    }
}

/**
 * Handle an incoming compactblock.  The block is fully validated, and if any transactions are missing, we fall
 * back to requesting a full block.
 */


bool CompactBlock::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    if (!pfrom->CompactBlockCapable())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("Compact block message received from a non compactblock node, peer=%s", pfrom->GetLogName());
    }

    CompactBlock compactBlock;
    vRecv >> compactBlock;

    // Message consistency checking
    try
    {
        validateCompactBlock(compactBlock);
    }
    catch (std::exception &e)
    {
        return error("compact block invalid\n");
    }

    // Is there a previous block or header to connect with?
    CBlockIndex *pprev = LookupBlockIndex(compactBlock.header.hashPrevBlock);
    if (!pprev)
        return error("compact block from peer %s will not connect, unknown previous block %s", pfrom->GetLogName(),
            compactBlock.header.hashPrevBlock.ToString());

    CValidationState state;
    if (!ContextualCheckBlockHeader(compactBlock.header, state, pprev))
    {
        // compact block does not fit within our blockchain
        dosMan.Misbehaving(pfrom, 100);
        return error(
            "compact block from peer %s contextual error: %s", pfrom->GetLogName(), state.GetRejectReason().c_str());
    }

    CInv inv(MSG_BLOCK, compactBlock.header.GetHash());
    uint64_t nSizeCompactBlock = ::GetSerializeSize(compactBlock, SER_NETWORK, PROTOCOL_VERSION);
    LOG(CMPCT, "received compact block %s from peer %s of %d bytes\n", inv.hash.ToString(), pfrom->GetLogName(),
        nSizeCompactBlock);

    // Ban a node for sending unrequested compact blocks
    if (!thinrelay.IsThinTypeBlockInFlight(pfrom, NetMsgType::CMPCTBLOCK))
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("unrequested compact block from peer %s", pfrom->GetLogName());
    }

    // Check if we've already received this block and have it on disk
    if (AlreadyHaveBlock(inv))
    {
        requester.AlreadyReceived(pfrom, inv);
        compactdata.ClearCompactBlockData(pfrom, inv.hash);

        LOG(CMPCT, "Received compactblock but returning because we already have this block %s on disk, peer=%s\n",
            inv.hash.ToString(), pfrom->GetLogName());
        return true;
    }

    return compactBlock.process(pfrom, nSizeCompactBlock);
}


bool CompactBlock::process(CNode *pfrom, uint64_t nSizeCompactBlock)
{
    // Xpress Validation - only perform xval if the chaintip matches the last blockhash in the compactblock
    bool fXVal = (header.hashPrevBlock == chainActive.Tip()->GetBlockHash()) ? true : false;

    compactdata.ClearCompactBlockData(pfrom);
    pfrom->nSizeCompactBlock = nSizeCompactBlock;

    pfrom->compactBlock.nVersion = header.nVersion;
    pfrom->compactBlock.nBits = header.nBits;
    pfrom->compactBlock.nNonce = header.nNonce;
    pfrom->compactBlock.nTime = header.nTime;
    pfrom->compactBlock.hashMerkleRoot = header.hashMerkleRoot;
    pfrom->compactBlock.hashPrevBlock = header.hashPrevBlock;
    pfrom->shorttxidk0 = shorttxidk0;
    pfrom->shorttxidk1 = shorttxidk1;

    // Because the list of shorttxids is not complete (missing the prefilled transaction hashes), we need
    // to first create the full list of compactblock shortid hashes, in proper order.
    //
    // Also, create the mapMissingTx from all the supplied tx's in the compact block

    // Reconstruct the list of shortid's and in the correct order taking into account the prefilled txns.
    if (prefilledtxn.empty())
    {
        pfrom->vShortCompactBlockHashes = shorttxids;
    }
    else
    {
        // Add hashes either from the prefilled txn vector or from the shorttxids vector.
        std::vector<uint64_t>::iterator iterShortID = shorttxids.begin();
        for (const PrefilledTransaction &prefilled : prefilledtxn)
        {
            if (prefilled.index == 0)
            {
                uint64_t shorthash = GetShortID(prefilled.tx.GetHash());
                pfrom->vShortCompactBlockHashes.push_back(shorthash);
                pfrom->mapMissingTx[shorthash] = MakeTransactionRef(prefilled.tx);
                continue;
            }

            // Add shortxid's until we get to the next prefilled txn
            for (size_t i = 0; i < prefilled.index; i++)
            {
                if (iterShortID != shorttxids.end())
                {
                    pfrom->vShortCompactBlockHashes.push_back(*iterShortID);
                    iterShortID++;
                }
                else
                    break;
            }

            // Add the prefilled txn and then get the next one
            pfrom->vShortCompactBlockHashes.push_back(GetShortID(prefilled.tx.GetHash()));
            pfrom->mapMissingTx[GetShortID(prefilled.tx.GetHash())] = MakeTransactionRef(prefilled.tx);
        }

        // Add the remaining shorttxids, if any.
        std::vector<uint64_t>::iterator it = pfrom->vShortCompactBlockHashes.end();
        pfrom->vShortCompactBlockHashes.insert(it, iterShortID, shorttxids.end());
    }

    // Create a map of all 8 bytes tx hashes pointing to their full tx hash counterpart
    // We need to check all transaction sources (orphan list, mempool, and new (incoming) transactions in this block)
    // for a collision.
    int missingCount = 0;
    int unnecessaryCount = 0;
    bool _collision = false;
    std::map<uint64_t, uint256> mapPartialTxHash;
    std::vector<uint256> memPoolHashes;
    std::set<uint64_t> setHashesToRequest;

    bool fMerkleRootCorrect = true;
    {
        // Do the orphans first before taking the mempool.cs lock, so that we maintain correct locking order.
        READLOCK(orphanpool.cs);
        for (auto &mi : orphanpool.mapOrphanTransactions)
        {
            uint64_t cheapHash = GetShortID(mi.first);
            if (mapPartialTxHash.count(cheapHash)) // Check for collisions
                _collision = true;
            mapPartialTxHash[cheapHash] = mi.first;
        }

        LOCK(cs_xval);
        mempool.queryHashes(memPoolHashes);

        for (uint64_t i = 0; i < memPoolHashes.size(); i++)
        {
            uint64_t cheapHash = GetShortID(memPoolHashes[i]);
            if (mapPartialTxHash.count(cheapHash)) // Check for collisions
                _collision = true;
            mapPartialTxHash[cheapHash] = memPoolHashes[i];
        }
        for (auto &mi : pfrom->mapMissingTx)
        {
            uint64_t cheapHash = mi.first;
            // Check for cheap hash collision. Only mark as collision if the full hash is not the same,
            // because the same tx could have been received into the mempool during the request of the compactblock.
            // In that case we would have the same transaction twice, so it is not a real cheap hash collision and we
            // continue normally.
            const uint256 existingHash = mapPartialTxHash[cheapHash];
            // Check if we already have the cheap hash
            if (!existingHash.IsNull())
            {
                // Check if it really is a cheap hash collision and not just the same transaction
                if (existingHash != mi.second->GetHash())
                {
                    _collision = true;
                }
            }
            mapPartialTxHash[cheapHash] = mi.second->GetHash();
        }

        if (!_collision)
        {
            // Start gathering the full tx hashes. If some are not available then add them to setHashesToRequest.
            uint256 nullhash;
            for (const uint64_t &cheapHash : pfrom->vShortCompactBlockHashes)
            {
                if (mapPartialTxHash.find(cheapHash) != mapPartialTxHash.end())
                {
                    pfrom->vCompactBlockHashes.push_back(mapPartialTxHash[cheapHash]);
                }
                else
                {
                    pfrom->vCompactBlockHashes.push_back(nullhash); // placeholder
                    setHashesToRequest.insert(cheapHash);

                    // If there are more hashes to request than available indices then we will not be able to
                    // reconstruct the compact block so just send a full block.
                    if (setHashesToRequest.size() > std::numeric_limits<uint16_t>::max())
                    {
                        // Since we can't process this compactblock then clear out the data from memory
                        compactdata.ClearCompactBlockData(pfrom, header.GetHash());

                        thinrelay.RequestBlock(pfrom, header.GetHash());
                        return error("Too many re-requested hashes for compactblock: requesting a full block");
                    }
                }
            }

            // We don't need this after here.
            mapPartialTxHash.clear();

            // Reconstruct the block if there are no hashes to re-request
            if (setHashesToRequest.empty())
            {
                bool mutated;
                uint256 merkleroot = ComputeMerkleRoot(pfrom->vCompactBlockHashes, &mutated);
                if (header.hashMerkleRoot != merkleroot || mutated)
                {
                    fMerkleRootCorrect = false;
                }
                else
                {
                    if (!ReconstructBlock(pfrom, fXVal, missingCount, unnecessaryCount))
                        return false;
                }
            }
        }
    } // End locking orphanpool.cs, mempool.cs and cs_xval
    LOG(CMPCT, "Total in memory compactblockbytes size is %ld bytes\n", compactdata.GetCompactBlockBytes());

    // These must be checked outside of the mempool.cs lock or deadlock may occur.
    // A merkle root mismatch here does not cause a ban because and expedited node will forward an xthin
    // without checking the merkle root, therefore we don't want to ban our expedited nodes. Just re-request
    // a full block if a mismatch occurs.
    // Also, there is a remote possiblity of a Tx hash collision therefore if it occurs we re-request a normal
    // block which has the full Tx hash data rather than just the truncated hash.
    if (_collision || !fMerkleRootCorrect)
    {
        if (!fMerkleRootCorrect)
            return error(
                "mismatched merkle root on compactblock: rerequesting a full block, peer=%s", pfrom->GetLogName());
        else
            return error(
                "TX HASH COLLISION for compactblock: re-requesting a full block, peer=%s", pfrom->GetLogName());

        compactdata.ClearCompactBlockData(pfrom, header.GetHash());
        thinrelay.RequestBlock(pfrom, header.GetHash());
        return true;
    }

    pfrom->compactBlockWaitingForTxns = missingCount;
    LOG(CMPCT, "compactblock waiting for: %d, unnecessary: %d, total txns: %d received txns: %d\n",
        pfrom->compactBlockWaitingForTxns, unnecessaryCount, pfrom->compactBlock.vtx.size(),
        pfrom->mapMissingTx.size());

    // If there are any missing hashes or transactions then we request them here.
    // This must be done outside of the mempool.cs lock or may deadlock.
    if (setHashesToRequest.size() > 0)
    {
        pfrom->compactBlockWaitingForTxns = setHashesToRequest.size();

        // find the index in the block associated with the hash
        uint64_t nIndex = 0;
        std::vector<uint16_t> vIndexesToRequest;
        for (auto cheaphash : pfrom->vShortCompactBlockHashes)
        {
            if (setHashesToRequest.find(cheaphash) != setHashesToRequest.end())
                vIndexesToRequest.push_back(nIndex);
            nIndex++;
        }
        CompactReRequest compactReRequest;
        compactReRequest.blockhash = header.GetHash();
        compactReRequest.indexes = vIndexesToRequest;
        pfrom->PushMessage(NetMsgType::GETBLOCKTXN, compactReRequest);

        // Update run-time statistics of compact block bandwidth savings
        compactdata.UpdateInBoundReRequestedTx(pfrom->compactBlockWaitingForTxns);
        return true;
    }

    // If there are still any missing transactions then we must clear out the compactblock data
    // and re-request a full block (This should never happen because we just checked the various pools).
    if (missingCount > 0)
    {
        // Since we can't process this compactblock then clear out the data from memory
        compactdata.ClearCompactBlockData(pfrom, header.GetHash());

        thinrelay.RequestBlock(pfrom, header.GetHash());
        return error("Still missing transactions for compactblock: re-requesting a full block");
    }

    // We now have all the transactions now that are in this block
    pfrom->compactBlockWaitingForTxns = -1;
    int blockSize = pfrom->compactBlock.GetBlockSize();
    LOG(CMPCT, "Reassembled compactblock for %s (%d bytes). Message was %d bytes, compression ratio %3.2f, peer=%s\n",
        pfrom->compactBlock.GetHash().ToString(), blockSize, pfrom->nSizeCompactBlock,
        ((float)blockSize) / ((float)pfrom->nSizeCompactBlock), pfrom->GetLogName());

    // Update run-time statistics of compact block bandwidth savings
    compactdata.UpdateInBound(pfrom->nSizeCompactBlock, blockSize);
    LOG(CMPCT, "compact block stats: %s\n", compactdata.ToString().c_str());

    // Process the full block
    PV->HandleBlockMessage(pfrom, NetMsgType::CMPCTBLOCK, MakeBlockRef(pfrom->compactBlock), GetInv());

    return true;
}

bool CompactReRequest::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    if (!pfrom->CompactBlockCapable())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("getblocktxn message received from a non compactblock node, peer=%s", pfrom->GetLogName());
    }

    CompactReRequest compactReRequest;
    vRecv >> compactReRequest;

    // Message consistency checking
    if (compactReRequest.indexes.empty() || compactReRequest.blockhash.IsNull())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("incorrectly constructed getblocktxn received.  Banning peer=%s", pfrom->GetLogName());
    }

    // We use MSG_TX here even though we refer to blockhash because we need to track
    // how many xblocktx requests we make in case of DOS
    CInv inv(MSG_TX, compactReRequest.blockhash);
    LOG(CMPCT, "received getblocktxn for %s peer=%s\n", inv.hash.ToString(), pfrom->GetLogName());

    // Check for Misbehaving and DOS
    // If they make more than 20 requests in 10 minutes then disconnect them
    if (Params().NetworkIDString() != "regtest")
    {
        if (pfrom->nGetXBlockTxLastTime <= 0)
            pfrom->nGetXBlockTxLastTime = GetTime();
        uint64_t nNow = GetTime();
        double tmp = pfrom->nGetXBlockTxCount;
        while (!pfrom->nGetXBlockTxCount.compare_exchange_weak(
            tmp, (tmp * std::pow(1.0 - 1.0 / 600.0, (double)(nNow - pfrom->nGetXBlockTxLastTime)) + 1)))
            ;
        pfrom->nGetXBlockTxLastTime = nNow;
        LOG(CMPCT, "nGetXBlockTxCount is %f\n", pfrom->nGetXBlockTxCount);
        if (pfrom->nGetXBlockTxCount >= 20)
        {
            dosMan.Misbehaving(pfrom, 100); // If they exceed the limit then disconnect them
            return error("DOS: Misbehaving - requesting too many getblocktxn: %s\n", inv.hash.ToString());
        }
    }

    std::vector<CTransaction> vTx;
    CBlockIndex *hdr = LookupBlockIndex(inv.hash);
    if (!hdr)
    {
        dosMan.Misbehaving(pfrom, 20);
        return error("Requested block is not available");
    }
    else
    {
        CBlock block;
        const Consensus::Params &consensusParams = Params().GetConsensus();
        if (!ReadBlockFromDisk(block, hdr, consensusParams))
        {
            // We do not assign misbehavior for not being able to read a block from disk because we already
            // know that the block is in the block index from the step above. Secondly, a failure to read may
            // be our own issue or the remote peer's issue in requesting too early.  We can't know at this point.
            return error("Cannot load block from disk -- Block txn request possibly received before assembled");
        }

        CompactReReqResponse compactReqResponse(block, compactReRequest.indexes);
        pfrom->PushMessage(NetMsgType::BLOCKTXN, compactReqResponse);
        pfrom->txsSent += compactReRequest.indexes.size();
    }

    return true;
}

bool CompactReReqResponse::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    if (!pfrom->CompactBlockCapable())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("compactrereqresponse message received from a non CMPCT node, peer=%s", pfrom->GetLogName());
    }

    std::string strCommand = NetMsgType::BLOCKTXN;
    size_t msgSize = vRecv.size();
    CompactReReqResponse compactReReqResponse;
    vRecv >> compactReReqResponse;

    // Message consistency checking
    CInv inv(MSG_CMPCT_BLOCK, compactReReqResponse.blockhash);
    if (compactReReqResponse.txn.empty() || compactReReqResponse.blockhash.IsNull())
    {
        compactdata.ClearCompactBlockData(pfrom, inv.hash);

        dosMan.Misbehaving(pfrom, 100);
        return error(
            "incorrectly constructed compactReReqResponse or inconsistent compactblock data received.  Banning peer=%s",
            pfrom->GetLogName());
    }

    LOG(CMPCT, "received compactReReqResponse for %s peer=%s\n", inv.hash.ToString(), pfrom->GetLogName());
    {
        // Do not process unrequested xblocktx unless from an expedited node.
        if (!thinrelay.IsThinTypeBlockInFlight(pfrom, NetMsgType::CMPCTBLOCK) && !connmgr->IsExpeditedUpstream(pfrom))
        {
            dosMan.Misbehaving(pfrom, 10);
            return error("Received compactReReqResponse %s from peer %s but was unrequested", inv.hash.ToString(),
                pfrom->GetLogName());
        }
    }

    // Check if we've already received this block and have it on disk
    if (AlreadyHaveBlock(inv))
    {
        requester.AlreadyReceived(pfrom, inv);
        compactdata.ClearCompactBlockData(pfrom, inv.hash);

        LOG(CMPCT,
            "Received compactReReqResponse but returning because we already have this block %s on disk, peer=%s\n",
            inv.hash.ToString(), pfrom->GetLogName());
        return true;
    }

    // Create the mapMissingTx from all the supplied tx's in the compactblock
    for (const CTransaction &tx : compactReReqResponse.txn)
        pfrom->mapMissingTx[GetShortID(pfrom->shorttxidk0, pfrom->shorttxidk1, tx.GetHash())] = MakeTransactionRef(tx);

    // Get the full hashes from the compactReReqResponse and add them to the compactBlockHashes vector.  These should
    // be all the missing or null hashes that we re-requested.
    int count = 0;
    for (size_t i = 0; i < pfrom->vCompactBlockHashes.size(); i++)
    {
        if (pfrom->vCompactBlockHashes[i].IsNull())
        {
            std::map<uint64_t, CTransactionRef>::iterator val =
                pfrom->mapMissingTx.find(pfrom->vShortCompactBlockHashes[i]);
            if (val != pfrom->mapMissingTx.end())
            {
                pfrom->vCompactBlockHashes[i] = val->second->GetHash();
            }
            count++;
        }
    }
    LOG(CMPCT, "Got %d Re-requested txs, needed %d of them from peer=%s\n", compactReReqResponse.txn.size(), count,
        pfrom->GetLogName());

    // At this point we should have all the full hashes in the block. Check that the merkle
    // root in the block header matches the merkleroot calculated from the hashes provided.
    bool mutated;
    uint256 merkleroot = ComputeMerkleRoot(pfrom->vCompactBlockHashes, &mutated);
    if (pfrom->compactBlock.hashMerkleRoot != merkleroot || mutated)
    {
        compactdata.ClearCompactBlockData(pfrom, inv.hash);

        dosMan.Misbehaving(pfrom, 100);
        return error("Merkle root for %s does not match computed merkle root, peer=%s", inv.hash.ToString(),
            pfrom->GetLogName());
    }
    LOG(CMPCT, "Merkle Root check passed for %s peer=%s\n", inv.hash.ToString(), pfrom->GetLogName());

    // Xpress Validation - only perform xval if the chaintip matches the last blockhash in the compactblock
    bool fXVal = (pfrom->compactBlock.hashPrevBlock == chainActive.Tip()->GetBlockHash()) ? true : false;

    int missingCount = 0;
    int unnecessaryCount = 0;
    // Look for each transaction in our various pools and buffers.
    // With compactblocks the vTxHashes contains only the first 8 bytes of the tx hash.
    {
        READLOCK(orphanpool.cs);
        LOCK(cs_xval);
        if (!ReconstructBlock(pfrom, fXVal, missingCount, unnecessaryCount))
            return false;
    }

    // If we're still missing transactions then bail out and just request the full block. This should never
    // happen unless we're under some kind of attack or somehow we lost transactions out of our memory pool
    // while we were retreiving missing transactions.
    if (missingCount > 0)
    {
        // Since we can't process this compactblock then clear out the data from memory
        compactdata.ClearCompactBlockData(pfrom, inv.hash);

        thinrelay.RequestBlock(pfrom, inv.hash);
        return error("Still missing transactions after reconstructing block, peer=%s: re-requesting a full block",
            pfrom->GetLogName());
    }
    else
    {
        // We have all the transactions now that are in this block: try to reassemble and process.
        CInv inv2(CInv(MSG_BLOCK, compactReReqResponse.blockhash));

        // for compression statistics, we have to add up the size of compactblock and the re-requested Txns.
        int nSizeCompactBlockTx = msgSize;
        int blockSize = pfrom->compactBlock.GetBlockSize();
        LOG(CMPCT,
            "Reassembled compactReReqResponse for %s (%d bytes). Message was %d bytes (compactblock) and %d bytes "
            "(re-requested tx), compression ratio %3.2f, peer=%s\n",
            pfrom->compactBlock.GetHash().ToString(), blockSize, pfrom->nSizeCompactBlock, nSizeCompactBlockTx,
            ((float)blockSize) / ((float)pfrom->nSizeCompactBlock + (float)nSizeCompactBlockTx), pfrom->GetLogName());

        // Update run-time statistics of compactblock bandwidth savings.
        // We add the original compactblock size with the size of transactions that were re-requested.
        // This is NOT double counting since we never accounted for the original compactblock due to the re-request.
        compactdata.UpdateInBound(nSizeCompactBlockTx + pfrom->nSizeCompactBlock, blockSize);
        LOG(CMPCT, "compactblock stats: %s\n", compactdata.ToString());

        // create a non-deleting shared pointer to wrap pfrom->compactBlock. We know that compactBlock will outlast the
        // thread because the thread has a node reference.
        PV->HandleBlockMessage(pfrom, strCommand, MakeBlockRef(pfrom->compactBlock), inv2);
    }

    return true;
}

static bool ReconstructBlock(CNode *pfrom, const bool fXVal, int &missingCount, int &unnecessaryCount)
{
    AssertLockHeld(orphanpool.cs);
    AssertLockHeld(cs_xval);

    // We must have all the full tx hashes by this point.  We first check for any duplicate
    // transaction ids.  This is a possible attack vector and has been used in the past.
    {
        std::set<uint256> setHashes(pfrom->vCompactBlockHashes.begin(), pfrom->vCompactBlockHashes.end());
        if (setHashes.size() != pfrom->vCompactBlockHashes.size())
        {
            compactdata.ClearCompactBlockData(pfrom, pfrom->compactBlock.GetBlockHeader().GetHash());

            dosMan.Misbehaving(pfrom, 10);
            return error("Duplicate transaction ids, peer=%s", pfrom->GetLogName());
        }
    }

    // The total maximum bytes that we can use to create a compactblock. We use shared pointers for
    // the transactions in the compactblock so we don't need to make as much memory available as we did in
    // the past. We caluculate the max memory allowed by using the largest block size possible, which is the
    // (maxMessageSizeMultiplier * excessiveBlockSize), then divide that by the smallest transaction possible
    // which is 158 bytes on a 32bit system.  That gives us the largest number of transactions possible in a block.
    // Then we multiply number of possible transactions by the size of a shared pointer.
    // NOTE * The 158 byte smallest txn possible was found by getting the smallest serialized size of a txn directly
    //        from the blockchain, on a 32bit system.
    CTransactionRef dummyptx = nullptr;
    uint32_t nTxSize = sizeof(dummyptx);
    uint64_t maxAllowedSize = nTxSize * maxMessageSizeMultiplier * excessiveBlockSize / 158;

    // Look for each transaction in our various pools and buffers.
    // With compactblocks the vTxHashes contains only the first 8 bytes of the tx hash.
    for (const uint256 &hash : pfrom->vCompactBlockHashes)
    {
        // Replace the truncated hash with the full hash value if it exists
        CTransactionRef ptx = nullptr;
        if (!hash.IsNull())
        {
            // Check the commit queue first. If we check the mempool first and it's not in there then when we release
            // the lock on the mempool it may get transfered from the commitQ to the mempool before we have time to
            // grab the lock on the commitQ and we'll think we don't have the transaction.
            // the mempool.
            bool inMemPool = false;
            bool inCommitQ = false;
            ptx = CommitQGet(hash);
            if (ptx)
            {
                inCommitQ = true;
            }
            else
            {
                // if it's not in the mempool then check the commitQ
                ptx = mempool.get(hash);
                if (ptx)
                    inMemPool = true;
            }

            uint64_t nShortId = GetShortID(pfrom->shorttxidk0, pfrom->shorttxidk1, hash);
            bool inMissingTx = pfrom->mapMissingTx.count(nShortId) > 0;
            bool inOrphanCache = orphanpool.mapOrphanTransactions.count(hash) > 0;

            if (((inMemPool || inCommitQ) && inMissingTx) || (inOrphanCache && inMissingTx))
                unnecessaryCount++;

            if (inOrphanCache)
            {
                ptx = orphanpool.mapOrphanTransactions[hash].ptx;
                setUnVerifiedOrphanTxHash.insert(hash);
            }
            else if ((inMemPool || inCommitQ) && fXVal)
                setPreVerifiedTxHash.insert(hash);
            else if (inMissingTx)
                ptx = pfrom->mapMissingTx[nShortId];
        }
        if (!ptx)
            missingCount++;

        // In order to prevent a memory exhaustion attack we track transaction bytes used to create Block
        // to see if we've exceeded any limits and if so clear out data and return.
        if (compactdata.AddCompactBlockBytes(nTxSize, pfrom) > maxAllowedSize)
        {
            LEAVE_CRITICAL_SECTION(cs_xval); // maintain locking order with vNodes
            if (ClearLargestCompactBlockAndDisconnect(pfrom))
            {
                ENTER_CRITICAL_SECTION(cs_xval);
                return error(
                    "Reconstructed block %s (size:%llu) has caused max memory limit %llu bytes to be exceeded, peer=%s",
                    pfrom->compactBlock.GetHash().ToString(), pfrom->nLocalCompactBlockBytes, maxAllowedSize,
                    pfrom->GetLogName());
            }
            ENTER_CRITICAL_SECTION(cs_xval);
        }
        if (pfrom->nLocalCompactBlockBytes > maxAllowedSize)
        {
            compactdata.ClearCompactBlockData(pfrom, pfrom->compactBlock.GetBlockHeader().GetHash());
            pfrom->fDisconnect = true;
            return error(
                "Reconstructed block %s (size:%llu) has caused max memory limit %llu bytes to be exceeded, peer=%s",
                pfrom->compactBlock.GetHash().ToString(), pfrom->nLocalCompactBlockBytes, maxAllowedSize,
                pfrom->GetLogName());
        }

        // Add this transaction. If the tx is null we still add it as a placeholder to keep the correct ordering.
        pfrom->compactBlock.vtx.emplace_back(ptx);
    }
    return true;
}


template <class T>
void CCompactBlockData::expireStats(std::map<int64_t, T> &statsMap)
{
    AssertLockHeld(cs_compactblockstats);
    // Delete any entries that are more than 24 hours old
    int64_t nTimeCutoff = getTimeForStats() - 60 * 60 * 24 * 1000;

    typename std::map<int64_t, T>::iterator iter = statsMap.begin();
    while (iter != statsMap.end())
    {
        // increment to avoid iterator becoming invalid when erasing below
        typename std::map<int64_t, T>::iterator mi = iter++;

        if (mi->first < nTimeCutoff)
            statsMap.erase(mi);
    }
}

template <class T>
void CCompactBlockData::updateStats(std::map<int64_t, T> &statsMap, T value)
{
    AssertLockHeld(cs_compactblockstats);
    statsMap[getTimeForStats()] = value;
    expireStats(statsMap);
}


//  Calculate average of values in map. Return 0 for no entries.
// Expires values before calculation.
double CCompactBlockData::average(std::map<int64_t, uint64_t> &map)
{
    AssertLockHeld(cs_compactblockstats);

    expireStats(map);

    if (map.size() == 0)
        return 0.0;

    uint64_t accum = 0U;
    for (std::pair<int64_t, uint64_t> const &ref : map)
    {
        // avoid wraparounds
        accum = std::max(accum, accum + ref.second);
    }
    return (double)accum / map.size();
}

double CCompactBlockData::computeTotalBandwidthSavingsInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_compactblockstats)
{
    AssertLockHeld(cs_compactblockstats);

    return double(nOriginalSize() - nCompactSize());
}

double CCompactBlockData::compute24hAverageCompressionInternal(
    std::map<int64_t, std::pair<uint64_t, uint64_t> > &mapCompactBlocks) EXCLUSIVE_LOCKS_REQUIRED(cs_compactblockstats)
{
    AssertLockHeld(cs_compactblockstats);

    expireStats(mapCompactBlocks);

    double nCompressionRate = 0;
    uint64_t nCompactSizeTotal = 0;
    uint64_t nOriginalSizeTotal = 0;
    for (const auto &mi : mapCompactBlocks)
    {
        nCompactSizeTotal += mi.second.first;
        nOriginalSizeTotal += mi.second.second;
    }

    if (nOriginalSizeTotal > 0)
        nCompressionRate = 100 - (100 * (double)(nCompactSizeTotal) / nOriginalSizeTotal);

    return nCompressionRate;
}

double CCompactBlockData::compute24hInboundRerequestTxPercentInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_compactblockstats)
{
    AssertLockHeld(cs_compactblockstats);

    expireStats(mapCompactBlocksInBoundReRequestedTx);
    expireStats(mapCompactBlocksInBound);

    double nReRequestRate = 0;
    uint64_t nTotalReRequests = 0;
    uint64_t nTotalReRequestedTxs = 0;
    for (const auto &mi : mapCompactBlocksInBoundReRequestedTx)
    {
        nTotalReRequests += 1;
        nTotalReRequestedTxs += mi.second;
    }

    if (mapCompactBlocksInBound.size() > 0)
        nReRequestRate = 100 * (double)nTotalReRequests / mapCompactBlocksInBound.size();

    return nReRequestRate;
}

void CCompactBlockData::UpdateInBound(uint64_t nCompactBlockSize, uint64_t nOriginalBlockSize)
{
    LOCK(cs_compactblockstats);
    // Update InBound compactblock tracking information
    nOriginalSize += nOriginalBlockSize;
    nCompactSize += nCompactBlockSize;
    nInBoundBlocks += 1;
    updateStats(mapCompactBlocksInBound, std::pair<uint64_t, uint64_t>(nCompactBlockSize, nOriginalBlockSize));
}

void CCompactBlockData::UpdateOutBound(uint64_t nCompactBlockSize, uint64_t nOriginalBlockSize)
{
    LOCK(cs_compactblockstats);
    nOriginalSize += nOriginalBlockSize;
    nCompactSize += nCompactBlockSize;
    nOutBoundBlocks += 1;
    updateStats(mapCompactBlocksOutBound, std::pair<uint64_t, uint64_t>(nCompactBlockSize, nOriginalBlockSize));
}

void CCompactBlockData::UpdateResponseTime(double nResponseTime)
{
    LOCK(cs_compactblockstats);

    // only update stats if IBD is complete
    if (IsChainNearlySyncd() && IsCompactBlocksEnabled())
    {
        updateStats(mapCompactBlockResponseTime, nResponseTime);
    }
}

void CCompactBlockData::UpdateValidationTime(double nValidationTime)
{
    LOCK(cs_compactblockstats);

    // only update stats if IBD is complete
    if (IsChainNearlySyncd() && IsCompactBlocksEnabled())
    {
        updateStats(mapCompactBlockValidationTime, nValidationTime);
    }
}

void CCompactBlockData::UpdateInBoundReRequestedTx(int nReRequestedTx)
{
    LOCK(cs_compactblockstats);

    // Update InBound compactblock tracking information
    updateStats(mapCompactBlocksInBoundReRequestedTx, nReRequestedTx);
}

void CCompactBlockData::UpdateMempoolLimiterBytesSaved(unsigned int nBytesSaved)
{
    LOCK(cs_compactblockstats);
    nMempoolLimiterBytesSaved += nBytesSaved;
}

void CCompactBlockData::UpdateCompactBlock(uint64_t nCompactBlockSize)
{
    LOCK(cs_compactblockstats);
    nTotalCompactBlockBytes += nCompactBlockSize;
    updateStats(mapCompactBlock, nCompactBlockSize);
}

void CCompactBlockData::UpdateFullTx(uint64_t nFullTxSize)
{
    LOCK(cs_compactblockstats);
    nTotalCompactBlockBytes += nFullTxSize;
    updateStats(mapFullTx, nFullTxSize);
}

std::string CCompactBlockData::ToString()
{
    LOCK(cs_compactblockstats);
    double size = computeTotalBandwidthSavingsInternal();
    std::ostringstream ss;
    ss << nInBoundBlocks() << " inbound and " << nOutBoundBlocks() << " outbound compactblocks have saved "
       << formatInfoUnit(size) << " of bandwidth";
    return ss.str();
}

// Calculate the xthin percentage compression over the last 24 hours for inbound blocks
std::string CCompactBlockData::InBoundPercentToString()
{
    LOCK(cs_compactblockstats);

    double nCompressionRate = compute24hAverageCompressionInternal(mapCompactBlocksInBound);

    // NOTE: Potential gotcha, compute24hAverageCompressionInternal has a side-effect of calling
    //       expireStats which modifies the contents of mapCompactBlocksInBound
    // We currently rely on this side-effect for the string produced below
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Compression for " << mapCompactBlocksInBound.size()
       << " Inbound  compactblocks (last 24hrs): " << nCompressionRate << "%";
    return ss.str();
}

// Calculate the xthin percentage compression over the last 24 hours for outbound blocks
std::string CCompactBlockData::OutBoundPercentToString()
{
    LOCK(cs_compactblockstats);

    double nCompressionRate = compute24hAverageCompressionInternal(mapCompactBlocksOutBound);

    // NOTE: Potential gotcha, compute24hAverageCompressionInternal has a side-effect of calling
    //       expireStats which modifies the contents of mapCompactBlocksOutBound
    // We currently rely on this side-effect for the string produced below
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Compression for " << mapCompactBlocksOutBound.size()
       << " Outbound compactblocks (last 24hrs): " << nCompressionRate << "%";
    return ss.str();
}

// Calculate the xthin average response time over the last 24 hours
std::string CCompactBlockData::ResponseTimeToString()
{
    LOCK(cs_compactblockstats);

    expireStats(mapCompactBlockResponseTime);

    std::vector<double> vResponseTime;

    double nResponseTimeAverage = 0;
    double nPercentile = 0;
    double nTotalResponseTime = 0;
    double nTotalEntries = 0;
    for (const auto &mi : mapCompactBlockResponseTime)
    {
        nTotalEntries += 1;
        nTotalResponseTime += mi.second;
        vResponseTime.push_back(mi.second);
    }

    if (nTotalEntries > 0)
    {
        nResponseTimeAverage = (double)nTotalResponseTime / nTotalEntries;

        // Calculate the 95th percentile
        uint64_t nPercentileElement = static_cast<int>((nTotalEntries * 0.95) + 0.5) - 1;
        sort(vResponseTime.begin(), vResponseTime.end());
        nPercentile = vResponseTime[nPercentileElement];
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "Response time   (last 24hrs) AVG:" << nResponseTimeAverage << ", 95th pcntl:" << nPercentile;
    return ss.str();
}

// Calculate the xthin average validation time over the last 24 hours
std::string CCompactBlockData::ValidationTimeToString()
{
    LOCK(cs_compactblockstats);

    expireStats(mapCompactBlockValidationTime);

    std::vector<double> vValidationTime;

    double nValidationTimeAverage = 0;
    double nPercentile = 0;
    double nTotalValidationTime = 0;
    double nTotalEntries = 0;
    for (const auto &mi : mapCompactBlockValidationTime)
    {
        nTotalEntries += 1;
        nTotalValidationTime += mi.second;
        vValidationTime.push_back(mi.second);
    }

    if (nTotalEntries > 0)
    {
        nValidationTimeAverage = (double)nTotalValidationTime / nTotalEntries;

        // Calculate the 95th percentile
        uint64_t nPercentileElement = static_cast<int>((nTotalEntries * 0.95) + 0.5) - 1;
        sort(vValidationTime.begin(), vValidationTime.end());
        nPercentile = vValidationTime[nPercentileElement];
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "Validation time (last 24hrs) AVG:" << nValidationTimeAverage << ", 95th pcntl:" << nPercentile;
    return ss.str();
}

// Calculate the xthin transaction re-request ratio and counter over the last 24 hours
std::string CCompactBlockData::ReRequestedTxToString()
{
    LOCK(cs_compactblockstats);

    double nReRequestRate = compute24hInboundRerequestTxPercentInternal();

    // NOTE: Potential gotcha, compute24hInboundRerequestTxPercentInternal has a side-effect of calling
    //       expireStats which modifies the contents of mapCompactBlocksInBoundReRequestedTx
    // We currently rely on this side-effect for the string produced below
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Tx re-request rate (last 24hrs): " << nReRequestRate
       << "% Total re-requests:" << mapCompactBlocksInBoundReRequestedTx.size();
    return ss.str();
}

std::string CCompactBlockData::MempoolLimiterBytesSavedToString()
{
    LOCK(cs_compactblockstats);
    double size = (double)nMempoolLimiterBytesSaved();
    std::ostringstream ss;
    ss << "CompactBlock mempool limiting has saved " << formatInfoUnit(size) << " of bandwidth";
    return ss.str();
}

// Calculate the average compact block size
std::string CCompactBlockData::CompactBlockToString()
{
    LOCK(cs_compactblockstats);
    double avgCompactBlockSize = average(mapCompactBlock);
    std::ostringstream ss;
    ss << "CompactBlock size (last 24hrs) AVG: " << formatInfoUnit(avgCompactBlockSize);
    return ss.str();
}

// Calculate the average size of all full txs sent with block
std::string CCompactBlockData::FullTxToString()
{
    LOCK(cs_compactblockstats);
    double avgFullTxSize = average(mapFullTx);
    std::ostringstream ss;
    ss << "compactblock full transactions size (last 24hrs) AVG: " << formatInfoUnit(avgFullTxSize);
    return ss.str();
}

// After a compactblock is finished processing or if for some reason we have to pre-empt the rebuilding
// of a compactblock then we clear out the compactblock data which can be substantial.
void CCompactBlockData::ClearCompactBlockData(CNode *pnode)
{
    // Remove bytes from counter
    compactdata.DeleteCompactBlockBytes(pnode->nLocalCompactBlockBytes, pnode);
    pnode->nLocalCompactBlockBytes = 0;

    // Clear out compactblock data we no longer need
    pnode->compactBlockWaitingForTxns = -1;
    pnode->compactBlock.SetNull();
    pnode->vCompactBlockHashes.clear();
    pnode->vShortCompactBlockHashes.clear();
    pnode->mapMissingTx.clear();

    LOG(CMPCT, "Total in memory compactblockbytes size after clearing a compactblock is %ld bytes\n",
        compactdata.GetCompactBlockBytes());
}


void CCompactBlockData::ClearCompactBlockData(CNode *pnode, const uint256 &hash)
{
    // We must make sure to clear the compactblock data first before clearing the compactblock in flight.
    ClearCompactBlockData(pnode);
    thinrelay.ClearThinTypeBlockInFlight(pnode, hash);
}

void CCompactBlockData::ClearCompactBlockStats()
{
    LOCK(cs_compactblockstats);

    nOriginalSize.Clear();
    nCompactSize.Clear();
    nInBoundBlocks.Clear();
    nOutBoundBlocks.Clear();
    nMempoolLimiterBytesSaved.Clear();
    nTotalCompactBlockBytes.Clear();
    nTotalFullTxBytes.Clear();

    mapCompactBlocksInBound.clear();
    mapCompactBlocksOutBound.clear();
    mapCompactBlockResponseTime.clear();
    mapCompactBlockValidationTime.clear();
    mapCompactBlocksInBoundReRequestedTx.clear();
    mapCompactBlock.clear();
    mapFullTx.clear();
}

uint64_t CCompactBlockData::AddCompactBlockBytes(uint64_t bytes, CNode *pfrom)
{
    pfrom->nLocalCompactBlockBytes += bytes;
    uint64_t ret = nCompactBlockBytes.fetch_add(bytes) + bytes;

    return ret;
}

void CCompactBlockData::DeleteCompactBlockBytes(uint64_t bytes, CNode *pfrom)
{
    if (bytes <= pfrom->nLocalCompactBlockBytes)
        pfrom->nLocalCompactBlockBytes -= bytes;

    if (bytes <= nCompactBlockBytes)
    {
        nCompactBlockBytes.fetch_sub(bytes);
    }
}

void CCompactBlockData::ResetCompactBlockBytes() { nCompactBlockBytes.store(0); }
uint64_t CCompactBlockData::GetCompactBlockBytes() { return nCompactBlockBytes.load(); }
void CCompactBlockData::FillCompactBlockQuickStats(CompactBlockQuickStats &stats)
{
    if (!IsCompactBlocksEnabled())
        return;

    LOCK(cs_compactblockstats);

    stats.nTotalInbound = nInBoundBlocks();
    stats.nTotalOutbound = nOutBoundBlocks();
    stats.nTotalBandwidthSavings = computeTotalBandwidthSavingsInternal();

    // NOTE: The following calls rely on the side-effect of the compute*Internal
    //       calls also calling expireStats on the associated statistics maps
    //       This is why we set the % value first, then the count second for compression values
    stats.fLast24hInboundCompression = compute24hAverageCompressionInternal(mapCompactBlocksInBound);
    stats.nLast24hInbound = mapCompactBlocksInBound.size();
    stats.fLast24hOutboundCompression = compute24hAverageCompressionInternal(mapCompactBlocksOutBound);
    stats.nLast24hOutbound = mapCompactBlocksOutBound.size();
    stats.fLast24hRerequestTxPercent = compute24hInboundRerequestTxPercentInternal();
    stats.nLast24hRerequestTx = mapCompactBlocksInBoundReRequestedTx.size();
}

bool IsCompactBlocksEnabled() { return GetBoolArg("-use-compactblocks", true); }
bool ClearLargestCompactBlockAndDisconnect(CNode *pfrom)
{
    CNode *pLargest = nullptr;
    LOCK(cs_vNodes);
    for (CNode *pnode : vNodes)
    {
        if ((pLargest == nullptr) || (pnode->nLocalCompactBlockBytes > pLargest->nLocalCompactBlockBytes))
            pLargest = pnode;
    }
    if (pLargest != nullptr)
    {
        compactdata.ClearCompactBlockData(pLargest, pLargest->compactBlock.GetBlockHeader().GetHash());
        pLargest->fDisconnect = true;

        // If the our node is currently using up the most compactblock bytes then return true so that we
        // can stop processing this compactblock and let the disconnection happen.
        if (pfrom == pLargest)
            return true;
    }

    return false;
}

void SendCompactBlock(ConstCBlockRef pblock, CNode *pfrom, const CInv &inv)
{
    if (inv.type == MSG_CMPCT_BLOCK)
    {
        CompactBlock compactBlock;
        compactBlock = CompactBlock(*pblock, &pfrom->filterInventoryKnown);
        uint64_t nSizeBlock = pblock->GetBlockSize();
        uint64_t nSizeCompactBlock = ::GetSerializeSize(compactBlock, SER_NETWORK, PROTOCOL_VERSION);

        // Send a compact block
        pfrom->PushMessage(NetMsgType::CMPCTBLOCK, compactBlock);
        LOG(CMPCT, "Sent compact block - compactblock size: %d vs block size: %d peer: %s\n", nSizeCompactBlock,
            nSizeBlock, pfrom->GetLogName());

        pfrom->blocksSent += 1;
    }
}


bool IsCompactBlockValid(CNode *pfrom, const std::vector<CTransaction> &vMissingTx, const CBlockHeader &header)
{
    // Check that that there is at least one txn in the xthin and that the first txn is the coinbase
    if (vMissingTx.empty())
    {
        return error(
            "No Transactions found in compactblock %s from peer %s", header.GetHash().ToString(), pfrom->GetLogName());
    }
    if (!vMissingTx[0].IsCoinBase())
    {
        return error("First txn is not coinbase for compactblock %s from peer %s", header.GetHash().ToString(),
            pfrom->GetLogName());
    }

    // check block header
    CValidationState state;
    if (!CheckBlockHeader(header, state, true))
    {
        return error("Received invalid header for compactblock %s from peer %s", header.GetHash().ToString(),
            pfrom->GetLogName());
    }
    if (state.Invalid())
    {
        return error("Received invalid header for compactblock %s from peer %s", header.GetHash().ToString(),
            pfrom->GetLogName());
    }

    return true;
}
