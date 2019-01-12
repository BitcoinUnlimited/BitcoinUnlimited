// Copyright (c) 2016-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "blockrelay/blockrelay_common.h"
#include "blockrelay/thinblock.h"
#include "blockstorage/blockstorage.h"
#include "chainparams.h"
#include "connmgr.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "dosman.h"
#include "expedited.h"
#include "net.h"
#include "parallel.h"
#include "policy/policy.h"
#include "pow.h"
#include "requestManager.h"
#include "timedata.h"
#include "txadmission.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "util.h"
#include "utiltime.h"
#include "validation/validation.h"
#include "xversionkeys.h"

static bool DEFAULT_BLOOM_FILTER_TARGETING = true;
static bool ReconstructBlock(CNode *pfrom, const bool fXVal, int &missingCount, int &unnecessaryCount);

CThinBlock::CThinBlock(const CBlock &block, const CBloomFilter &filter)
{
    header = block.GetBlockHeader();

    unsigned int nTx = block.vtx.size();
    vTxHashes.reserve(nTx);
    for (unsigned int i = 0; i < nTx; i++)
    {
        const uint256 &hash = block.vtx[i]->GetHash();
        vTxHashes.push_back(hash);

        // Find the transactions that do not match the filter.
        // These are the ones we need to relay back to the requesting peer.
        // NOTE: We always add the first tx, the coinbase as it is the one
        //       most often missing.
        if (!filter.contains(hash) || i == 0)
            vMissingTx.push_back(*block.vtx[i]);
    }
}

/**
 * Handle an incoming thin block.  The block is fully validated, and if any transactions are missing, we fall
 * back to requesting a full block.
 */
bool CThinBlock::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    if (!pfrom->ThinBlockCapable())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("Thinblock message received from a non thinblock node, peer=%s", pfrom->GetLogName());
    }

    CThinBlock thinBlock;
    vRecv >> thinBlock;

    // Message consistency checking
    if (!IsThinBlockValid(pfrom, thinBlock.vMissingTx, thinBlock.header))
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("Invalid thinblock received");
    }

    // Is there a previous block or header to connect with?
    CBlockIndex *pprev = LookupBlockIndex(thinBlock.header.hashPrevBlock);
    if (!pprev)
        return error("thinblock from peer %s will not connect, unknown previous block %s", pfrom->GetLogName(),
            thinBlock.header.hashPrevBlock.ToString());

    CValidationState state;
    if (!ContextualCheckBlockHeader(thinBlock.header, state, pprev))
    {
        // Thin block does not fit within our blockchain
        dosMan.Misbehaving(pfrom, 100);
        return error(
            "thinblock from peer %s contextual error: %s", pfrom->GetLogName(), state.GetRejectReason().c_str());
    }

    CInv inv(MSG_BLOCK, thinBlock.header.GetHash());
    int nSizeThinBlock = ::GetSerializeSize(thinBlock, SER_NETWORK, PROTOCOL_VERSION);
    LOG(THIN, "received thinblock %s from peer %s of %d bytes\n", inv.hash.ToString(), pfrom->GetLogName(),
        nSizeThinBlock);

    // Ban a node for sending unrequested thinblocks unless from an expedited node.
    {
        if (!thinrelay.IsBlockInFlight(pfrom, NetMsgType::XTHINBLOCK) && !connmgr->IsExpeditedUpstream(pfrom))
        {
            dosMan.Misbehaving(pfrom, 100);
            return error("unrequested thinblock from peer %s", pfrom->GetLogName());
        }
    }

    // Check if we've already received this block and have it on disk
    if (AlreadyHaveBlock(inv))
    {
        requester.AlreadyReceived(pfrom, inv);
        thindata.ClearThinBlockData(pfrom, inv.hash);

        LOG(THIN, "Received thinblock but returning because we already have this block %s on disk, peer=%s\n",
            inv.hash.ToString(), pfrom->GetLogName());
        return true;
    }

    return thinBlock.process(pfrom, nSizeThinBlock);
}

bool CThinBlock::process(CNode *pfrom, int nSizeThinBlock)
{
    // Xpress Validation - only perform xval if the chaintip matches the last blockhash in the thinblock
    bool fXVal;
    {
        LOCK(cs_main);
        fXVal = (header.hashPrevBlock == chainActive.Tip()->GetBlockHash()) ? true : false;
    }

    thindata.ClearThinBlockData(pfrom);
    pfrom->nSizeThinBlock = nSizeThinBlock;

    pfrom->thinBlock.nVersion = header.nVersion;
    pfrom->thinBlock.nBits = header.nBits;
    pfrom->thinBlock.nNonce = header.nNonce;
    pfrom->thinBlock.nTime = header.nTime;
    pfrom->thinBlock.hashMerkleRoot = header.hashMerkleRoot;
    pfrom->thinBlock.hashPrevBlock = header.hashPrevBlock;
    pfrom->thinBlockHashes = vTxHashes;

    // Check that the merkleroot matches the merkleroot calculated from the hashes provided.
    bool mutated;
    uint256 merkleroot = ComputeMerkleRoot(vTxHashes, &mutated);
    if (header.hashMerkleRoot != merkleroot || mutated)
    {
        thindata.ClearThinBlockData(pfrom, header.GetHash());

        dosMan.Misbehaving(pfrom, 100);
        return error("Thinblock merkle root does not match computed merkle root, peer=%s", pfrom->GetLogName());
    }

    // Create the mapMissingTx from all the supplied tx's in the xthinblock
    for (const CTransaction &tx : vMissingTx)
        pfrom->mapMissingTx[tx.GetHash().GetCheapHash()] = MakeTransactionRef(tx);

    {
        READLOCK(orphanpool.cs);
        LOCK(cs_xval);
        int missingCount = 0;
        int unnecessaryCount = 0;

        if (!ReconstructBlock(pfrom, fXVal, missingCount, unnecessaryCount))
            return false;

        pfrom->thinBlockWaitingForTxns = missingCount;
        LOG(THIN, "Thinblock %s waiting for: %d, unnecessary: %d, total txns: %d received txns: %d peer=%s\n",
            pfrom->thinBlock.GetHash().ToString(), pfrom->thinBlockWaitingForTxns, unnecessaryCount,
            pfrom->thinBlock.vtx.size(), pfrom->mapMissingTx.size(), pfrom->GetLogName());
    } // end lock orphanpool.cs, mempool.cs, cs_xval
    LOG(THIN, "Total in memory thinblockbytes size is %ld bytes\n", thindata.GetThinBlockBytes());

    // Clear out data we no longer need before processing block.
    pfrom->thinBlockHashes.clear();

    if (pfrom->thinBlockWaitingForTxns == 0)
    {
        // We have all the transactions now that are in this block: try to reassemble and process.
        pfrom->thinBlockWaitingForTxns = -1;
        int blockSize = pfrom->thinBlock.GetBlockSize();
        float nCompressionRatio = 0.0;
        if (nSizeThinBlock > 0)
            nCompressionRatio = (float)blockSize / (float)nSizeThinBlock;
        LOG(THIN, "Reassembled thinblock for %s (%d bytes). Message was %d bytes, compression ratio %3.2f peer=%s\n",
            pfrom->thinBlock.GetHash().ToString(), blockSize, nSizeThinBlock, nCompressionRatio, pfrom->GetLogName());

        // Update run-time statistics of thin block bandwidth savings
        thindata.UpdateInBound(nSizeThinBlock, blockSize);
        LOG(THIN, "thin block stats: %s\n", thindata.ToString());

        // create a non-deleting shared pointer to wrap pfrom->thinBlock.  We know that thinBlock will outlast the
        // thread because the thread has a node reference.
        PV->HandleBlockMessage(pfrom, NetMsgType::THINBLOCK, MakeBlockRef(pfrom->thinBlock), GetInv());
    }
    else if (pfrom->thinBlockWaitingForTxns > 0)
    {
        // This marks the end of the transactions we've received. If we get this and we have NOT been able to
        // finish reassembling the block, we need to re-request the full regular block
        LOG(THIN, "Missing %d Thinblock transactions, re-requesting a regular block from peer=%s\n",
            pfrom->thinBlockWaitingForTxns, pfrom->GetLogName());
        thinrelay.RequestBlock(pfrom, header.GetHash());

        thindata.UpdateInBoundReRequestedTx(pfrom->thinBlockWaitingForTxns);
        thindata.ClearThinBlockData(pfrom, header.GetHash());
        {
            // Clear the set since we do not use XVal for full blocks
            LOCK(cs_xval);
            setPreVerifiedTxHash.clear();
        }
    }

    return true;
}


CXThinBlock::CXThinBlock(const CBlock &block, const CBloomFilter *filter)
{
    header = block.GetBlockHeader();
    this->collision = false;

    unsigned int nTx = block.vtx.size();
    vTxHashes.reserve(nTx);
    std::set<uint64_t> setPartialTxHash;
    for (unsigned int i = 0; i < nTx; i++)
    {
        const uint256 hash256 = block.vtx[i]->GetHash();
        uint64_t cheapHash = hash256.GetCheapHash();
        vTxHashes.push_back(cheapHash);

        if (setPartialTxHash.count(cheapHash))
            this->collision = true;
        setPartialTxHash.insert(cheapHash);

        // Find the transactions that do not match the filter.
        // These are the ones we need to relay back to the requesting peer.
        // NOTE: We always add the first tx, the coinbase as it is the one
        //       most often missing.
        if ((filter && !filter->contains(hash256)) || i == 0)
            vMissingTx.push_back(*block.vtx[i]);
    }
}

CXThinBlock::CXThinBlock(const CBlock &block)
{
    header = block.GetBlockHeader();
    this->collision = false;

    unsigned int nTx = block.vtx.size();
    vTxHashes.reserve(nTx);
    std::set<uint64_t> setPartialTxHash;

    READLOCK(orphanpool.cs);
    for (unsigned int i = 0; i < nTx; i++)
    {
        const uint256 hash256 = block.vtx[i]->GetHash();
        uint64_t cheapHash = hash256.GetCheapHash();
        vTxHashes.push_back(cheapHash);

        if (setPartialTxHash.count(cheapHash))
            this->collision = true;
        setPartialTxHash.insert(cheapHash);

        // if it is missing from this node, then add it to the thin block
        if (!((mempool.exists(hash256)) ||
                (orphanpool.mapOrphanTransactions.find(hash256) != orphanpool.mapOrphanTransactions.end())))
        {
            vMissingTx.push_back(*block.vtx[i]);
        }
        // We always add the first tx, the coinbase as it is the one
        // most often missing.
        else if (i == 0)
            vMissingTx.push_back(*block.vtx[i]);
    }
}

CXThinBlockTx::CXThinBlockTx(uint256 blockHash, std::vector<CTransaction> &vTx)
{
    blockhash = blockHash;
    vMissingTx = vTx;
}

bool CXThinBlockTx::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    if (!pfrom->ThinBlockCapable())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("xblocktx message received from a non XTHIN node, peer=%s", pfrom->GetLogName());
    }

    std::string strCommand = NetMsgType::XBLOCKTX;
    size_t msgSize = vRecv.size();
    CXThinBlockTx thinBlockTx;
    vRecv >> thinBlockTx;

    // Message consistency checking
    CInv inv(MSG_XTHINBLOCK, thinBlockTx.blockhash);
    if (thinBlockTx.vMissingTx.empty() || thinBlockTx.blockhash.IsNull())
    {
        thindata.ClearThinBlockData(pfrom, inv.hash);

        dosMan.Misbehaving(pfrom, 100);
        return error("incorrectly constructed xblocktx or inconsistent thinblock data received.  Banning peer=%s",
            pfrom->GetLogName());
    }

    LOG(THIN, "received xblocktx for %s peer=%s\n", inv.hash.ToString(), pfrom->GetLogName());
    {
        // Do not process unrequested xblocktx unless from an expedited node.
        if (!thinrelay.IsBlockInFlight(pfrom, NetMsgType::XTHINBLOCK) && !connmgr->IsExpeditedUpstream(pfrom))
        {
            dosMan.Misbehaving(pfrom, 10);
            return error(
                "Received xblocktx %s from peer %s but was unrequested", inv.hash.ToString(), pfrom->GetLogName());
        }
    }

    // Check if we've already received this block and have it on disk
    if (AlreadyHaveBlock(inv))
    {
        requester.AlreadyReceived(pfrom, inv);
        thindata.ClearThinBlockData(pfrom, inv.hash);

        LOG(THIN, "Received xblocktx but returning because we already have this block %s on disk, peer=%s\n",
            inv.hash.ToString(), pfrom->GetLogName());
        return true;
    }

    // Create the mapMissingTx from all the supplied tx's in the xthinblock
    for (const CTransaction &tx : thinBlockTx.vMissingTx)
        pfrom->mapMissingTx[tx.GetHash().GetCheapHash()] = MakeTransactionRef(tx);

    // Get the full hashes from the xblocktx and add them to the thinBlockHashes vector.  These should
    // be all the missing or null hashes that we re-requested.
    int count = 0;
    for (size_t i = 0; i < pfrom->thinBlockHashes.size(); i++)
    {
        if (pfrom->thinBlockHashes[i].IsNull())
        {
            std::map<uint64_t, CTransactionRef>::iterator val = pfrom->mapMissingTx.find(pfrom->xThinBlockHashes[i]);
            if (val != pfrom->mapMissingTx.end())
            {
                pfrom->thinBlockHashes[i] = val->second->GetHash();
            }
            count++;
        }
    }
    LOG(THIN, "Got %d Re-requested txs, needed %d of them from peer=%s\n", thinBlockTx.vMissingTx.size(), count,
        pfrom->GetLogName());

    // At this point we should have all the full hashes in the block. Check that the merkle
    // root in the block header matches the merkleroot calculated from the hashes provided.
    bool mutated;
    uint256 merkleroot = ComputeMerkleRoot(pfrom->thinBlockHashes, &mutated);
    if (pfrom->thinBlock.hashMerkleRoot != merkleroot || mutated)
    {
        thindata.ClearThinBlockData(pfrom, inv.hash);

        dosMan.Misbehaving(pfrom, 100);
        return error("Merkle root for %s does not match computed merkle root, peer=%s", inv.hash.ToString(),
            pfrom->GetLogName());
    }
    LOG(THIN, "Merkle Root check passed for %s peer=%s\n", inv.hash.ToString(), pfrom->GetLogName());

    // Xpress Validation - only perform xval if the chaintip matches the last blockhash in the thinblock
    bool fXVal;
    {
        LOCK(cs_main);
        fXVal = (pfrom->thinBlock.hashPrevBlock == chainActive.Tip()->GetBlockHash()) ? true : false;
    }

    int missingCount = 0;
    int unnecessaryCount = 0;
    // Look for each transaction in our various pools and buffers.
    // With xThinBlocks the vTxHashes contains only the first 8 bytes of the tx hash.
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
        // Since we can't process this thinblock then clear out the data from memory
        thindata.ClearThinBlockData(pfrom, inv.hash);

        thinrelay.RequestBlock(pfrom, inv.hash);
        return error("Still missing transactions after reconstructing block, peer=%s: re-requesting a full block",
            pfrom->GetLogName());
    }
    else
    {
        // We have all the transactions now that are in this block: try to reassemble and process.
        CInv inv2(CInv(MSG_BLOCK, thinBlockTx.blockhash));

        // for compression statistics, we have to add up the size of xthinblock and the re-requested thinBlockTx.
        int nSizeThinBlockTx = msgSize;
        int blockSize = pfrom->thinBlock.GetBlockSize();
        LOG(THIN, "Reassembled xblocktx for %s (%d bytes). Message was %d bytes (thinblock) and %d bytes "
                  "(re-requested tx), compression ratio %3.2f, peer=%s\n",
            pfrom->thinBlock.GetHash().ToString(), blockSize, pfrom->nSizeThinBlock, nSizeThinBlockTx,
            ((float)blockSize) / ((float)pfrom->nSizeThinBlock + (float)nSizeThinBlockTx), pfrom->GetLogName());

        // Update run-time statistics of thin block bandwidth savings.
        // We add the original thinblock size with the size of transactions that were re-requested.
        // This is NOT double counting since we never accounted for the original thinblock due to the re-request.
        thindata.UpdateInBound(nSizeThinBlockTx + pfrom->nSizeThinBlock, blockSize);
        LOG(THIN, "thin block stats: %s\n", thindata.ToString());

        // create a non-deleting shared pointer to wrap pfrom->thinBlock.  We know that thinBlock will outlast the
        // thread because the thread has a node reference.
        PV->HandleBlockMessage(pfrom, strCommand, MakeBlockRef(pfrom->thinBlock), inv2);
    }

    return true;
}

CXRequestThinBlockTx::CXRequestThinBlockTx(uint256 blockHash, std::set<uint64_t> &setHashesToRequest)
{
    blockhash = blockHash;
    setCheapHashesToRequest = setHashesToRequest;
}

bool CXRequestThinBlockTx::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    if (!pfrom->ThinBlockCapable())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("get_xblocktx message received from a non XTHIN node, peer=%s", pfrom->GetLogName());
    }

    CXRequestThinBlockTx thinRequestBlockTx;
    vRecv >> thinRequestBlockTx;

    // Message consistency checking
    if (thinRequestBlockTx.setCheapHashesToRequest.empty() || thinRequestBlockTx.blockhash.IsNull())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("incorrectly constructed get_xblocktx received.  Banning peer=%s", pfrom->GetLogName());
    }

    // We use MSG_TX here even though we refer to blockhash because we need to track
    // how many xblocktx requests we make in case of DOS
    CInv inv(MSG_TX, thinRequestBlockTx.blockhash);
    LOG(THIN, "received get_xblocktx for %s peer=%s\n", inv.hash.ToString(), pfrom->GetLogName());

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
        else
        {
            for (unsigned int i = 0; i < block.vtx.size(); i++)
            {
                uint64_t cheapHash = block.vtx[i]->GetHash().GetCheapHash();
                if (thinRequestBlockTx.setCheapHashesToRequest.count(cheapHash))
                    vTx.push_back(*block.vtx[i]);
            }
        }
    }
    CXThinBlockTx thinBlockTx(thinRequestBlockTx.blockhash, vTx);
    pfrom->PushMessage(NetMsgType::XBLOCKTX, thinBlockTx);
    pfrom->txsSent += vTx.size();

    return true;
}

bool CXThinBlock::CheckBlockHeader(const CBlockHeader &block, CValidationState &state)
{
    // Check proof of work matches claimed amount
    if (!CheckProofOfWork(header.GetHash(), header.nBits, Params().GetConsensus()))
        return state.DoS(50, error("CheckBlockHeader(): proof of work failed"), REJECT_INVALID, "high-hash");

    // Check timestamp
    if (header.GetBlockTime() > GetAdjustedTime() + 2 * 60 * 60)
        return state.Invalid(
            error("CheckBlockHeader(): block timestamp too far in the future"), REJECT_INVALID, "time-too-new");

    return true;
}

/**
 * Handle an incoming Xthin or Xpedited block
 * Once the block is validated apart from the Merkle root, forward the Xpedited block with a hop count of nHops.
 */
bool CXThinBlock::HandleMessage(CDataStream &vRecv, CNode *pfrom, std::string strCommand, unsigned nHops)
{
    if (!pfrom->ThinBlockCapable())
    {
        dosMan.Misbehaving(pfrom, 5);
        return error("%s message received from a non XTHIN node, peer=%s", strCommand, pfrom->GetLogName());
    }

    int nSizeThinBlock = vRecv.size();
    CInv inv(MSG_BLOCK, uint256());

    CXThinBlock thinBlock;
    vRecv >> thinBlock;

    {
        // Message consistency checking (FIXME: some redundancy here with AcceptBlockHeader)
        if (!IsThinBlockValid(pfrom, thinBlock.vMissingTx, thinBlock.header))
        {
            dosMan.Misbehaving(pfrom, 100);
            LOGA("Received an invalid %s from peer %s\n", strCommand, pfrom->GetLogName());

            thindata.ClearThinBlockData(pfrom, thinBlock.header.GetHash());
            return false;
        }

        // Is there a previous block or header to connect with?
        if (!LookupBlockIndex(thinBlock.header.hashPrevBlock))
        {
            return error("xthinblock from peer %s will not connect, unknown previous block %s", pfrom->GetLogName(),
                thinBlock.header.hashPrevBlock.ToString());
        }

        LOCK(cs_main);
        CValidationState state;
        CBlockIndex *pIndex = nullptr;
        if (!AcceptBlockHeader(thinBlock.header, state, Params(), &pIndex))
        {
            int nDoS;
            if (state.IsInvalid(nDoS))
            {
                if (nDoS > 0)
                    dosMan.Misbehaving(pfrom, nDoS);
                LOGA("Received an invalid %s header from peer %s\n", strCommand, pfrom->GetLogName());
            }

            thindata.ClearThinBlockData(pfrom, thinBlock.header.GetHash());
            return false;
        }

        // pIndex should always be set by AcceptBlockHeader
        if (!pIndex)
        {
            LOGA("INTERNAL ERROR: pIndex null in CXThinBlock::HandleMessage");
            thindata.ClearThinBlockData(pfrom, thinBlock.header.GetHash());
            return true;
        }

        inv.hash = pIndex->GetBlockHash();
        requester.UpdateBlockAvailability(pfrom->GetId(), inv.hash);

        // Return early if we already have the block data
        if (pIndex->nStatus & BLOCK_HAVE_DATA)
        {
            // Tell the Request Manager we received this block
            requester.AlreadyReceived(pfrom, inv);

            thindata.ClearThinBlockData(pfrom, thinBlock.header.GetHash());
            LOG(THIN, "Received xthinblock but returning because we already have block data %s from peer %s hop"
                      " %d size %d bytes\n",
                inv.hash.ToString(), pfrom->GetLogName(), nHops, nSizeThinBlock);
            return true;
        }

        // Request full block if it isn't extending the best chain
        if (pIndex->nChainWork <= chainActive.Tip()->nChainWork)
        {
            thinrelay.RequestBlock(pfrom, thinBlock.header.GetHash());
            thindata.ClearThinBlockData(pfrom, thinBlock.header.GetHash());
            LOGA("%s %s from peer %s received but does not extend longest chain; requesting full block\n", strCommand,
                inv.hash.ToString(), pfrom->GetLogName());
            return true;
        }

        // If this is an expedited block then add and entry to mapThinBlocksInFlight.
        if (nHops > 0 && connmgr->IsExpeditedUpstream(pfrom))
        {
            // If we can't add this xthin then we've already requested it
            if (!thinrelay.AddBlockInFlight(pfrom, inv.hash, NetMsgType::XTHINBLOCK))
                return true;

            LOG(THIN, "Received new expedited %s %s from peer %s hop %d size %d bytes\n", strCommand,
                inv.hash.ToString(), pfrom->GetLogName(), nHops, nSizeThinBlock);
        }
        else
        {
            LOG(THIN, "Received %s %s from peer %s. Size %d bytes.\n", strCommand, inv.hash.ToString(),
                pfrom->GetLogName(), nSizeThinBlock);

            // Do not process unrequested xthinblocks unless from an expedited node.
            if (!thinrelay.IsBlockInFlight(pfrom, NetMsgType::XTHINBLOCK) && !connmgr->IsExpeditedUpstream(pfrom))
            {
                dosMan.Misbehaving(pfrom, 10);
                return error(
                    "%s %s from peer %s but was unrequested\n", strCommand, inv.hash.ToString(), pfrom->GetLogName());
            }
        }
    }

    // Send expedited block without checking merkle root.
    SendExpeditedBlock(thinBlock, nHops, pfrom);

    return thinBlock.process(pfrom, nSizeThinBlock, strCommand);
}

bool CXThinBlock::process(CNode *pfrom,
    int nSizeThinBlock,
    std::string strCommand) // TODO: request from the "best" txn source not necessarily from the block source
{
    // In PV we must prevent two thinblocks from simulaneously processing from that were recieved from the
    // same peer. This would only happen as in the example of an expedited block coming in
    // after an xthin request, because we would never explicitly request two xthins from the same peer.
    if (PV->IsAlreadyValidating(pfrom->id))
        return false;

    // Xpress Validation - only perform xval if the chaintip matches the last blockhash in the thinblock
    bool fXVal;
    {
        LOCK(cs_main);
        fXVal = (header.hashPrevBlock == chainActive.Tip()->GetBlockHash()) ? true : false;
    }

    thindata.ClearThinBlockData(pfrom);
    pfrom->nSizeThinBlock = nSizeThinBlock;

    pfrom->thinBlock.nVersion = header.nVersion;
    pfrom->thinBlock.nBits = header.nBits;
    pfrom->thinBlock.nNonce = header.nNonce;
    pfrom->thinBlock.nTime = header.nTime;
    pfrom->thinBlock.hashMerkleRoot = header.hashMerkleRoot;
    pfrom->thinBlock.hashPrevBlock = header.hashPrevBlock;
    pfrom->xThinBlockHashes = vTxHashes;

    // Create the mapMissingTx from all the supplied tx's in the xthinblock
    for (const CTransaction &tx : vMissingTx)
        pfrom->mapMissingTx[tx.GetHash().GetCheapHash()] = MakeTransactionRef(tx);

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
            uint64_t cheapHash = mi.first.GetCheapHash();
            if (mapPartialTxHash.count(cheapHash)) // Check for collisions
                _collision = true;
            mapPartialTxHash[cheapHash] = mi.first;
        }

        LOCK(cs_xval);
        mempool.queryHashes(memPoolHashes);

        for (uint64_t i = 0; i < memPoolHashes.size(); i++)
        {
            uint64_t cheapHash = memPoolHashes[i].GetCheapHash();
            if (mapPartialTxHash.count(cheapHash)) // Check for collisions
                _collision = true;
            mapPartialTxHash[cheapHash] = memPoolHashes[i];
        }
        for (auto &mi : pfrom->mapMissingTx)
        {
            uint64_t cheapHash = mi.first;
            // Check for cheap hash collision. Only mark as collision if the full hash is not the same,
            // because the same tx could have been received into the mempool during the request of the xthinblock.
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
            for (const uint64_t &cheapHash : vTxHashes)
            {
                if (mapPartialTxHash.find(cheapHash) != mapPartialTxHash.end())
                    pfrom->thinBlockHashes.push_back(mapPartialTxHash[cheapHash]);
                else
                {
                    pfrom->thinBlockHashes.push_back(nullhash); // placeholder
                    setHashesToRequest.insert(cheapHash);
                }
            }

            // We don't need this after here.
            mapPartialTxHash.clear();

            // Reconstruct the block if there are no hashes to re-request
            if (setHashesToRequest.empty())
            {
                bool mutated;
                uint256 merkleroot = ComputeMerkleRoot(pfrom->thinBlockHashes, &mutated);
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
    LOG(THIN, "Total in memory thinblockbytes size is %ld bytes\n", thindata.GetThinBlockBytes());

    // These must be checked outside of the mempool.cs lock or deadlock may occur.
    // A merkle root mismatch here does not cause a ban because and expedited node will forward an xthin
    // without checking the merkle root, therefore we don't want to ban our expedited nodes. Just re-request
    // a full thinblock if a mismatch occurs.
    // Also, there is a remote possiblity of a Tx hash collision therefore if it occurs we re-request a normal
    // thinblock which has the full Tx hash data rather than just the truncated hash.
    if (_collision || !fMerkleRootCorrect)
    {
        if (!fMerkleRootCorrect)
            return error(
                "mismatched merkle root on xthinblock: rerequesting a thinblock, peer=%s", pfrom->GetLogName());
        else
            return error("TX HASH COLLISION for xthinblock: re-requesting a thinblock, peer=%s", pfrom->GetLogName());

        RequestThinBlock(pfrom, header.GetHash());
        thindata.ClearThinBlockData(pfrom, header.GetHash());
        return true;
    }

    pfrom->thinBlockWaitingForTxns = missingCount;
    LOG(THIN, "xthinblock waiting for: %d, unnecessary: %d, total txns: %d received txns: %d\n",
        pfrom->thinBlockWaitingForTxns, unnecessaryCount, pfrom->thinBlock.vtx.size(), pfrom->mapMissingTx.size());

    // If there are any missing hashes or transactions then we request them here.
    // This must be done outside of the mempool.cs lock or may deadlock.
    if (setHashesToRequest.size() > 0)
    {
        pfrom->thinBlockWaitingForTxns = setHashesToRequest.size();
        CXRequestThinBlockTx thinBlockTx(header.GetHash(), setHashesToRequest);
        pfrom->PushMessage(NetMsgType::GET_XBLOCKTX, thinBlockTx);

        // Update run-time statistics of thin block bandwidth savings
        thindata.UpdateInBoundReRequestedTx(pfrom->thinBlockWaitingForTxns);
        return true;
    }

    // If there are still any missing transactions then we must clear out the thinblock data
    // and re-request a full block (This should never happen because we just checked the various pools).
    if (missingCount > 0)
    {
        // Since we can't process this thinblock then clear out the data from memory and request a full block
        thindata.ClearThinBlockData(pfrom, header.GetHash());
        thinrelay.RequestBlock(pfrom, header.GetHash());
        return error("Still missing transactions for xthinblock: re-requesting a full block");
    }

    // We now have all the transactions now that are in this block
    pfrom->thinBlockWaitingForTxns = -1;
    int blockSize = pfrom->thinBlock.GetBlockSize();
    LOG(THIN, "Reassembled xthinblock for %s (%d bytes). Message was %d bytes, compression ratio %3.2f, peer=%s\n",
        pfrom->thinBlock.GetHash().ToString(), blockSize, pfrom->nSizeThinBlock,
        ((float)blockSize) / ((float)pfrom->nSizeThinBlock), pfrom->GetLogName());

    // Update run-time statistics of thin block bandwidth savings
    thindata.UpdateInBound(pfrom->nSizeThinBlock, blockSize);
    LOG(THIN, "thin block stats: %s\n", thindata.ToString().c_str());

    // Process the full block
    PV->HandleBlockMessage(pfrom, strCommand, MakeBlockRef(pfrom->thinBlock), GetInv());

    return true;
}

static bool ReconstructBlock(CNode *pfrom, const bool fXVal, int &missingCount, int &unnecessaryCount)
{
    AssertLockHeld(orphanpool.cs);
    AssertLockHeld(cs_xval);

    // We must have all the full tx hashes by this point.  We first check for any duplicate
    // transaction ids.  This is a possible attack vector and has been used in the past.
    {
        std::set<uint256> setHashes(pfrom->thinBlockHashes.begin(), pfrom->thinBlockHashes.end());
        if (setHashes.size() != pfrom->thinBlockHashes.size())
        {
            thindata.ClearThinBlockData(pfrom, pfrom->thinBlock.GetBlockHeader().GetHash());

            dosMan.Misbehaving(pfrom, 10);
            return error("Duplicate transaction ids, peer=%s", pfrom->GetLogName());
        }
    }

    // The total maximum bytes that we can use to create a thinblock. We use shared pointers for
    // the transactions in the thinblock so we don't need to make as much memory available as we did in
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
    // With xThinBlocks the vTxHashes contains only the first 8 bytes of the tx hash.
    for (const uint256 &hash : pfrom->thinBlockHashes)
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

            bool inMissingTx = pfrom->mapMissingTx.count(hash.GetCheapHash()) > 0;
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
                ptx = pfrom->mapMissingTx[hash.GetCheapHash()];
        }
        if (!ptx)
            missingCount++;

        // In order to prevent a memory exhaustion attack we track transaction bytes used to create Block
        // to see if we've exceeded any limits and if so clear out data and return.
        if (thindata.AddThinBlockBytes(nTxSize, pfrom) > maxAllowedSize)
        {
            LEAVE_CRITICAL_SECTION(cs_xval); // maintain locking order with vNodes
            if (ClearLargestThinBlockAndDisconnect(pfrom))
            {
                ENTER_CRITICAL_SECTION(cs_xval);
                return error(
                    "Reconstructed block %s (size:%llu) has caused max memory limit %llu bytes to be exceeded, peer=%s",
                    pfrom->thinBlock.GetHash().ToString(), pfrom->nLocalThinBlockBytes, maxAllowedSize,
                    pfrom->GetLogName());
            }
            ENTER_CRITICAL_SECTION(cs_xval);
        }
        if (pfrom->nLocalThinBlockBytes > maxAllowedSize)
        {
            thindata.ClearThinBlockData(pfrom, pfrom->thinBlock.GetBlockHeader().GetHash());
            pfrom->fDisconnect = true;
            return error(
                "Reconstructed block %s (size:%llu) has caused max memory limit %llu bytes to be exceeded, peer=%s",
                pfrom->thinBlock.GetHash().ToString(), pfrom->nLocalThinBlockBytes, maxAllowedSize,
                pfrom->GetLogName());
        }

        // Add this transaction. If the tx is null we still add it as a placeholder to keep the correct ordering.
        pfrom->thinBlock.vtx.emplace_back(ptx);
    }
    return true;
}

template <class T>
void CThinBlockData::expireStats(std::map<int64_t, T> &statsMap)
{
    AssertLockHeld(cs_thinblockstats);
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
void CThinBlockData::updateStats(std::map<int64_t, T> &statsMap, T value)
{
    AssertLockHeld(cs_thinblockstats);
    statsMap[getTimeForStats()] = value;
    expireStats(statsMap);
}

/**
   Calculate average of values in map. Return 0 for no entries.
   Expires values before calculation. */
double CThinBlockData::average(std::map<int64_t, uint64_t> &map)
{
    AssertLockHeld(cs_thinblockstats);

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

double CThinBlockData::computeTotalBandwidthSavingsInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_thinblockstats)
{
    AssertLockHeld(cs_thinblockstats);

    return double(nOriginalSize() - nThinSize() - nTotalBloomFilterBytes());
}

double CThinBlockData::compute24hAverageCompressionInternal(
    std::map<int64_t, std::pair<uint64_t, uint64_t> > &mapThinBlocks,
    std::map<int64_t, uint64_t> &mapBloomFilters) EXCLUSIVE_LOCKS_REQUIRED(cs_thinblockstats)
{
    AssertLockHeld(cs_thinblockstats);

    expireStats(mapThinBlocks);
    expireStats(mapBloomFilters);

    double nCompressionRate = 0;
    uint64_t nThinSizeTotal = 0;
    uint64_t nOriginalSizeTotal = 0;
    for (const auto &mi : mapThinBlocks)
    {
        nThinSizeTotal += mi.second.first;
        nOriginalSizeTotal += mi.second.second;
    }
    // We count up the bloom filters from the opposite direction as the blocks.
    // Outbound bloom filters go with Inbound XThins and vice versa.
    uint64_t nBloomFilterSize = 0;
    for (const auto &mi : mapBloomFilters)
    {
        nBloomFilterSize += mi.second;
    }

    if (nOriginalSizeTotal > 0)
        nCompressionRate = 100 - (100 * (double)(nThinSizeTotal + nBloomFilterSize) / nOriginalSizeTotal);

    return nCompressionRate;
}

double CThinBlockData::compute24hInboundRerequestTxPercentInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_thinblockstats)
{
    AssertLockHeld(cs_thinblockstats);

    expireStats(mapThinBlocksInBoundReRequestedTx);
    expireStats(mapThinBlocksInBound);

    double nReRequestRate = 0;
    uint64_t nTotalReRequests = 0;
    uint64_t nTotalReRequestedTxs = 0;
    for (const auto &mi : mapThinBlocksInBoundReRequestedTx)
    {
        nTotalReRequests += 1;
        nTotalReRequestedTxs += mi.second;
    }

    if (mapThinBlocksInBound.size() > 0)
        nReRequestRate = 100 * (double)nTotalReRequests / mapThinBlocksInBound.size();

    return nReRequestRate;
}

void CThinBlockData::UpdateInBound(uint64_t nThinBlockSize, uint64_t nOriginalBlockSize)
{
    LOCK(cs_thinblockstats);
    // Update InBound thinblock tracking information
    nOriginalSize += nOriginalBlockSize;
    nThinSize += nThinBlockSize;
    nInBoundBlocks += 1;
    updateStats(mapThinBlocksInBound, std::pair<uint64_t, uint64_t>(nThinBlockSize, nOriginalBlockSize));
}

void CThinBlockData::UpdateOutBound(uint64_t nThinBlockSize, uint64_t nOriginalBlockSize)
{
    LOCK(cs_thinblockstats);
    nOriginalSize += nOriginalBlockSize;
    nThinSize += nThinBlockSize;
    nOutBoundBlocks += 1;
    updateStats(mapThinBlocksOutBound, std::pair<uint64_t, uint64_t>(nThinBlockSize, nOriginalBlockSize));
}

void CThinBlockData::UpdateOutBoundBloomFilter(uint64_t nBloomFilterSize)
{
    LOCK(cs_thinblockstats);
    nTotalBloomFilterBytes += nBloomFilterSize;
    updateStats(mapBloomFiltersOutBound, nBloomFilterSize);
}

void CThinBlockData::UpdateInBoundBloomFilter(uint64_t nBloomFilterSize)
{
    LOCK(cs_thinblockstats);
    nTotalBloomFilterBytes += nBloomFilterSize;
    updateStats(mapBloomFiltersInBound, nBloomFilterSize);
}

void CThinBlockData::UpdateResponseTime(double nResponseTime)
{
    LOCK(cs_thinblockstats);

    // only update stats if IBD is complete
    if (IsChainNearlySyncd() && IsThinBlocksEnabled())
    {
        updateStats(mapThinBlockResponseTime, nResponseTime);
    }
}

void CThinBlockData::UpdateValidationTime(double nValidationTime)
{
    LOCK(cs_thinblockstats);

    // only update stats if IBD is complete
    if (IsChainNearlySyncd() && IsThinBlocksEnabled())
    {
        updateStats(mapThinBlockValidationTime, nValidationTime);
    }
}

void CThinBlockData::UpdateInBoundReRequestedTx(int nReRequestedTx)
{
    LOCK(cs_thinblockstats);

    // Update InBound thinblock tracking information
    updateStats(mapThinBlocksInBoundReRequestedTx, nReRequestedTx);
}

void CThinBlockData::UpdateMempoolLimiterBytesSaved(unsigned int nBytesSaved)
{
    LOCK(cs_thinblockstats);
    nMempoolLimiterBytesSaved += nBytesSaved;
}

void CThinBlockData::UpdateThinBlock(uint64_t nThinBlockSize)
{
    LOCK(cs_thinblockstats);
    nTotalThinBlockBytes += nThinBlockSize;
    updateStats(mapThinBlock, nThinBlockSize);
}

void CThinBlockData::UpdateFullTx(uint64_t nFullTxSize)
{
    LOCK(cs_thinblockstats);
    nTotalThinBlockBytes += nFullTxSize;
    updateStats(mapFullTx, nFullTxSize);
}

std::string CThinBlockData::ToString()
{
    LOCK(cs_thinblockstats);
    double size = computeTotalBandwidthSavingsInternal();
    std::ostringstream ss;
    ss << nInBoundBlocks() << " inbound and " << nOutBoundBlocks() << " outbound thin blocks have saved "
       << formatInfoUnit(size) << " of bandwidth";
    return ss.str();
}

// Calculate the xthin percentage compression over the last 24 hours for inbound blocks
std::string CThinBlockData::InBoundPercentToString()
{
    LOCK(cs_thinblockstats);

    double nCompressionRate = compute24hAverageCompressionInternal(mapThinBlocksInBound, mapBloomFiltersOutBound);

    // NOTE: Potential gotcha, compute24hAverageCompressionInternal has a side-effect of calling
    //       expireStats which modifies the contents of mapThinBlocksInBound
    // We currently rely on this side-effect for the string produced below
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Compression for " << mapThinBlocksInBound.size() << " Inbound  thinblocks (last 24hrs): " << nCompressionRate
       << "%";
    return ss.str();
}

// Calculate the xthin percentage compression over the last 24 hours for outbound blocks
std::string CThinBlockData::OutBoundPercentToString()
{
    LOCK(cs_thinblockstats);

    double nCompressionRate = compute24hAverageCompressionInternal(mapThinBlocksOutBound, mapBloomFiltersInBound);

    // NOTE: Potential gotcha, compute24hAverageCompressionInternal has a side-effect of calling
    //       expireStats which modifies the contents of mapThinBlocksOutBound
    // We currently rely on this side-effect for the string produced below
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Compression for " << mapThinBlocksOutBound.size()
       << " Outbound thinblocks (last 24hrs): " << nCompressionRate << "%";
    return ss.str();
}

// Calculate the average inbound xthin bloom filter size
std::string CThinBlockData::InBoundBloomFiltersToString()
{
    LOCK(cs_thinblockstats);
    double avgBloomSize = average(mapBloomFiltersInBound);
    std::ostringstream ss;
    ss << "Inbound bloom filter size (last 24hrs) AVG: " << formatInfoUnit(avgBloomSize);
    return ss.str();
}

// Calculate the average inbound xthin bloom filter size
std::string CThinBlockData::OutBoundBloomFiltersToString()
{
    LOCK(cs_thinblockstats);
    double avgBloomSize = average(mapBloomFiltersOutBound);
    std::ostringstream ss;
    ss << "Outbound bloom filter size (last 24hrs) AVG: " << formatInfoUnit(avgBloomSize);
    return ss.str();
}
// Calculate the xthin average response time over the last 24 hours
std::string CThinBlockData::ResponseTimeToString()
{
    LOCK(cs_thinblockstats);

    expireStats(mapThinBlockResponseTime);

    std::vector<double> vResponseTime;

    double nResponseTimeAverage = 0;
    double nPercentile = 0;
    double nTotalResponseTime = 0;
    double nTotalEntries = 0;
    for (const auto &mi : mapThinBlockResponseTime)
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
std::string CThinBlockData::ValidationTimeToString()
{
    LOCK(cs_thinblockstats);

    expireStats(mapThinBlockValidationTime);

    std::vector<double> vValidationTime;

    double nValidationTimeAverage = 0;
    double nPercentile = 0;
    double nTotalValidationTime = 0;
    double nTotalEntries = 0;
    for (const auto &mi : mapThinBlockValidationTime)
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
std::string CThinBlockData::ReRequestedTxToString()
{
    LOCK(cs_thinblockstats);

    double nReRequestRate = compute24hInboundRerequestTxPercentInternal();

    // NOTE: Potential gotcha, compute24hInboundRerequestTxPercentInternal has a side-effect of calling
    //       expireStats which modifies the contents of mapThinBlocksInBoundReRequestedTx
    // We currently rely on this side-effect for the string produced below
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Tx re-request rate (last 24hrs): " << nReRequestRate
       << "% Total re-requests:" << mapThinBlocksInBoundReRequestedTx.size();
    return ss.str();
}

std::string CThinBlockData::MempoolLimiterBytesSavedToString()
{
    LOCK(cs_thinblockstats);
    double size = (double)nMempoolLimiterBytesSaved();
    std::ostringstream ss;
    ss << "Thinblock mempool limiting has saved " << formatInfoUnit(size) << " of bandwidth";
    return ss.str();
}

// Calculate the average xthin block size
std::string CThinBlockData::ThinBlockToString()
{
    LOCK(cs_thinblockstats);
    double avgThinBlockSize = average(mapThinBlock);
    std::ostringstream ss;
    ss << "Thinblock size (last 24hrs) AVG: " << formatInfoUnit(avgThinBlockSize);
    return ss.str();
}

// Calculate the average size of all full txs sent with block
std::string CThinBlockData::FullTxToString()
{
    LOCK(cs_thinblockstats);
    double avgFullTxSize = average(mapFullTx);
    std::ostringstream ss;
    ss << "Thinblock full transactions size (last 24hrs) AVG: " << formatInfoUnit(avgFullTxSize);
    return ss.str();
}

// After a thinblock is finished processing or if for some reason we have to pre-empt the rebuilding
// of a thinblock then we clear out the thinblock data which can be substantial.
void CThinBlockData::ClearThinBlockData(CNode *pnode)
{
    LOCK(pnode->cs_xthinblock);

    // Remove bytes from counter
    thindata.DeleteThinBlockBytes(pnode->nLocalThinBlockBytes, pnode);
    pnode->nLocalThinBlockBytes = 0;

    // Clear out thinblock data we no longer need
    pnode->thinBlockWaitingForTxns = -1;
    pnode->thinBlock.SetNull();
    pnode->xThinBlockHashes.clear();
    pnode->thinBlockHashes.clear();
    pnode->mapMissingTx.clear();

    LOG(THIN, "Total in memory thinblockbytes size after clearing a thinblock is %ld bytes\n",
        thindata.GetThinBlockBytes());
}

void CThinBlockData::ClearThinBlockData(CNode *pnode, const uint256 &hash)
{
    // We must make sure to clear the thinblock data first before clearing the thinblock in flight.
    ClearThinBlockData(pnode);
    thinrelay.ClearBlockInFlight(pnode, hash);
}

void CThinBlockData::ClearThinBlockStats()
{
    LOCK(cs_thinblockstats);

    nOriginalSize.Clear();
    nThinSize.Clear();
    nInBoundBlocks.Clear();
    nOutBoundBlocks.Clear();
    nMempoolLimiterBytesSaved.Clear();
    nTotalBloomFilterBytes.Clear();
    nTotalThinBlockBytes.Clear();
    nTotalFullTxBytes.Clear();

    mapThinBlocksInBound.clear();
    mapThinBlocksOutBound.clear();
    mapBloomFiltersOutBound.clear();
    mapBloomFiltersInBound.clear();
    mapThinBlockResponseTime.clear();
    mapThinBlockValidationTime.clear();
    mapThinBlocksInBoundReRequestedTx.clear();
    mapThinBlock.clear();
    mapFullTx.clear();
}

uint64_t CThinBlockData::AddThinBlockBytes(uint64_t bytes, CNode *pfrom)
{
    pfrom->nLocalThinBlockBytes += bytes;
    uint64_t ret = nThinBlockBytes.fetch_add(bytes) + bytes;

    return ret;
}

void CThinBlockData::DeleteThinBlockBytes(uint64_t bytes, CNode *pfrom)
{
    if (bytes <= pfrom->nLocalThinBlockBytes)
        pfrom->nLocalThinBlockBytes -= bytes;

    if (bytes <= nThinBlockBytes)
    {
        nThinBlockBytes.fetch_sub(bytes);
    }
}

void CThinBlockData::ResetThinBlockBytes() { nThinBlockBytes.store(0); }
uint64_t CThinBlockData::GetThinBlockBytes() { return nThinBlockBytes.load(); }
void CThinBlockData::FillThinBlockQuickStats(ThinBlockQuickStats &stats)
{
    if (!IsThinBlocksEnabled())
        return;

    LOCK(cs_thinblockstats);

    stats.nTotalInbound = nInBoundBlocks();
    stats.nTotalOutbound = nOutBoundBlocks();
    stats.nTotalBandwidthSavings = computeTotalBandwidthSavingsInternal();

    // NOTE: The following calls rely on the side-effect of the compute*Internal
    //       calls also calling expireStats on the associated statistics maps
    //       This is why we set the % value first, then the count second for compression values
    stats.fLast24hInboundCompression =
        compute24hAverageCompressionInternal(mapThinBlocksInBound, mapBloomFiltersOutBound);
    stats.nLast24hInbound = mapThinBlocksInBound.size();
    stats.fLast24hOutboundCompression =
        compute24hAverageCompressionInternal(mapThinBlocksOutBound, mapBloomFiltersInBound);
    stats.nLast24hOutbound = mapThinBlocksOutBound.size();
    stats.fLast24hRerequestTxPercent = compute24hInboundRerequestTxPercentInternal();
    stats.nLast24hRerequestTx = mapThinBlocksInBoundReRequestedTx.size();
}

bool IsThinBlocksEnabled() { return GetBoolArg("-use-thinblocks", true); }
bool ClearLargestThinBlockAndDisconnect(CNode *pfrom)
{
    CNode *pLargest = nullptr;
    LOCK(cs_vNodes);
    for (CNode *pnode : vNodes)
    {
        if ((pLargest == nullptr) || (pnode->nLocalThinBlockBytes > pLargest->nLocalThinBlockBytes))
            pLargest = pnode;
    }
    if (pLargest != nullptr)
    {
        thindata.ClearThinBlockData(pLargest, pLargest->thinBlock.GetBlockHeader().GetHash());
        pLargest->fDisconnect = true;

        // If the our node is currently using up the most thinblock bytes then return true so that we
        // can stop processing this thinblock and let the disconnection happen.
        if (pfrom == pLargest)
            return true;
    }

    return false;
}

void SendXThinBlock(ConstCBlockRef pblock, CNode *pfrom, const CInv &inv)
{
    if (inv.type == MSG_XTHINBLOCK)
    {
        CXThinBlock xThinBlock;
        {
            LOCK(pfrom->cs_filter);
            xThinBlock = CXThinBlock(*pblock, pfrom->pThinBlockFilter);
        }
        int nSizeBlock = pblock->GetBlockSize();
        // If there is a cheapHash collision in this block then send a normal thinblock
        if (xThinBlock.collision == true)
        {
            CThinBlock thinBlock;
            {
                LOCK(pfrom->cs_filter);
                thinBlock = CThinBlock(*pblock, *pfrom->pThinBlockFilter);
            }
            int nSizeThinBlock = ::GetSerializeSize(thinBlock, SER_NETWORK, PROTOCOL_VERSION);
            if (nSizeThinBlock < nSizeBlock)
            {
                pfrom->PushMessage(NetMsgType::THINBLOCK, thinBlock);
                thindata.UpdateOutBound(nSizeThinBlock, nSizeBlock);
                LOG(THIN, "TX HASH COLLISION: Sent thinblock - size: %d vs block size: %d => tx hashes: %d "
                          "transactions: %d  peer: %s\n",
                    nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.vMissingTx.size(),
                    pfrom->GetLogName());
            }
            else
            {
                pfrom->PushMessage(NetMsgType::BLOCK, *pblock);
                LOG(THIN, "Sent regular block instead - xthinblock size: %d vs block size: %d => tx hashes: %d "
                          "transactions: %d  peer: %s\n",
                    nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.vMissingTx.size(),
                    pfrom->GetLogName());
            }
        }
        else // Send an xThinblock
        {
            // Only send a thinblock if smaller than a regular block
            int nSizeThinBlock = ::GetSerializeSize(xThinBlock, SER_NETWORK, PROTOCOL_VERSION);
            if (nSizeThinBlock < nSizeBlock)
            {
                thindata.UpdateOutBound(nSizeThinBlock, nSizeBlock);
                pfrom->PushMessage(NetMsgType::XTHINBLOCK, xThinBlock);
                LOG(THIN, "Sent xthinblock - size: %d vs block size: %d => tx hashes: %d transactions: %d peer: %s\n",
                    nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.vMissingTx.size(),
                    pfrom->GetLogName());
                thindata.UpdateThinBlock(nSizeThinBlock);
                thindata.UpdateFullTx(::GetSerializeSize(xThinBlock.vMissingTx, SER_NETWORK, PROTOCOL_VERSION));
            }
            else
            {
                pfrom->PushMessage(NetMsgType::BLOCK, *pblock);
                LOG(THIN, "Sent regular block instead - xthinblock size: %d vs block size: %d => tx hashes: %d "
                          "transactions: %d  peer: %s\n",
                    nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.vMissingTx.size(),
                    pfrom->GetLogName());
            }
        }
    }
    else if (inv.type == MSG_THINBLOCK)
    {
        CThinBlock thinBlock;
        {
            LOCK(pfrom->cs_filter);
            thinBlock = CThinBlock(*pblock, *pfrom->pThinBlockFilter);
        }
        int nSizeBlock = pblock->GetBlockSize();
        int nSizeThinBlock = ::GetSerializeSize(thinBlock, SER_NETWORK, PROTOCOL_VERSION);
        if (nSizeThinBlock < nSizeBlock)
        { // Only send a thinblock if smaller than a regular block
            thindata.UpdateOutBound(nSizeThinBlock, nSizeBlock);
            pfrom->PushMessage(NetMsgType::THINBLOCK, thinBlock);
            LOG(THIN, "Sent thinblock - size: %d vs block size: %d => tx hashes: %d transactions: %d  peer: %s\n",
                nSizeThinBlock, nSizeBlock, thinBlock.vTxHashes.size(), thinBlock.vMissingTx.size(),
                pfrom->GetLogName());
        }
        else
        {
            pfrom->PushMessage(NetMsgType::BLOCK, *pblock);
            LOG(THIN, "Sent regular block instead - thinblock size: %d vs block size: %d => tx hashes: %d "
                      "transactions: %d  peer: %s\n",
                nSizeThinBlock, nSizeBlock, thinBlock.vTxHashes.size(), thinBlock.vMissingTx.size(),
                pfrom->GetLogName());
        }
    }
    else
    {
        dosMan.Misbehaving(pfrom, 100);
        return;
    }
    pfrom->blocksSent += 1;
}

void RequestThinBlock(CNode *pfrom, const uint256 &hash)
{
    CInv inv(MSG_THINBLOCK, hash);
    if (pfrom->xVersion.as_u64c(XVer::BU_XTHIN_VERSION) >= 2)
    {
        pfrom->PushMessage(NetMsgType::GET_THIN, inv);
    }
    else
    {
        pfrom->PushMessage(NetMsgType::GETDATA, inv);
    }
}

bool IsThinBlockValid(CNode *pfrom, const std::vector<CTransaction> &vMissingTx, const CBlockHeader &header)
{
    // Check that that there is at least one txn in the xthin and that the first txn is the coinbase
    if (vMissingTx.empty())
    {
        return error("No Transactions found in thinblock or xthinblock %s from peer %s", header.GetHash().ToString(),
            pfrom->GetLogName());
    }
    if (!vMissingTx[0].IsCoinBase())
    {
        return error("First txn is not coinbase for thinblock or xthinblock %s from peer %s",
            header.GetHash().ToString(), pfrom->GetLogName());
    }

    // check block header
    CValidationState state;
    if (!CheckBlockHeader(header, state, true))
    {
        return error("Received invalid header for thinblock or xthinblock %s from peer %s", header.GetHash().ToString(),
            pfrom->GetLogName());
    }
    if (state.Invalid())
    {
        return error("Received invalid header for thinblock or xthinblock %s from peer %s", header.GetHash().ToString(),
            pfrom->GetLogName());
    }

    return true;
}

void BuildSeededBloomFilter(CBloomFilter &filterMemPool,
    std::vector<uint256> &vOrphanHashes,
    uint256 hash,
    CNode *pfrom,
    bool fDeterministic)
{
    int64_t nStartTimer = GetTimeMillis();
    FastRandomContext insecure_rand(fDeterministic);
    std::set<uint256> setHighScoreMemPoolHashes;
    std::set<uint256> setPriorityMemPoolHashes;

    // When bloom filter targeting is turned on we try to limit the number of hashes we add to the bloom
    // filter by approximately determining which transasctions are most likely to be mined in the next block.
    //
    // This helps to keep the size of the bloom filter down to a minimum however it also incurrs a small
    // performance hit and therefore it is not done unless the memepool size is larger than the excessive
    // block size, since there is no benefit to targeting if the blocks are likely big enough to clear the mempool.
    if (GetBoolArg("-use-bloom-filter-targeting", DEFAULT_BLOOM_FILTER_TARGETING) &&
        excessiveBlockSize < mempool.GetTotalTxSize())
    {
        // How much of the block should be dedicated to high-priority transactions.
        // Logically this should be the same size as the DEFAULT_BLOCK_PRIORITY_SIZE however,
        // we can't be sure that a miner won't decide to mine more high priority txs and therefore
        // by including a full blocks worth of high priority tx's we cover every scenario.  And when we
        // go on to add the high fee tx's there will be an intersection between the two which then makes
        // the total number of tx's that go into the bloom filter smaller than just the sum of the two.
        uint64_t nBlockPrioritySize = excessiveBlockSize * 1.5;

        // Largest projected block size used to add the high fee transactions.  We multiply it by an
        // additional factor to take into account that miners may have slighty different policies when selecting
        // high fee tx's from the pool.
        uint64_t nBlockMaxProjectedSize = excessiveBlockSize * 1.5;

        std::vector<TxCoinAgePriority> vPriority;
        TxCoinAgePriorityCompare pricomparer;

        uint64_t nMapTxSize = 0;
        {
            READLOCK(mempool.cs);
            nMapTxSize = mempool.mapTx.size();
        }

        if (nMapTxSize > 0)
        {
            int nHeight = 0;
            int64_t nMedianTimePast = 0;
            int64_t nLockTimeCutoff = 0;
            {
                LOCK(cs_main);
                CBlockIndex *pindexPrev = chainActive.Tip();
                nHeight = pindexPrev->nHeight + 1;
                nMedianTimePast = pindexPrev->GetMedianTimePast();

                nLockTimeCutoff =
                    (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST) ? nMedianTimePast : GetAdjustedTime();
            }

            READLOCK(mempool.cs);

            // Create a sorted list of transactions and their updated priorities.  This will be used to fill
            // the mempoolhashes with the expected priority area of the next block.  We will multiply this by
            // a factor of ? to account for any differences between the "Miners".
            vPriority.reserve(mempool.mapTx.size());
            for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end();
                 mi++)
            {
                double dPriority = mi->GetPriority(nHeight);
                CAmount dummy;
                mempool._ApplyDeltas(mi->GetTx().GetHash(), dPriority, dummy);
                vPriority.push_back(TxCoinAgePriority(dPriority, mi));
            }
            make_heap(vPriority.begin(), vPriority.end(), pricomparer);

            uint64_t nPrioritySize = 0;
            CTxMemPool::txiter iter;
            for (uint64_t i = 0; i < vPriority.size(); i++)
            {
                nPrioritySize += vPriority[i].second->GetTxSize();
                if (nPrioritySize > nBlockPrioritySize)
                    break;
                setPriorityMemPoolHashes.insert(vPriority[i].second->GetTx().GetHash());

                // Add children.  We don't need to look for parents here since they will all be parents.
                iter = mempool.mapTx.project<0>(vPriority[i].second);
                for (CTxMemPool::txiter child : mempool.GetMemPoolChildren(iter))
                {
                    uint256 childHash = child->GetTx().GetHash();
                    if (!setPriorityMemPoolHashes.count(childHash))
                    {
                        setPriorityMemPoolHashes.insert(childHash);
                        nPrioritySize += child->GetTxSize();
                        LOG(BLOOM,
                            "add priority child %s with fee %d modified fee %d size %d clearatentry %d priority %f\n",
                            child->GetTx().GetHash().ToString(), child->GetFee(), child->GetModifiedFee(),
                            child->GetTxSize(), child->WasClearAtEntry(), child->GetPriority(nHeight));
                    }
                }
            }

            // Create a list of high score transactions. We will multiply this by
            // a factor of ? to account for any differences between the way Miners include tx's
            CTxMemPool::indexed_transaction_set::nth_index<3>::type::iterator mi = mempool.mapTx.get<3>().begin();
            uint64_t nBlockSize = 0;
            while (mi != mempool.mapTx.get<3>().end())
            {
                const CTransactionRef &tx = mi->GetSharedTx();
                const uint256 &txHash = tx->GetHash();

                if (!IsFinalTx(tx, nHeight, nLockTimeCutoff))
                {
                    LOG(BLOOM, "tx %s is not final\n", txHash.ToString());
                    mi++;
                    continue;
                }

                // If this tx is not accounted for already in the priority set then continue and add
                // it to the high score set if it can be and also add any parents or children.  Also add
                // children and parents to the priority set tx's if they have any.
                iter = mempool.mapTx.project<0>(mi);
                if (!setHighScoreMemPoolHashes.count(txHash))
                {
                    LOG(BLOOM,
                        "next tx is %s blocksize %d fee %d modified fee %d size %d clearatentry %d priority %f\n",
                        mi->GetTx().GetHash().ToString(), nBlockSize, mi->GetFee(), mi->GetModifiedFee(),
                        mi->GetTxSize(), mi->WasClearAtEntry(), mi->GetPriority(nHeight));

                    // add tx to the set: we don't know if this is a parent or child yet.
                    setHighScoreMemPoolHashes.insert(txHash);

                    // Add any parent tx's
                    bool fChild = false;
                    for (CTxMemPool::txiter parent : mempool.GetMemPoolParents(iter))
                    {
                        fChild = true;
                        const uint256 &parentHash = parent->GetTx().GetHash();
                        if (!setHighScoreMemPoolHashes.count(parentHash))
                        {
                            setHighScoreMemPoolHashes.insert(parentHash);
                            LOG(BLOOM, "add high score parent %s with blocksize %d fee %d modified fee %d size "
                                       "%d clearatentry %d priority %f\n",
                                parent->GetTx().GetHash().ToString(), nBlockSize, parent->GetFee(),
                                parent->GetModifiedFee(), parent->GetTxSize(), parent->WasClearAtEntry(),
                                parent->GetPriority(nHeight));
                        }
                    }

                    // Now add any children tx's.
                    bool fHasChildren = false;
                    for (CTxMemPool::txiter child : mempool.GetMemPoolChildren(iter))
                    {
                        fHasChildren = true;
                        const uint256 &childHash = child->GetTx().GetHash();
                        if (!setHighScoreMemPoolHashes.count(childHash))
                        {
                            setHighScoreMemPoolHashes.insert(childHash);
                            LOG(BLOOM, "add high score child %s with blocksize %d fee %d modified fee %d size "
                                       "%d clearatentry %d priority %f\n",
                                child->GetTx().GetHash().ToString(), nBlockSize, child->GetFee(),
                                child->GetModifiedFee(), child->GetTxSize(), child->WasClearAtEntry(),
                                child->GetPriority(nHeight));
                        }
                    }

                    // If a tx with no parents and no children, then we increment this block size.
                    // We don't want to add parents and children to the size because for tx's with many children, miners
                    // may not mine them
                    // as they are not as profitable but we still have to add their hash to the bloom filter in case
                    // they do.
                    if (!fChild && !fHasChildren)
                        nBlockSize += mi->GetTxSize();
                }

                if (nBlockSize > nBlockMaxProjectedSize)
                    break;

                mi++;
            }
        }
    }
    else
    {
        std::vector<uint256> vMempoolHashes;

        // Add all the transaction hashes currently in the mempool
        mempool.queryHashes(vMempoolHashes);
        setHighScoreMemPoolHashes.insert(vMempoolHashes.begin(), vMempoolHashes.end());
    }

    // Also add all the transaction hashes currently in the txCommitQ
    {
        boost::unique_lock<boost::mutex> lock(csCommitQ);
        for (auto &it : *txCommitQ)
        {
            setHighScoreMemPoolHashes.insert(it.first);
        }
    }

    LOG(THIN, "Bloom Filter Targeting completed in:%d (ms)\n", GetTimeMillis() - nStartTimer);
    nStartTimer = GetTimeMillis(); // reset the timer

    // We set the beginning of our growth algortithm to the time we request our first xthin.  We do this here
    // rather than setting up a global variable in init.cpp.  This has more to do with potential merge conflicts
    // with BU than any other technical reason.
    static int64_t nStartGrowth = GetTime();

    // Tuning knobs for the false positive growth algorithm
    static uint8_t nHoursToGrow = 12; // number of hours until maximum growth for false positive rate
    // use for nMinFalsePositive = 0.0001 and nMaxFalsePositive = 0.01 for
    // static double nGrowthCoefficient = 0.7676;
    // 6 hour growth period
    // use for nMinFalsePositive = 0.0001 and nMaxFalsePositive = 0.02 for
    // static double nGrowthCoefficient = 0.8831;
    // 6 hour growth period
    // use for nMinFalsePositive = 0.0001 and nMaxFalsePositive = 0.01 for
    // static double nGrowthCoefficient = 0.1921;
    // 24 hour growth period
    static double nGrowthCoefficient =
        0.0544; // use for nMinFalsePositive = 0.0001 and nMaxFalsePositive = 0.005 for 72 hour growth period
    static double nMinFalsePositive = 0.0001; // starting value for false positive
    static double nMaxFalsePositive = 0.005; // maximum false positive rate at end of decay
    // TODO: automatically calculate the nGrowthCoefficient from nHoursToGrow, nMinFalsePositve and nMaxFalsePositive

    // Count up all the transactions that we'll be putting into the filter, removing any duplicates
    for (const uint256 &txHash : setHighScoreMemPoolHashes)
    {
        if (setPriorityMemPoolHashes.count(txHash))
            setPriorityMemPoolHashes.erase(txHash);
    }

    unsigned int nSelectedTxHashes =
        setHighScoreMemPoolHashes.size() + vOrphanHashes.size() + setPriorityMemPoolHashes.size();
    unsigned int nElements =
        std::max(nSelectedTxHashes, (unsigned int)1); // Must make sure nElements is greater than zero or will assert

    // Calculate the new False Positive rate.
    // We increase the false positive rate as time increases, starting at nMinFalsePositive and with growth governed by
    // nGrowthCoefficient,
    // using the simple exponential growth function as follows:
    // y = (starting or minimum fprate: nMinFalsePositive) * e ^ (time in hours from start * nGrowthCoefficient)
    int64_t nTimePassed = GetTime() - nStartGrowth;
    double nFPRate = nMinFalsePositive * exp(((double)(nTimePassed) / 3600) * nGrowthCoefficient);
    if (nTimePassed > nHoursToGrow * 3600)
        nFPRate = nMaxFalsePositive;

    uint32_t nMaxFilterSize = std::max(SMALLEST_MAX_BLOOM_FILTER_SIZE, pfrom->nXthinBloomfilterSize);
    filterMemPool = CBloomFilter(nElements, nFPRate, insecure_rand.rand32(), BLOOM_UPDATE_ALL, nMaxFilterSize);
    LOG(THIN, "FPrate: %f Num elements in bloom filter:%d high priority txs:%d high fee txs:%d orphans:%d total "
              "txs in mempool:%d\n",
        nFPRate, nElements, setPriorityMemPoolHashes.size(), setHighScoreMemPoolHashes.size(), vOrphanHashes.size(),
        mempool.mapTx.size());

    // Add the selected tx hashes to the bloom filter
    for (const uint256 &txHash : setPriorityMemPoolHashes)
        filterMemPool.insert(txHash);
    for (const uint256 &txHash : setHighScoreMemPoolHashes)
        filterMemPool.insert(txHash);
    for (const uint256 &txHash : vOrphanHashes)
        filterMemPool.insert(txHash);
    uint64_t nSizeFilter = ::GetSerializeSize(filterMemPool, SER_NETWORK, PROTOCOL_VERSION);
    LOG(THIN, "Created bloom filter: %d bytes for block: %s in:%d (ms)\n", nSizeFilter, hash.ToString(),
        GetTimeMillis() - nStartTimer);
    thindata.UpdateOutBoundBloomFilter(nSizeFilter);
}
