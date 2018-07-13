// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "graphene.h"
#include "chainparams.h"
#include "connmgr.h"
#include "consensus/merkle.h"
#include "dosman.h"
#include "expedited.h"
#include "net.h"
#include "parallel.h"
#include "policy/policy.h"
#include "pow.h"
#include "requestManager.h"
#include "thinblock.h"
#include "timedata.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "util.h"
#include "utiltime.h"

static bool ReconstructBlock(CNode *pfrom, const bool fXVal, int &missingCount, int &unnecessaryCount);

CMemPoolInfo::CMemPoolInfo(uint64_t nTx) { this->nTx = nTx; }
CMemPoolInfo::CMemPoolInfo() { this->nTx = 0; }
CGrapheneBlock::CGrapheneBlock(const CBlockRef pblock, uint64_t nReceiverMemPoolTx)
{
    header = pblock->GetBlockHeader();
    nBlockTxs = pblock->vtx.size();

    std::vector<uint256> blockHashes;
    for (auto &tx : pblock->vtx)
    {
        blockHashes.push_back(tx->GetHash());

        if (tx->IsCoinBase())
            vAdditionalTxs.push_back(tx);
    }

    pGrapheneSet = new CGrapheneSet(nReceiverMemPoolTx, blockHashes, true);
}

CGrapheneBlock::~CGrapheneBlock()
{
    if (pGrapheneSet)
    {
        delete pGrapheneSet;
        pGrapheneSet = nullptr;
    }
}

CGrapheneBlockTx::CGrapheneBlockTx(uint256 blockHash, std::vector<CTransaction> &vTx)
{
    blockhash = blockHash;
    vMissingTx = vTx;
}

bool CGrapheneBlockTx::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    if (!pfrom->GrapheneCapable())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("Graphene block tx message received from a non GRAPHENE node, peer=%s", pfrom->GetLogName());
    }

    std::string strCommand = NetMsgType::GRAPHENETX;
    size_t msgSize = vRecv.size();
    CGrapheneBlockTx grapheneBlockTx;
    vRecv >> grapheneBlockTx;

    // Message consistency checking
    CInv inv(MSG_GRAPHENEBLOCK, grapheneBlockTx.blockhash);
    if (grapheneBlockTx.vMissingTx.empty() || grapheneBlockTx.blockhash.IsNull())
    {
        graphenedata.ClearGrapheneBlockData(pfrom, inv.hash);

        dosMan.Misbehaving(pfrom, 100);
        return error("Incorrectly constructed grblocktx or inconsistent graphene block data received.  Banning peer=%s",
            pfrom->GetLogName());
    }

    LOG(GRAPHENE, "Received grblocktx for %s peer=%s\n", inv.hash.ToString(), pfrom->GetLogName());
    {
        // Do not process unrequested grblocktx unless from an expedited node.
        LOCK(pfrom->cs_mapgrapheneblocksinflight);
        if (!pfrom->mapGrapheneBlocksInFlight.count(inv.hash) && !connmgr->IsExpeditedUpstream(pfrom))
        {
            dosMan.Misbehaving(pfrom, 10);
            return error(
                "Received grblocktx %s from peer %s but was unrequested", inv.hash.ToString(), pfrom->GetLogName());
        }
    }

    // Check if we've already received this block and have it on disk
    bool fAlreadyHave = false;
    {
        LOCK(cs_main);
        fAlreadyHave = AlreadyHaveBlock(inv);
    }
    if (fAlreadyHave)
    {
        requester.AlreadyReceived(inv);
        graphenedata.ClearGrapheneBlockData(pfrom, inv.hash);

        LOG(GRAPHENE, "Received grblocktx but returning because we already have this block %s on disk, peer=%s\n",
            inv.hash.ToString(), pfrom->GetLogName());
        return true;
    }

    for (const CTransaction &tx : grapheneBlockTx.vMissingTx)
    {
        pfrom->mapMissingTx[tx.GetHash().GetCheapHash()] = MakeTransactionRef(tx);

        uint256 hash = tx.GetHash();
        uint64_t cheapHash = hash.GetCheapHash();
        pfrom->grapheneBlockHashes[pfrom->grapheneMapHashOrderIndex[cheapHash]] = hash;
    }

    LOG(GRAPHENE, "Got %d Re-requested txs from peer=%s\n", grapheneBlockTx.vMissingTx.size(), pfrom->GetLogName());

    // At this point we should have all the full hashes in the block. Check that the merkle
    // root in the block header matches the merkel root calculated from the hashes provided.
    bool mutated;
    uint256 merkleroot = ComputeMerkleRoot(pfrom->grapheneBlockHashes, &mutated);
    if (pfrom->grapheneBlock.hashMerkleRoot != merkleroot || mutated)
    {
        graphenedata.ClearGrapheneBlockData(pfrom, inv.hash);

        dosMan.Misbehaving(pfrom, 100);
        return error("Merkle root for %s does not match computed merkle root, peer=%s", inv.hash.ToString(),
            pfrom->GetLogName());
    }
    LOG(GRAPHENE, "Merkle Root check passed for %s peer=%s\n", inv.hash.ToString(), pfrom->GetLogName());

    // Xpress Validation - only perform xval if the chaintip matches the last blockhash in the graphene block
    bool fXVal;
    {
        LOCK(cs_main);
        fXVal = (pfrom->grapheneBlock.hashPrevBlock == chainActive.Tip()->GetBlockHash()) ? true : false;
    }

    int missingCount = 0;
    int unnecessaryCount = 0;
    // Look for each transaction in our various pools and buffers.
    // With grapheneBlocks the vTxHashes contains only the first 8 bytes of the tx hash.
    {
        LOCK2(orphanpool.cs, cs_xval);
        if (!ReconstructBlock(pfrom, fXVal, missingCount, unnecessaryCount))
            return false;
    }

    // If we're still missing transactions then bail out and request the failover block. This should never
    // happen unless we're under some kind of attack or somehow we lost transactions out of our memory pool
    // while we were retreiving missing transactions.
    if (missingCount > 0)
    {
        // Since we can't process this graphene block then clear out the data from memory
        graphenedata.ClearGrapheneBlockData(pfrom, inv.hash);

        RequestFailoverBlock(pfrom, grapheneBlockTx.blockhash);
        return error("Still missing transactions after reconstructing block, peer=%s: re-requesting failover block",
            pfrom->GetLogName());
    }
    else
    {
        // We have all the transactions now that are in this block: try to reassemble and process.
        CInv inv(CInv(MSG_BLOCK, grapheneBlockTx.blockhash));

        // for compression statistics, we have to add up the size of grapheneblock and the re-requested grapheneBlockTx.
        int nSizeGrapheneBlockTx = msgSize;
        int blockSize = ::GetSerializeSize(pfrom->grapheneBlock, SER_NETWORK, CBlock::CURRENT_VERSION);
        LOG(GRAPHENE, "Reassembled grblktx for %s (%d bytes). Message was %d bytes (graphene block) and %d bytes "
                      "(re-requested tx), compression ratio %3.2f, peer=%s\n",
            pfrom->grapheneBlock.GetHash().ToString(), blockSize, pfrom->nSizeGrapheneBlock, nSizeGrapheneBlockTx,
            ((float)blockSize) / ((float)pfrom->nSizeGrapheneBlock + (float)nSizeGrapheneBlockTx), pfrom->GetLogName());

        // Update run-time statistics of graphene block bandwidth savings.
        // We add the original graphene block size with the size of transactions that were re-requested.
        // This is NOT double counting since we never accounted for the original graphene block due to the re-request.
        graphenedata.UpdateInBound(nSizeGrapheneBlockTx + pfrom->nSizeGrapheneBlock, blockSize);
        LOG(GRAPHENE, "Graphene block stats: %s\n", graphenedata.ToString());

        PV->HandleBlockMessage(pfrom, strCommand, MakeBlockRef(pfrom->grapheneBlock), inv);
    }

    return true;
}

CRequestGrapheneBlockTx::CRequestGrapheneBlockTx(uint256 blockHash, std::set<uint64_t> &setHashesToRequest)
{
    blockhash = blockHash;
    setCheapHashesToRequest = setHashesToRequest;
}

bool CRequestGrapheneBlockTx::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    if (!pfrom->GrapheneCapable())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("get_grblocktx message received from a non GRAPHENE node, peer=%s", pfrom->GetLogName());
    }

    CRequestGrapheneBlockTx grapheneRequestBlockTx;
    vRecv >> grapheneRequestBlockTx;

    // Message consistency checking
    if (grapheneRequestBlockTx.setCheapHashesToRequest.empty() || grapheneRequestBlockTx.blockhash.IsNull())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("Incorrectly constructed get_grblocktx received.  Banning peer=%s", pfrom->GetLogName());
    }

    // We use MSG_TX here even though we refer to blockhash because we need to track
    // how many grblocktx requests we make in case of DOS
    CInv inv(MSG_TX, grapheneRequestBlockTx.blockhash);
    LOG(GRAPHENE, "Received get_grblocktx for %s peer=%s\n", inv.hash.ToString(), pfrom->GetLogName());

    // Check for Misbehaving and DOS
    // If they make more than 20 requests in 10 minutes then disconnect them
    {
        LOCK(cs_vNodes);
        if (pfrom->nGetGrapheneBlockTxLastTime <= 0)
            pfrom->nGetGrapheneBlockTxLastTime = GetTime();
        uint64_t nNow = GetTime();
        pfrom->nGetGrapheneBlockTxCount *=
            std::pow(1.0 - 1.0 / 600.0, (double)(nNow - pfrom->nGetGrapheneBlockTxLastTime));
        pfrom->nGetGrapheneBlockTxLastTime = nNow;
        pfrom->nGetGrapheneBlockTxCount += 1;
        LOG(GRAPHENE, "nGetGrapheneTxCount is %f\n", pfrom->nGetGrapheneBlockTxCount);
        if (pfrom->nGetGrapheneBlockTxCount >= 20)
        {
            dosMan.Misbehaving(pfrom, 100); // If they exceed the limit then disconnect them
            return error("DOS: Misbehaving - requesting too many grblocktx: %s\n", inv.hash.ToString());
        }
    }

    {
        LOCK(cs_main);
        std::vector<CTransaction> vTx;
        BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
        if (mi == mapBlockIndex.end())
        {
            dosMan.Misbehaving(pfrom, 20);
            return error("Requested block is not available");
        }
        else
        {
            CBlock block;
            const Consensus::Params &consensusParams = Params().GetConsensus();
            if (!ReadBlockFromDisk(block, (*mi).second, consensusParams))
            {
                // We do not assign misbehavior for not being able to read a block from disk because we already
                // know that the block is in the block index from the step above. Secondly, a failure to read may
                // be our own issue or the remote peer's issue in requesting too early.  We can't know at this point.
                return error("Cannot load block from disk -- Block txn request possibly received before assembled");
            }
            else
            {
                for (auto &tx : block.vtx)
                {
                    uint64_t cheapHash = tx->GetHash().GetCheapHash();

                    if (grapheneRequestBlockTx.setCheapHashesToRequest.count(cheapHash))
                        vTx.push_back(*tx);
                }
            }
        }
        CGrapheneBlockTx grapheneBlockTx(grapheneRequestBlockTx.blockhash, vTx);
        pfrom->PushMessage(NetMsgType::GRAPHENETX, grapheneBlockTx);
        pfrom->blocksSent += 1;
    }

    return true;
}

bool CGrapheneBlock::CheckBlockHeader(const CBlockHeader &block, CValidationState &state)
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
 * Handle an incoming graphene block
 * Once the block is validated apart from the Merkle root, forward the Xpedited block with a hop count of nHops.
 */
bool CGrapheneBlock::HandleMessage(CDataStream &vRecv, CNode *pfrom, std::string strCommand, unsigned nHops)
{
    if (!pfrom->GrapheneCapable())
    {
        dosMan.Misbehaving(pfrom, 5);
        return error("%s message received from a non GRAPHENE node, peer=%s", strCommand, pfrom->GetLogName());
    }

    int nSizeGrapheneBlock = vRecv.size();
    CInv inv(MSG_BLOCK, uint256());

    CGrapheneBlock grapheneBlock;
    vRecv >> grapheneBlock;

    {
        LOCK(cs_main);

        // Message consistency checking (FIXME: some redundancy here with AcceptBlockHeader)
        if (!IsGrapheneBlockValid(pfrom, grapheneBlock.header))
        {
            dosMan.Misbehaving(pfrom, 100);
            LOGA("Received an invalid %s from peer %s\n", strCommand, pfrom->GetLogName());

            graphenedata.ClearGrapheneBlockData(pfrom, grapheneBlock.header.GetHash());
            return false;
        }

        // Is there a previous block or header to connect with?
        {
            uint256 prevHash = grapheneBlock.header.hashPrevBlock;
            BlockMap::iterator mi = mapBlockIndex.find(prevHash);
            if (mi == mapBlockIndex.end())
            {
                return error("Graphene block from peer %s will not connect, unknown previous block %s",
                    pfrom->GetLogName(), prevHash.ToString());
            }
        }

        CValidationState state;
        CBlockIndex *pIndex = nullptr;
        if (!AcceptBlockHeader(grapheneBlock.header, state, Params(), &pIndex))
        {
            int nDoS;
            if (state.IsInvalid(nDoS))
            {
                if (nDoS > 0)
                    dosMan.Misbehaving(pfrom, nDoS);
                LOGA("Received an invalid %s header from peer %s\n", strCommand, pfrom->GetLogName());
            }

            graphenedata.ClearGrapheneBlockData(pfrom, grapheneBlock.header.GetHash());
            return false;
        }

        // pIndex should always be set by AcceptBlockHeader
        if (!pIndex)
        {
            LOGA("INTERNAL ERROR: pIndex null in CGrapheneBlock::HandleMessage");
            graphenedata.ClearGrapheneBlockData(pfrom, grapheneBlock.header.GetHash());
            return true;
        }

        inv.hash = pIndex->GetBlockHash();
        requester.UpdateBlockAvailability(pfrom->GetId(), inv.hash);

        // Return early if we already have the block data
        if (pIndex->nStatus & BLOCK_HAVE_DATA)
        {
            // Tell the Request Manager we received this block
            requester.AlreadyReceived(inv);

            graphenedata.ClearGrapheneBlockData(pfrom, grapheneBlock.header.GetHash());
            LOG(GRAPHENE, "Received grapheneblock but returning because we already have block data %s from peer %s hop"
                          " %d size %d bytes\n",
                inv.hash.ToString(), pfrom->GetLogName(), nHops, nSizeGrapheneBlock);
            return true;
        }

        // Request failover block if this one isn't extending the best chain
        if (pIndex->nChainWork <= chainActive.Tip()->nChainWork)
        {
            std::vector<CInv> vGetData;
            vGetData.push_back(inv);
            pfrom->PushMessage(NetMsgType::GETDATA, vGetData);

            graphenedata.ClearGrapheneBlockData(pfrom, grapheneBlock.header.GetHash());

            RequestFailoverBlock(pfrom, grapheneBlock.header.GetHash());
            LOGA("%s %s from peer %s received but does not extend longest chain; requesting failover block\n",
                strCommand, inv.hash.ToString(), pfrom->GetLogName());
            return true;
        }

        {
            LOG(GRAPHENE, "Received %s %s from peer %s. Size %d bytes.\n", strCommand, inv.hash.ToString(),
                pfrom->GetLogName(), nSizeGrapheneBlock);

            // Do not process unrequested grapheneblocks.
            LOCK(pfrom->cs_mapgrapheneblocksinflight);
            if (!pfrom->mapGrapheneBlocksInFlight.count(inv.hash))
            {
                dosMan.Misbehaving(pfrom, 10);
                return error(
                    "%s %s from peer %s but was unrequested\n", strCommand, inv.hash.ToString(), pfrom->GetLogName());
            }
        }
    }

    bool result = grapheneBlock.process(pfrom, nSizeGrapheneBlock, strCommand);

    return result;
}

bool CGrapheneBlock::process(CNode *pfrom,
    int nSizeGrapheneBlock,
    std::string strCommand) // TODO: request from the "best" txn source not necessarily from the block source
{
    // In PV we must prevent two graphene blocks from simulaneously processing from that were recieved from the
    // same peer. This would only happen as in the example of an expedited block coming in
    // after an graphene request, because we would never explicitly request two graphene blocks from the same peer.
    if (PV->IsAlreadyValidating(pfrom->id))
        return false;

    // Xpress Validation - only perform xval if the chaintip matches the last blockhash in the graphene block
    bool fXVal;
    {
        LOCK(cs_main);
        fXVal = (header.hashPrevBlock == chainActive.Tip()->GetBlockHash()) ? true : false;
    }

    graphenedata.ClearGrapheneBlockData(pfrom);
    pfrom->nSizeGrapheneBlock = nSizeGrapheneBlock;

    uint256 nullhash;
    pfrom->grapheneBlock.nVersion = header.nVersion;
    pfrom->grapheneBlock.nBits = header.nBits;
    pfrom->grapheneBlock.nNonce = header.nNonce;
    pfrom->grapheneBlock.nTime = header.nTime;
    pfrom->grapheneBlock.hashMerkleRoot = header.hashMerkleRoot;
    pfrom->grapheneBlock.hashPrevBlock = header.hashPrevBlock;

    {
        LOCK(pfrom->cs_grapheneadditionaltxs);

        pfrom->grapheneAdditionalTxs.clear();
        for (auto tx : vAdditionalTxs)
            pfrom->grapheneAdditionalTxs.push_back(tx);
    }

    vTxHashes.reserve(nBlockTxs);

    // Create a map of all 8 bytes tx hashes pointing to their full tx hash counterpart
    // We need to check all transaction sources (orphan list, mempool, and new (incoming) transactions in this block)
    // for a collision.
    int missingCount = 0;
    int unnecessaryCount = 0;
    bool collision = false;
    std::set<uint256> passingTxHashes;
    std::map<uint64_t, uint256> mapPartialTxHash;
    std::vector<uint256> memPoolHashes;
    std::set<uint64_t> setHashesToRequest;

    bool fMerkleRootCorrect = true;
    {
        // Do the orphans first before taking the mempool.cs lock, so that we maintain correct locking order.
        LOCK(orphanpool.cs);
        for (auto &kv : orphanpool.mapOrphanTransactions)
        {
            uint64_t cheapHash = kv.first.GetCheapHash();

            if (mapPartialTxHash.count(cheapHash)) // Check for collisions
                collision = true;

            mapPartialTxHash[cheapHash] = kv.first;
        }

        // We don't have to keep the lock on mempool.cs here to do mempool.queryHashes
        // but we take the lock anyway so we don't have to re-lock again later.
        LOCK(cs_xval);
        mempool.queryHashes(memPoolHashes);

        for (const uint256 &hash : memPoolHashes)
        {
            uint64_t cheapHash = hash.GetCheapHash();

            if (mapPartialTxHash.count(cheapHash)) // Check for collisions
                collision = true;

            mapPartialTxHash[cheapHash] = hash;
        }

        // Add full transactions included in the block
        for (auto &tx : vAdditionalTxs)
        {
            const uint256 &hash = tx->GetHash();
            uint64_t cheapHash = hash.GetCheapHash();

            if (mapPartialTxHash.count(cheapHash)) // Check for collisions
                collision = true;

            mapPartialTxHash[cheapHash] = hash;
        }

        if (!collision)
        {
            std::vector<uint256> localHashes;
            for (const std::pair<uint64_t, uint256> &kv : mapPartialTxHash)
                localHashes.push_back(kv.second);

            try
            {
                std::vector<uint64_t> blockCheapHashes = pGrapheneSet->Reconcile(localHashes);

                // Sort out what hashes we have from the complete set of cheapHashes
                uint64_t nGrapheneTxsPossessed = 0;
                for (size_t i = 0; i < blockCheapHashes.size(); i++)
                {
                    uint64_t cheapHash = blockCheapHashes[i];

                    // Update mapHashOrderIndex so it is available if we later receive missing txs
                    pfrom->grapheneMapHashOrderIndex[cheapHash] = i;

                    if (mapPartialTxHash.count(cheapHash) > 0)
                    {
                        pfrom->grapheneBlockHashes.push_back(mapPartialTxHash[cheapHash]);

                        nGrapheneTxsPossessed++;
                    }
                    else
                    {
                        pfrom->grapheneBlockHashes.push_back(nullhash);
                        setHashesToRequest.insert(cheapHash);
                    }
                }

                graphenedata.AddGrapheneBlockBytes(nGrapheneTxsPossessed * sizeof(uint64_t), pfrom);
            }
            catch (const std::runtime_error &e)
            {
                RequestFailoverBlock(pfrom, header.GetHash());
                LOG(GRAPHENE, "Graphene set could not be reconciled; requesting failover for peer %s: %s\n",
                    pfrom->GetLogName(), e.what());

                graphenedata.ClearGrapheneBlockData(pfrom, header.GetHash());
                graphenedata.IncrementDecodeFailures();

                return true;
            }

            // Reconstruct the block if there are no hashes to re-request
            if (setHashesToRequest.empty())
            {
                bool mutated;
                uint256 merkleroot = ComputeMerkleRoot(pfrom->grapheneBlockHashes, &mutated);
                if (header.hashMerkleRoot != merkleroot || mutated)
                    fMerkleRootCorrect = false;
                else
                {
                    if (!ReconstructBlock(pfrom, fXVal, missingCount, unnecessaryCount))
                        return false;
                }
            }
        }
    } // End locking cs_orphancache, mempool.cs and cs_xval
    LOG(GRAPHENE, "Total in-memory graphene bytes size is %ld bytes\n", graphenedata.GetGrapheneBlockBytes());

    // These must be checked outside of the mempool.cs lock or deadlock may occur.
    // A merkle root mismatch here does not cause a ban because and expedited node will forward an graphene
    // without checking the merkle root, therefore we don't want to ban our expedited nodes. Just request
    // a failover block if a mismatch occurs.
    // Also, there is a remote possiblity of a Tx hash collision therefore if it occurs we request a failover
    // block.
    if (collision || !fMerkleRootCorrect)
    {
        RequestFailoverBlock(pfrom, header.GetHash());
        graphenedata.ClearGrapheneBlockData(pfrom, header.GetHash());

        if (!fMerkleRootCorrect)
            return error(
                "Mismatched merkle root on grapheneblock: requesting failover block, peer=%s", pfrom->GetLogName());
        else
            return error("TX HASH COLLISION for grapheneblock: requesting a full block, peer=%s", pfrom->GetLogName());
    }

    pfrom->grapheneBlockWaitingForTxns = missingCount;
    LOG(GRAPHENE, "Graphene block waiting for: %d, unnecessary: %d, total txns: %d received txns: %d\n",
        pfrom->grapheneBlockWaitingForTxns, unnecessaryCount, pfrom->grapheneBlock.vtx.size(),
        pfrom->mapMissingTx.size());

    // If there are any missing hashes or transactions then we request them here.
    // This must be done outside of the mempool.cs lock or may deadlock.
    if (setHashesToRequest.size() > 0)
    {
        pfrom->grapheneBlockWaitingForTxns = setHashesToRequest.size();
        CRequestGrapheneBlockTx grapheneBlockTx(header.GetHash(), setHashesToRequest);
        pfrom->PushMessage(NetMsgType::GET_GRAPHENETX, grapheneBlockTx);

        // Update run-time statistics of graphene block bandwidth savings
        graphenedata.UpdateInBoundReRequestedTx(pfrom->grapheneBlockWaitingForTxns);

        return true;
    }

    // If there are still any missing transactions then we must clear out the graphene block data
    // and re-request failover block (This should never happen because we just checked the various pools).
    if (missingCount > 0)
    {
        // Since we can't process this graphene block then clear out the data from memory
        graphenedata.ClearGrapheneBlockData(pfrom, header.GetHash());

        RequestFailoverBlock(pfrom, header.GetHash());
        return error("Still missing transactions for graphene block: re-requesting failover block");
    }

    // We now have all the transactions that are in this block
    pfrom->grapheneBlockWaitingForTxns = -1;
    int blockSize = ::GetSerializeSize(pfrom->grapheneBlock, SER_NETWORK, CBlock::CURRENT_VERSION);
    LOG(GRAPHENE,
        "Reassembled graphene block for %s (%d bytes). Message was %d bytes, compression ratio %3.2f, peer=%s\n",
        pfrom->grapheneBlock.GetHash().ToString(), blockSize, pfrom->nSizeGrapheneBlock,
        ((float)blockSize) / ((float)pfrom->nSizeGrapheneBlock), pfrom->GetLogName());

    // Update run-time statistics of graphene block bandwidth savings
    graphenedata.UpdateInBound(pfrom->nSizeGrapheneBlock, blockSize);
    LOG(GRAPHENE, "Graphene block stats: %s\n", graphenedata.ToString().c_str());

    // Process the full block
    PV->HandleBlockMessage(pfrom, strCommand, MakeBlockRef(pfrom->grapheneBlock), GetInv());

    return true;
}

static bool ReconstructBlock(CNode *pfrom, const bool fXVal, int &missingCount, int &unnecessaryCount)
{
    AssertLockHeld(cs_xval);

    // We must have all the full tx hashes by this point.  We first check for any repeating
    // sequences in transaction id's.  This is a possible attack vector and has been used in the past.
    {
        std::set<uint256> setHashes(pfrom->grapheneBlockHashes.begin(), pfrom->grapheneBlockHashes.end());
        if (setHashes.size() != pfrom->grapheneBlockHashes.size())
        {
            graphenedata.ClearGrapheneBlockData(pfrom, pfrom->grapheneBlock.GetBlockHeader().GetHash());

            dosMan.Misbehaving(pfrom, 10);
            return error("Repeating Transaction Id sequence, peer=%s", pfrom->GetLogName());
        }
    }

    // The total maximum bytes that we can use to create a graphene block. We use shared pointers for
    // the transactions in the graphene block so we don't need to make as much memory available as we did in
    // the past. We caluculate the max memory allowed by using the largest block size possible, which is the
    // (maxMessageSizeMultiplier * excessiveBlockSize), then divide that by the smallest transaction possible
    // which is 158 bytes on a 32bit system.  That gives us the largest number of transactions possible in a block.
    // Then we multiply number of possible transactions by the size of a shared pointer.
    // NOTE * The 158 byte smallest txn possible was found by getting the smallest serialized size of a txn directly
    //        from the blockchain, on a 32bit system.
    CTransactionRef dummyptx = nullptr;
    uint32_t nTxSize = sizeof(dummyptx);
    uint64_t maxAllowedSize = nTxSize * maxMessageSizeMultiplier * excessiveBlockSize / 158;

    std::map<uint256, CTransactionRef> mapAdditionalTxs;
    {
        LOCK(pfrom->cs_grapheneadditionaltxs);

        for (auto tx : pfrom->grapheneAdditionalTxs)
            mapAdditionalTxs[tx->GetHash()] = tx;
    }

    // Look for each transaction in our various pools and buffers.
    // With grapheneBlocks the vTxHashes contains only the first 8 bytes of the tx hash.
    for (const uint256 &hash : pfrom->grapheneBlockHashes)
    {
        // Replace the truncated hash with the full hash value if it exists
        CTransactionRef ptx = nullptr;
        if (!hash.IsNull())
        {
            bool inMemPool = false;
            ptx = mempool.get(hash);
            if (ptx)
                inMemPool = true;

            bool inMissingTx = pfrom->mapMissingTx.count(hash.GetCheapHash()) > 0;
            bool inAdditionalTxs = mapAdditionalTxs.count(hash) > 0;
            bool inOrphanCache = orphanpool.mapOrphanTransactions.count(hash) > 0;

            if ((inMemPool && inMissingTx) || (inOrphanCache && inMissingTx) || (inAdditionalTxs && inMissingTx))
                unnecessaryCount++;

            if (inAdditionalTxs)
                ptx = mapAdditionalTxs[hash];
            else if (inOrphanCache)
            {
                ptx = orphanpool.mapOrphanTransactions[hash].ptx;
                setUnVerifiedOrphanTxHash.insert(hash);
            }
            else if (inMemPool && fXVal)
                setPreVerifiedTxHash.insert(hash);
            else if (inMissingTx)
                ptx = pfrom->mapMissingTx[hash.GetCheapHash()];
        }
        if (!ptx)
            missingCount++;

        // In order to prevent a memory exhaustion attack we track transaction bytes used to create Block
        // to see if we've exceeded any limits and if so clear out data and return.
        if (graphenedata.AddGrapheneBlockBytes(nTxSize, pfrom) > maxAllowedSize)
        {
            LEAVE_CRITICAL_SECTION(cs_xval); // maintain locking order with vNodes
            if (ClearLargestGrapheneBlockAndDisconnect(pfrom))
            {
                ENTER_CRITICAL_SECTION(cs_xval);
                return error(
                    "Reconstructed block %s (size:%llu) has caused max memory limit %llu bytes to be exceeded, peer=%s",
                    pfrom->grapheneBlock.GetHash().ToString(), pfrom->nLocalGrapheneBlockBytes, maxAllowedSize,
                    pfrom->GetLogName());
            }
            ENTER_CRITICAL_SECTION(cs_xval);
        }
        if (pfrom->nLocalGrapheneBlockBytes > maxAllowedSize)
        {
            graphenedata.ClearGrapheneBlockData(pfrom, pfrom->grapheneBlock.GetBlockHeader().GetHash());
            pfrom->fDisconnect = true;
            return error(
                "Reconstructed block %s (size:%llu) has caused max memory limit %llu bytes to be exceeded, peer=%s",
                pfrom->grapheneBlock.GetHash().ToString(), pfrom->nLocalGrapheneBlockBytes, maxAllowedSize,
                pfrom->GetLogName());
        }

        // Add this transaction. If the tx is null we still add it as a placeholder to keep the correct ordering.
        pfrom->grapheneBlock.vtx.emplace_back(ptx);
    }

    return true;
}

template <class T>
void CGrapheneBlockData::expireStats(std::map<int64_t, T> &statsMap)
{
    AssertLockHeld(cs_graphenestats);
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
void CGrapheneBlockData::updateStats(std::map<int64_t, T> &statsMap, T value)
{
    AssertLockHeld(cs_graphenestats);
    statsMap[getTimeForStats()] = value;
    expireStats(statsMap);
}

/**
   Calculate average of values in map. Return 0 for no entries.
   Expires values before calculation. */
double CGrapheneBlockData::average(std::map<int64_t, uint64_t> &map)
{
    AssertLockHeld(cs_graphenestats);

    expireStats(map);

    if (map.size() == 0)
        return 0.0;

    uint64_t accum = 0U;
    for (std::pair<int64_t, uint64_t> p : map)
    {
        // avoid wraparounds
        accum = std::max(accum, accum + p.second);
    }
    return (double)accum / map.size();
}

void CGrapheneBlockData::IncrementDecodeFailures()
{
    LOCK(cs_graphenestats);
    nDecodeFailures += 1;
}

void CGrapheneBlockData::UpdateInBound(uint64_t nGrapheneBlockSize, uint64_t nOriginalBlockSize)
{
    LOCK(cs_graphenestats);
    // Update InBound graphene block tracking information
    nOriginalSize += nOriginalBlockSize;
    nGrapheneSize += nGrapheneBlockSize;
    nInBoundBlocks += 1;
    updateStats(mapGrapheneBlocksInBound, std::pair<uint64_t, uint64_t>(nGrapheneBlockSize, nOriginalBlockSize));
}

void CGrapheneBlockData::UpdateOutBound(uint64_t nGrapheneBlockSize, uint64_t nOriginalBlockSize)
{
    LOCK(cs_graphenestats);
    nOriginalSize += nOriginalBlockSize;
    nGrapheneSize += nGrapheneBlockSize;
    nOutBoundBlocks += 1;
    updateStats(mapGrapheneBlocksOutBound, std::pair<uint64_t, uint64_t>(nGrapheneBlockSize, nOriginalBlockSize));
}

void CGrapheneBlockData::UpdateOutBoundMemPoolInfo(uint64_t nMemPoolInfoSize)
{
    LOCK(cs_graphenestats);
    nTotalMemPoolInfoBytes += nMemPoolInfoSize;
    updateStats(mapMemPoolInfoOutBound, nMemPoolInfoSize);
}

void CGrapheneBlockData::UpdateInBoundMemPoolInfo(uint64_t nMemPoolInfoSize)
{
    LOCK(cs_graphenestats);
    nTotalMemPoolInfoBytes += nMemPoolInfoSize;
    updateStats(mapMemPoolInfoInBound, nMemPoolInfoSize);
}

void CGrapheneBlockData::UpdateFilter(uint64_t nFilterSize)
{
    LOCK(cs_graphenestats);
    nTotalFilterBytes += nFilterSize;
    updateStats(mapFilter, nFilterSize);
}

void CGrapheneBlockData::UpdateIblt(uint64_t nIbltSize)
{
    LOCK(cs_graphenestats);
    nTotalIbltBytes += nIbltSize;
    updateStats(mapIblt, nIbltSize);
}

void CGrapheneBlockData::UpdateRank(uint64_t nRankSize)
{
    LOCK(cs_graphenestats);
    nTotalRankBytes += nRankSize;
    updateStats(mapRank, nRankSize);
}

void CGrapheneBlockData::UpdateGrapheneBlock(uint64_t nGrapheneBlockSize)
{
    LOCK(cs_graphenestats);
    nTotalGrapheneBlockBytes += nGrapheneBlockSize;
    updateStats(mapGrapheneBlock, nGrapheneBlockSize);
}

void CGrapheneBlockData::UpdateAdditionalTx(uint64_t nAdditionalTxSize)
{
    LOCK(cs_graphenestats);
    nTotalAdditionalTxBytes += nAdditionalTxSize;
    updateStats(mapAdditionalTx, nAdditionalTxSize);
}

void CGrapheneBlockData::UpdateResponseTime(double nResponseTime)
{
    LOCK(cs_graphenestats);

    // only update stats if IBD is complete
    if (IsChainNearlySyncd() && IsGrapheneBlockEnabled())
        updateStats(mapGrapheneBlockResponseTime, nResponseTime);
}

void CGrapheneBlockData::UpdateValidationTime(double nValidationTime)
{
    LOCK(cs_graphenestats);

    // only update stats if IBD is complete
    if (IsChainNearlySyncd() && IsGrapheneBlockEnabled())
        updateStats(mapGrapheneBlockValidationTime, nValidationTime);
}

void CGrapheneBlockData::UpdateInBoundReRequestedTx(int nReRequestedTx)
{
    LOCK(cs_graphenestats);

    // Update InBound graphene block tracking information
    updateStats(mapGrapheneBlocksInBoundReRequestedTx, nReRequestedTx);
}

std::string CGrapheneBlockData::ToString()
{
    LOCK(cs_graphenestats);
    double size = double(nOriginalSize() - nGrapheneSize() - nTotalMemPoolInfoBytes());
    std::ostringstream ss;
    ss << nInBoundBlocks() << " inbound and " << nOutBoundBlocks() << " outbound graphene blocks have saved "
       << formatInfoUnit(size) << " of bandwidth with " << nDecodeFailures() << " local decode "
       << ((nDecodeFailures() == 1) ? "failure" : "failures");

    return ss.str();
}

// Calculate the graphene percentage compression over the last 24 hours
std::string CGrapheneBlockData::InBoundPercentToString()
{
    LOCK(cs_graphenestats);

    expireStats(mapGrapheneBlocksInBound);

    double nCompressionRate = 0;
    uint64_t nGrapheneSizeTotal = 0;
    uint64_t nOriginalSizeTotal = 0;
    for (std::map<int64_t, std::pair<uint64_t, uint64_t> >::iterator mi = mapGrapheneBlocksInBound.begin();
         mi != mapGrapheneBlocksInBound.end(); ++mi)
    {
        nGrapheneSizeTotal += (*mi).second.first;
        nOriginalSizeTotal += (*mi).second.second;
    }
    // We count up the outbound CMemPoolInfo sizes. Outbound CMemPoolInfo sizes go with Inbound graphene blocks.
    uint64_t nOutBoundMemPoolInfoSize = 0;
    for (std::map<int64_t, uint64_t>::iterator mi = mapMemPoolInfoOutBound.begin(); mi != mapMemPoolInfoOutBound.end();
         ++mi)
    {
        nOutBoundMemPoolInfoSize += (*mi).second;
    }

    if (nOriginalSizeTotal > 0)
        nCompressionRate = 100 - (100 * (double)(nGrapheneSizeTotal + nOutBoundMemPoolInfoSize) / nOriginalSizeTotal);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Compression for " << mapGrapheneBlocksInBound.size()
       << " Inbound  graphene blocks (last 24hrs): " << nCompressionRate << "%";

    return ss.str();
}

// Calculate the graphene percentage compression over the last 24 hours
std::string CGrapheneBlockData::OutBoundPercentToString()
{
    LOCK(cs_graphenestats);

    expireStats(mapGrapheneBlocksOutBound);

    double nCompressionRate = 0;
    uint64_t nGrapheneSizeTotal = 0;
    uint64_t nOriginalSizeTotal = 0;
    for (std::map<int64_t, std::pair<uint64_t, uint64_t> >::iterator mi = mapGrapheneBlocksOutBound.begin();
         mi != mapGrapheneBlocksOutBound.end(); ++mi)
    {
        nGrapheneSizeTotal += (*mi).second.first;
        nOriginalSizeTotal += (*mi).second.second;
    }
    // We count up the inbound CMemPoolInfo sizes. Inbound CMemPoolInfo sizes go with Outbound graphene blocks.
    uint64_t nInBoundMemPoolInfoSize = 0;
    for (std::map<int64_t, uint64_t>::iterator mi = mapMemPoolInfoInBound.begin(); mi != mapMemPoolInfoInBound.end();
         ++mi)
        nInBoundMemPoolInfoSize += (*mi).second;

    if (nOriginalSizeTotal > 0)
        nCompressionRate = 100 - (100 * (double)(nGrapheneSizeTotal + nInBoundMemPoolInfoSize) / nOriginalSizeTotal);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Compression for " << mapGrapheneBlocksOutBound.size()
       << " Outbound graphene blocks (last 24hrs): " << nCompressionRate << "%";
    return ss.str();
}

// Calculate the average inbound graphene CMemPoolInfo size
std::string CGrapheneBlockData::InBoundMemPoolInfoToString()
{
    LOCK(cs_graphenestats);
    double avgMemPoolInfoSize = average(mapMemPoolInfoInBound);
    std::ostringstream ss;
    ss << "Inbound CMemPoolInfo size (last 24hrs) AVG: " << formatInfoUnit(avgMemPoolInfoSize);
    return ss.str();
}

// Calculate the average outbound graphene CMemPoolInfo size
std::string CGrapheneBlockData::OutBoundMemPoolInfoToString()
{
    LOCK(cs_graphenestats);
    double avgMemPoolInfoSize = average(mapMemPoolInfoOutBound);
    std::ostringstream ss;
    ss << "Outbound CMemPoolInfo size (last 24hrs) AVG: " << formatInfoUnit(avgMemPoolInfoSize);
    return ss.str();
}

std::string CGrapheneBlockData::FilterToString()
{
    LOCK(cs_graphenestats);
    double avgFilterSize = average(mapFilter);
    std::ostringstream ss;
    ss << "Bloom filter size (last 24hrs) AVG: " << formatInfoUnit(avgFilterSize);
    return ss.str();
}

std::string CGrapheneBlockData::IbltToString()
{
    LOCK(cs_graphenestats);
    double avgIbltSize = average(mapIblt);
    std::ostringstream ss;
    ss << "IBLT size (last 24hrs) AVG: " << formatInfoUnit(avgIbltSize);
    return ss.str();
}

std::string CGrapheneBlockData::RankToString()
{
    LOCK(cs_graphenestats);
    double avgRankSize = average(mapRank);
    std::ostringstream ss;
    ss << "Rank size (last 24hrs) AVG: " << formatInfoUnit(avgRankSize);
    return ss.str();
}

std::string CGrapheneBlockData::GrapheneBlockToString()
{
    LOCK(cs_graphenestats);
    double avgGrapheneBlockSize = average(mapGrapheneBlock);
    std::ostringstream ss;
    ss << "Graphene block size (last 24hrs) AVG: " << formatInfoUnit(avgGrapheneBlockSize);
    return ss.str();
}

std::string CGrapheneBlockData::AdditionalTxToString()
{
    LOCK(cs_graphenestats);
    double avgAdditionalTxSize = average(mapAdditionalTx);
    std::ostringstream ss;
    ss << "Graphene size additional txs (last 24hrs) AVG: " << formatInfoUnit(avgAdditionalTxSize);
    return ss.str();
}

// Calculate the graphene average response time over the last 24 hours
std::string CGrapheneBlockData::ResponseTimeToString()
{
    LOCK(cs_graphenestats);

    std::vector<double> vResponseTime;

    double nResponseTimeAverage = 0;
    double nPercentile = 0;
    double nTotalResponseTime = 0;
    double nTotalEntries = 0;
    for (std::map<int64_t, double>::iterator mi = mapGrapheneBlockResponseTime.begin();
         mi != mapGrapheneBlockResponseTime.end(); ++mi)
    {
        nTotalEntries += 1;
        nTotalResponseTime += (*mi).second;
        vResponseTime.push_back((*mi).second);
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

// Calculate the graphene average block validation time over the last 24 hours
std::string CGrapheneBlockData::ValidationTimeToString()
{
    LOCK(cs_graphenestats);

    std::vector<double> vValidationTime;

    double nValidationTimeAverage = 0;
    double nPercentile = 0;
    double nTotalValidationTime = 0;
    double nTotalEntries = 0;
    for (std::map<int64_t, double>::iterator mi = mapGrapheneBlockValidationTime.begin();
         mi != mapGrapheneBlockValidationTime.end(); ++mi)
    {
        nTotalEntries += 1;
        nTotalValidationTime += (*mi).second;
        vValidationTime.push_back((*mi).second);
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

// Calculate the graphene average tx re-requested ratio over the last 24 hours
std::string CGrapheneBlockData::ReRequestedTxToString()
{
    LOCK(cs_graphenestats);

    expireStats(mapGrapheneBlocksInBoundReRequestedTx);

    double nReRequestRate = 0;
    uint64_t nTotalReRequests = 0;
    uint64_t nTotalReRequestedTxs = 0;
    for (std::map<int64_t, int>::iterator mi = mapGrapheneBlocksInBoundReRequestedTx.begin();
         mi != mapGrapheneBlocksInBoundReRequestedTx.end(); ++mi)
    {
        nTotalReRequests += 1;
        nTotalReRequestedTxs += (*mi).second;
    }

    if (mapGrapheneBlocksInBound.size() > 0)
        nReRequestRate = 100 * (double)nTotalReRequests / mapGrapheneBlocksInBound.size();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Tx re-request rate (last 24hrs): " << nReRequestRate << "% Total re-requests:" << nTotalReRequests;
    return ss.str();
}

// Preferential Graphene Block Timer:
// The purpose of the timer is to ensure that we more often download an GRAPHENEBLOCK rather than a full block.
// The timer is started when we receive the first announcement indicating there is a new block to download.  If the
// block inventory is from a non GRAPHENE node then we will continue to wait for block announcements until either we
// get one from an GRAPHENE capable node or the timer is exceeded.  If the timer is exceeded before receiving an
// announcement from an GRAPHENE node then we just download a full block instead of a graphene block.
bool CGrapheneBlockData::CheckGrapheneBlockTimer(const uint256 &hash)
{
    // Base time used to calculate the random timeout value.
    static int64_t nTimeToWait = 10000;

    LOCK(cs_mapGrapheneBlockTimer);
    if (!mapGrapheneBlockTimer.count(hash))
    {
        // The timeout limit is a random number betwee 8 and 12 seconds.
        // This way a node connected to this one may download the block
        // before the other node and thus be able to serve the other with
        // a graphene block, rather than both nodes timing out and downloading
        // a thinblock instead. This can happen at the margins of the BU network
        // where we receive full blocks from peers that don't support graphene.
        //
        // To make the timeout random we adjust the start time of the timer forward
        // or backward by a random amount plus or minus 2 seconds.
        FastRandomContext insecure_rand(false);
        uint64_t nOffset = nTimeToWait - (8000 + (insecure_rand.rand64() % 4000) + 1);
        mapGrapheneBlockTimer[hash] = GetTimeMillis() + nOffset;
        LOG(GRAPHENE, "Starting Preferential Graphene Block timer (%d millis)\n", nTimeToWait + nOffset);
    }
    else
    {
        // Check that we have not exceeded time limit.
        // If we have then we want to return false so that we can
        // proceed to download a regular block instead.
        int64_t elapsed = GetTimeMillis() - mapGrapheneBlockTimer[hash];
        if (elapsed > nTimeToWait)
        {
            LOG(GRAPHENE, "Preferential Graphene Block timer exceeded\n");
            return false;
        }
    }
    return true;
}

// The timer is cleared as soon as we request a block or graphene block.
void CGrapheneBlockData::ClearGrapheneBlockTimer(const uint256 &hash)
{
    LOCK(cs_mapGrapheneBlockTimer);
    if (mapGrapheneBlockTimer.count(hash))
    {
        mapGrapheneBlockTimer.erase(hash);
        LOG(GRAPHENE, "Clearing Preferential Graphene Block timer\n");
    }
}

// After a graphene block is finished processing or if for some reason we have to pre-empt the rebuilding
// of a graphene block then we clear out the graphene block data which can be substantial.
void CGrapheneBlockData::ClearGrapheneBlockData(CNode *pnode)
{
    // Remove bytes from counter
    graphenedata.DeleteGrapheneBlockBytes(pnode->nLocalGrapheneBlockBytes, pnode);
    pnode->nLocalGrapheneBlockBytes = 0;

    // Clear out graphene block data we no longer need
    pnode->grapheneBlockWaitingForTxns = -1;
    pnode->grapheneBlock.SetNull();
    pnode->grapheneBlockHashes.clear();
    pnode->grapheneMapHashOrderIndex.clear();
    pnode->mapGrapheneMissingTx.clear();

    LOG(GRAPHENE, "Total in-memory graphene bytes size after clearing a graphene block is %ld bytes\n",
        graphenedata.GetGrapheneBlockBytes());
}

void CGrapheneBlockData::ClearGrapheneBlockData(CNode *pnode, const uint256 &hash)
{
    // We must make sure to clear the graphene block data first before clearing the graphene block in flight.
    ClearGrapheneBlockData(pnode);
    ClearGrapheneBlockInFlight(pnode, hash);
}

void CGrapheneBlockData::ClearGrapheneBlockStats()
{
    LOCK(cs_graphenestats);

    nOriginalSize.Clear();
    nGrapheneSize.Clear();
    nInBoundBlocks.Clear();
    nOutBoundBlocks.Clear();
    nDecodeFailures.Clear();
    nTotalMemPoolInfoBytes.Clear();
    nTotalFilterBytes.Clear();
    nTotalIbltBytes.Clear();
    nTotalRankBytes.Clear();
    nTotalGrapheneBlockBytes.Clear();

    mapGrapheneBlocksInBound.clear();
    mapGrapheneBlocksOutBound.clear();
    mapMemPoolInfoOutBound.clear();
    mapMemPoolInfoInBound.clear();
    mapFilter.clear();
    mapIblt.clear();
    mapRank.clear();
    mapGrapheneBlock.clear();
    mapGrapheneBlockResponseTime.clear();
    mapGrapheneBlockValidationTime.clear();
    mapGrapheneBlocksInBoundReRequestedTx.clear();
}

uint64_t CGrapheneBlockData::AddGrapheneBlockBytes(uint64_t bytes, CNode *pfrom)
{
    pfrom->nLocalGrapheneBlockBytes += bytes;
    uint64_t ret = nGrapheneBlockBytes.fetch_add(bytes) + bytes;

    return ret;
}

void CGrapheneBlockData::DeleteGrapheneBlockBytes(uint64_t bytes, CNode *pfrom)
{
    if (bytes <= pfrom->nLocalGrapheneBlockBytes)
        pfrom->nLocalGrapheneBlockBytes -= bytes;

    if (bytes <= nGrapheneBlockBytes)
        nGrapheneBlockBytes.fetch_sub(bytes);
}

void CGrapheneBlockData::ResetGrapheneBlockBytes() { nGrapheneBlockBytes.store(0); }
uint64_t CGrapheneBlockData::GetGrapheneBlockBytes() { return nGrapheneBlockBytes.load(); }
bool HaveGrapheneNodes()
{
    {
        LOCK(cs_vNodes);
        for (CNode *pnode : vNodes)
            if (pnode->GrapheneCapable())
                return true;
    }

    return false;
}

bool IsGrapheneBlockEnabled() { return GetBoolArg("-use-grapheneblocks", false); }
bool CanGrapheneBlockBeDownloaded(CNode *pto)
{
    if (pto->GrapheneCapable())
        return true;

    return false;
}

bool ClearLargestGrapheneBlockAndDisconnect(CNode *pfrom)
{
    CNode *pLargest = nullptr;
    LOCK(cs_vNodes);
    for (CNode *pnode : vNodes)
    {
        if ((pLargest == nullptr) || (pnode->nLocalGrapheneBlockBytes > pLargest->nLocalGrapheneBlockBytes))
            pLargest = pnode;
    }
    if (pLargest != nullptr)
    {
        graphenedata.ClearGrapheneBlockData(pLargest, pLargest->grapheneBlock.GetBlockHeader().GetHash());
        pLargest->fDisconnect = true;

        // If the our node is currently using up the most graphene block bytes then return true so that we
        // can stop processing this graphene block and let the disconnection happen.
        if (pfrom == pLargest)
            return true;
    }

    return false;
}

void ClearGrapheneBlockInFlight(CNode *pfrom, const uint256 &hash)
{
    LOCK(pfrom->cs_mapgrapheneblocksinflight);
    pfrom->mapGrapheneBlocksInFlight.erase(hash);
}

void AddGrapheneBlockInFlight(CNode *pfrom, const uint256 &hash)
{
    LOCK(pfrom->cs_mapgrapheneblocksinflight);
    pfrom->mapGrapheneBlocksInFlight.insert(
        std::pair<uint256, CNode::CGrapheneBlockInFlight>(hash, CNode::CGrapheneBlockInFlight()));
}

void SendGrapheneBlock(CBlockRef pblock, CNode *pfrom, const CInv &inv)
{
    int64_t nReceiverMemPoolTx = pfrom->nGrapheneMemPoolTx;

    // Use the size of your own mempool if receiver did not send hers
    if (nReceiverMemPoolTx == -1)
    {
        nReceiverMemPoolTx = mempool.size();
    }

    if (inv.type == MSG_GRAPHENEBLOCK)
    {
        try
        {
            CGrapheneBlock grapheneBlock(MakeBlockRef(*pblock), nReceiverMemPoolTx);
            int nSizeBlock = ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION);
            int nSizeGrapheneBlock = ::GetSerializeSize(grapheneBlock, SER_NETWORK, PROTOCOL_VERSION);

            if (nSizeGrapheneBlock + MIN_MEMPOOL_INFO_BYTES >
                nSizeBlock) // If graphene block is larger than a regular block then
            // send a regular block instead
            {
                pfrom->PushMessage(NetMsgType::BLOCK, *pblock);
                LOG(GRAPHENE, "Sent regular block instead - graphene block size: %d vs block size: %d => peer: %s\n",
                    nSizeGrapheneBlock, nSizeBlock, pfrom->GetLogName());
            }
            else
            {
                graphenedata.UpdateOutBound(nSizeGrapheneBlock, nSizeBlock);
                pfrom->PushMessage(NetMsgType::GRAPHENEBLOCK, grapheneBlock);
                LOG(GRAPHENE, "Sent graphene block - size: %d vs block size: %d => peer: %s\n", nSizeGrapheneBlock,
                    nSizeBlock, pfrom->GetLogName());

                graphenedata.UpdateFilter(grapheneBlock.pGrapheneSet->GetFilterSerializationSize());
                graphenedata.UpdateIblt(grapheneBlock.pGrapheneSet->GetIbltSerializationSize());
                graphenedata.UpdateRank(grapheneBlock.pGrapheneSet->GetRankSerializationSize());
                graphenedata.UpdateGrapheneBlock(nSizeGrapheneBlock);
                graphenedata.UpdateAdditionalTx(grapheneBlock.GetAdditionalTxSerializationSize());
            }
        }
        catch (const std::runtime_error &e)
        {
            pfrom->PushMessage(NetMsgType::BLOCK, *pblock);
            LOG(GRAPHENE,
                "Sent regular block instead - encountered error when creating graphene block for peer %s: %s\n",
                pfrom->GetLogName(), e.what());
        }
    }
    else
    {
        dosMan.Misbehaving(pfrom, 100);

        return;
    }

    pfrom->blocksSent += 1;
}

bool IsGrapheneBlockValid(CNode *pfrom, const CBlockHeader &header)
{
    // check block header
    CValidationState state;
    if (!CheckBlockHeader(header, state, true))
    {
        return error("Received invalid header for graphene block %s from peer %s", header.GetHash().ToString(),
            pfrom->GetLogName());
    }
    if (state.Invalid())
    {
        return error("Received invalid header for graphene block %s from peer %s", header.GetHash().ToString(),
            pfrom->GetLogName());
    }

    return true;
}

bool HandleGrapheneBlockRequest(CDataStream &vRecv, CNode *pfrom, const CChainParams &chainparams)
{
    if (!pfrom->GrapheneCapable())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("Graphene block message received from a non graphene block node, peer=%d", pfrom->GetId());
    }

    // Check for Misbehaving and DOS
    // If they make more than 20 requests in 10 minutes then disconnect them
    {
        LOCK(cs_vNodes);
        if (pfrom->nGetGrapheneLastTime <= 0)
            pfrom->nGetGrapheneLastTime = GetTime();
        uint64_t nNow = GetTime();
        pfrom->nGetGrapheneCount *= std::pow(1.0 - 1.0 / 600.0, (double)(nNow - pfrom->nGetGrapheneLastTime));
        pfrom->nGetGrapheneLastTime = nNow;
        pfrom->nGetGrapheneCount += 1;
        LOG(GRAPHENE, "nGetGrapheneCount is %f\n", pfrom->nGetGrapheneCount);
        if (chainparams.NetworkIDString() == "main") // other networks have variable mining rates
        {
            if (pfrom->nGetGrapheneCount >= 20)
            {
                dosMan.Misbehaving(pfrom, 100); // If they exceed the limit then disconnect them
                return error("sending too many GET_GRAPHENE messages");
            }
        }
    }

    CMemPoolInfo receiverMemPoolInfo;
    CInv inv;
    vRecv >> inv >> receiverMemPoolInfo;
    graphenedata.UpdateInBoundMemPoolInfo(::GetSerializeSize(receiverMemPoolInfo, SER_NETWORK, PROTOCOL_VERSION));

    {
        LOCK(pfrom->cs_ngraphenemempooltx);
        pfrom->nGrapheneMemPoolTx = receiverMemPoolInfo.nTx;
    }

    // Message consistency checking
    if (!(inv.type == MSG_GRAPHENEBLOCK) || inv.hash.IsNull())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("invalid GET_GRAPHENE message type=%u hash=%s", inv.type, inv.hash.ToString());
    }

    CBlock block;
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
        if (mi == mapBlockIndex.end())
        {
            dosMan.Misbehaving(pfrom, 100);
            return error(
                "Peer %s (%d) requested nonexistent block %s", pfrom->addrName.c_str(), pfrom->id, inv.hash.ToString());
        }

        const Consensus::Params &consensusParams = Params().GetConsensus();
        if (!ReadBlockFromDisk(block, (*mi).second, consensusParams))
        {
            // We don't have the block yet, although we know about it.
            return error("Peer %s (%d) requested block %s that cannot be read", pfrom->addrName.c_str(), pfrom->id,
                inv.hash.ToString());
        }
        else
            SendGrapheneBlock(MakeBlockRef(block), pfrom, inv);
    }

    return true;
}

CMemPoolInfo GetGrapheneMempoolInfo() { return CMemPoolInfo(mempool.size()); }
uint256 GetSalt(unsigned char seed)
{
    std::vector<unsigned char> vec(32);
    vec[0] = seed;
    return uint256(vec);
}

void RequestFailoverBlock(CNode *pfrom, uint256 blockHash)
{
    if (IsThinBlocksEnabled() && pfrom->ThinBlockCapable())
    {
        LOG(GRAPHENE, "Requesting xthin block as failover from peer %s\n", pfrom->GetLogName());
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        CBloomFilter filterMemPool;
        CInv inv2(MSG_XTHINBLOCK, blockHash);

        AddThinBlockInFlight(pfrom, inv2.hash);

        std::vector<uint256> vOrphanHashes;
        {
            LOCK(orphanpool.cs);
            for (auto &mi : orphanpool.mapOrphanTransactions)
                vOrphanHashes.emplace_back(mi.first);
        }
        BuildSeededBloomFilter(filterMemPool, vOrphanHashes, inv2.hash, pfrom);
        ss << inv2;
        ss << filterMemPool;
        pfrom->PushMessage(NetMsgType::GET_XTHIN, ss);
    }
    else
    {
        LOG(GRAPHENE, "Requesting full block as failover from peer %s\n", pfrom->GetLogName());
        std::vector<CInv> vGetData;
        vGetData.push_back(CInv(MSG_BLOCK, blockHash));
        pfrom->PushMessage(NetMsgType::GETDATA, vGetData);
    }
}
