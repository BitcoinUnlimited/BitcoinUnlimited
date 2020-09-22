// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockrelay/graphene.h"
#include "blockstorage/blockstorage.h"
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
#include "timedata.h"
#include "txadmission.h"
#include "txmempool.h"
#include "txorphanpool.h"
#include "util.h"
#include "utiltime.h"
#include "validation/validation.h"
#include "xversionkeys.h"

#include <iomanip>
static bool ReconstructBlock(CNode *pfrom,
    std::shared_ptr<CBlockThinRelay> pblock,
    const std::map<uint64_t, CTransactionRef> &mapTxFromPools);
extern CTweak<uint64_t> grapheneFastFilterCompatibility;

CMemPoolInfo::CMemPoolInfo(uint64_t _nTx) : nTx(_nTx) {}
CMemPoolInfo::CMemPoolInfo() { this->nTx = 0; }
CGrapheneBlock::CGrapheneBlock(const CBlockRef pblock,
    uint64_t nReceiverMemPoolTx,
    uint64_t nSenderMempoolPlusBlock,
    uint64_t _version,
    bool _computeOptimized)
    : // Use cryptographically strong pseudorandom number because
      // we will extract SipHash secret key from this
      sipHashNonce(GetRand(std::numeric_limits<uint64_t>::max())),
      nSize(0), nWaitingFor(0), shorttxidk0(0), shorttxidk1(0), version(_version), computeOptimized(_computeOptimized)
{
    header = pblock->GetBlockHeader();
    nBlockTxs = pblock->vtx.size();
    uint64_t grapheneSetVersion = CGrapheneBlock::GetGrapheneSetVersion(version);

    if (version >= 2)
        FillShortTxIDSelector();

    std::vector<uint256> blockHashes;
    for (auto &tx : pblock->vtx)
    {
        blockHashes.push_back(tx->GetHash());

        if (tx->IsCoinBase())
            vAdditionalTxs.push_back(tx);
    }

    if (fCanonicalTxsOrder)
        pGrapheneSet =
            std::make_shared<CGrapheneSet>(CGrapheneSet(nReceiverMemPoolTx, nSenderMempoolPlusBlock, blockHashes,
                shorttxidk0, shorttxidk1, grapheneSetVersion, (uint32_t)sipHashNonce, computeOptimized, false));
    else
        pGrapheneSet = std::make_shared<CGrapheneSet>(CGrapheneSet(nReceiverMemPoolTx, nSenderMempoolPlusBlock,
            blockHashes, shorttxidk0, shorttxidk1, grapheneSetVersion, (uint32_t)sipHashNonce, computeOptimized, true));
}

CGrapheneBlock::~CGrapheneBlock() { pGrapheneSet = nullptr; }
void CGrapheneBlock::FillShortTxIDSelector()
{
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << header << sipHashNonce;
    CSHA256 hasher;
    hasher.Write((unsigned char *)&(*stream.begin()), stream.end() - stream.begin());
    uint256 shorttxidhash;
    hasher.Finalize(shorttxidhash.begin());
    shorttxidk0 = shorttxidhash.GetUint64(0);
    shorttxidk1 = shorttxidhash.GetUint64(1);
}

void CGrapheneBlock::AddNewTransactions(std::vector<CTransaction> vMissingTx, CNode *pfrom)
{
    if (vMissingTx.size() == 0)
        return;

    // If canonical ordering is activated, locate empty indexes in vTxHashes256 to be used in sorting
    std::vector<size_t> missingTxIdxs;
    if (fCanonicalTxsOrder && NegotiateGrapheneVersion(pfrom) >= 1)
    {
        uint256 nullhash;
        for (size_t idx = 0; idx < vTxHashes256.size(); idx++)
        {
            if (vTxHashes256[idx] == nullhash)
                missingTxIdxs.push_back(idx);
        }
    }

    if (vMissingTx.size() != missingTxIdxs.size())
        throw std::runtime_error("Could not accommodate all vMissingTx in vTxHashes256");

    size_t idx = 0;
    for (const CTransaction &tx : vMissingTx)
    {
        mapMissingTx[GetShortID(pfrom->gr_shorttxidk0.load(), pfrom->gr_shorttxidk1.load(), tx.GetHash(),
            NegotiateGrapheneVersion(pfrom))] = MakeTransactionRef(tx);

        uint256 hash = tx.GetHash();
        uint64_t cheapHash = GetShortID(
            pfrom->gr_shorttxidk0.load(), pfrom->gr_shorttxidk1.load(), hash, NegotiateGrapheneVersion(pfrom));

        // Insert in arbitrary order if canonical ordering is enabled and xversion is recent enough
        if (fCanonicalTxsOrder && NegotiateGrapheneVersion(pfrom) >= 1)
        {
            if (idx >= missingTxIdxs.size())
                throw std::runtime_error("Range exceeded in missingTxIdxs");
            vTxHashes256[missingTxIdxs[idx]] = hash;
            idx++;
        }
        // Otherwise, use ordering information
        else
            vTxHashes256[mapHashOrderIndex[cheapHash]] = hash;
    }
}

void CGrapheneBlock::OrderTxHashes(CNode *pfrom)
{
    if (vTxHashes256.size() != nBlockTxs)
    {
        throw std::runtime_error("Cannot OrderTxHashes if size of vTxHashes256 unequal to nBlockTxs");
    }

    // Sort order transactions if canonical order is enabled and graphene version is late enough
    if (fCanonicalTxsOrder && NegotiateGrapheneVersion(pfrom) >= 1)
    {
        // coinbase is always first
        std::sort(vTxHashes256.begin() + 1, vTxHashes256.end());
        LOG(GRAPHENE, "Using canonical order for block from peer=%s\n", pfrom->GetLogName());
    }
    else
    {
        uint256 nullhash;
        std::vector<uint256> orderedTxHashes256(nBlockTxs, nullhash);
        for (auto &hash : vTxHashes256)
        {
            uint64_t cheapHash = GetShortID(
                pfrom->gr_shorttxidk0.load(), pfrom->gr_shorttxidk1.load(), hash, NegotiateGrapheneVersion(pfrom));
            const auto &orderIdx = mapHashOrderIndex.find(cheapHash);
            if (orderIdx == mapHashOrderIndex.end())
                throw std::runtime_error("Could not locate cheapHash in mapHashOrderIndex");
            orderedTxHashes256[orderIdx->second] = hash;
        }
        std::copy(orderedTxHashes256.begin(), orderedTxHashes256.end(), vTxHashes256.begin());
    }
}

bool CGrapheneBlock::ValidateAndRecontructBlock(uint256 blockhash,
    std::shared_ptr<CBlockThinRelay> pblock,
    const std::map<uint64_t, CTransactionRef> &mapCheapHashTx,
    std::string command,
    CNode *pfrom,
    CDataStream &vRecv)
{
    size_t msgSize = vRecv.size();
    OrderTxHashes(pfrom);

    // At this point we should have all the full hashes in the block. Check that the merkle
    // root in the block header matches the merkel root calculated from the hashes provided.
    bool mutated;
    uint256 merkleroot = ComputeMerkleRoot(vTxHashes256, &mutated);
    if (pblock->hashMerkleRoot != merkleroot || mutated)
    {
        thinrelay.ClearAllBlockData(pfrom, pblock->GetHash());
        return error("Merkle root for block %s does not match computed merkle root, peer=%s", blockhash.ToString(),
            pfrom->GetLogName());
    }
    LOG(GRAPHENE, "Merkle Root check passed for block %s peer=%s\n", blockhash.ToString(), pfrom->GetLogName());

    // Look for each transaction in our various pools and buffers.
    // With grapheneBlocks recovered txs contains only the first 8 bytes of the tx hash.
    {
        if (!ReconstructBlock(pfrom, pblock, mapCheapHashTx))
            return false;
    }

    // We have all the transactions now that are in this block: try to reassemble and process.
    CInv inv2(MSG_BLOCK, blockhash);

    // for compression statistics, we have to add up the size of grapheneblock and the re-requested grapheneBlockTx.
    uint64_t nSizeGrapheneBlockTx = msgSize;
    uint64_t blockSize = pblock->GetBlockSize();
    float nCompressionRatio = 0.0;
    if (GetSize() + nSizeGrapheneBlockTx > 0)
        nCompressionRatio = (float)blockSize / ((float)GetSize() + (float)nSizeGrapheneBlockTx);
    LOG(GRAPHENE, "Reassembled grblktx for %s (%d bytes). Message was %d bytes (graphene block) and %d bytes "
                  "(re-requested tx), compression ratio %3.2f, peer=%s\n",
        pblock->GetHash().ToString(), blockSize, GetSize(), nSizeGrapheneBlockTx, nCompressionRatio,
        pfrom->GetLogName());

    // Update run-time statistics of graphene block bandwidth savings.
    // We add the original graphene block size with the size of transactions that were re-requested.
    // This is NOT double counting since we never accounted for the original graphene block due to the re-request.
    graphenedata.UpdateInBound(nSizeGrapheneBlockTx + GetSize(), blockSize);
    LOG(GRAPHENE, "Graphene block stats: %s\n", graphenedata.ToString());

    PV->HandleBlockMessage(pfrom, command, pblock, inv2);

    return true;
}

CGrapheneBlockTx::CGrapheneBlockTx(uint256 blockHash, std::vector<CTransaction> &vTx)
{
    blockhash = blockHash;
    vMissingTx = vTx;
}

bool CGrapheneBlockTx::HandleMessage(CDataStream &vRecv, CNode *pfrom)
{
    std::string strCommand = NetMsgType::GRAPHENETX;
    CGrapheneBlockTx grapheneBlockTx;
    vRecv >> grapheneBlockTx;

    auto pblock = thinrelay.GetBlockToReconstruct(pfrom, grapheneBlockTx.blockhash);
    if (pblock == nullptr)
        return error("No block available to reconstruct for graphenetx");
    DbgAssert(pblock->grapheneblock != nullptr, return false);

    // Message consistency checking
    CInv inv(MSG_GRAPHENEBLOCK, grapheneBlockTx.blockhash);
    if (grapheneBlockTx.vMissingTx.empty())
    {
        // Normal effect if the IBLT decode on the other side completely failed
        std::shared_ptr<CBlockThinRelay> backup = std::make_shared<CBlockThinRelay>(*pblock);
        RequestFailoverBlock(pfrom, backup);
        return error("Incorrectly constructed grblocktx data received, Empty tx set from: %s", pfrom->GetLogName());
    }
    if (grapheneBlockTx.blockhash.IsNull())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error(
            "Incorrectly constructed grblocktx  data received, hash is NULL.  Banning peer=%s", pfrom->GetLogName());
    }

    LOG(GRAPHENE, "Received grblocktx for %s peer=%s\n", inv.hash.ToString(), pfrom->GetLogName());
    {
        // Do not process unrequested grblocktx unless from an expedited node.
        if (!thinrelay.IsBlockInFlight(pfrom, NetMsgType::GRAPHENEBLOCK, inv.hash) &&
            !connmgr->IsExpeditedUpstream(pfrom))
        {
            dosMan.Misbehaving(pfrom, 10);
            return error(
                "Received grblocktx %s from peer %s but was unrequested", inv.hash.ToString(), pfrom->GetLogName());
        }
    }

    // Copy backup block for failover
    std::shared_ptr<CBlockThinRelay> backup = std::make_shared<CBlockThinRelay>(*pblock);

    std::shared_ptr<CGrapheneBlock> grapheneBlock = pblock->grapheneblock;
    if (grapheneBlock->vTxHashes256.size() < grapheneBlockTx.vMissingTx.size())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("Inconsistent graphene block data received.  Banning peer=%s", pfrom->GetLogName());
    }

    // Check if we've already received this block and have it on disk
    if (AlreadyHaveBlock(inv))
    {
        requester.AlreadyReceived(pfrom, inv);
        thinrelay.ClearAllBlockData(pfrom, inv.hash);

        LOG(GRAPHENE, "Received grblocktx but returning because we already have this block %s on disk, peer=%s\n",
            inv.hash.ToString(), pfrom->GetLogName());
        return true;
    }

    // In the rare event of an erroneous checksum during IBLT decoding, the receiver may
    // have requested an invalid cheap hash, and the sender would have simply skipped sending
    // it. In that case, the number of missing txs returned will be fewer than the number
    // needed. Because the graphene block will be incomplete without the missing txs, we
    // request a failover block instead.
    if (grapheneBlockTx.vMissingTx.size() < grapheneBlock->nWaitingFor)
    {
        RequestFailoverBlock(pfrom, backup);
        return error("Still missing transactions from those returned by sender, peer=%s: re-requesting failover block",
            pfrom->GetLogName());
    }

    grapheneBlock->AddNewTransactions(grapheneBlockTx.vMissingTx, pfrom);

    LOG(GRAPHENE, "Got %d Re-requested txs from peer=%s\n", grapheneBlockTx.vMissingTx.size(), pfrom->GetLogName());

    std::map<uint64_t, CTransactionRef> mapPartialTxHash;
    grapheneBlock->FillTxMapFromPools(mapPartialTxHash);

    // Add full transactions included in the block
    for (auto &tx : grapheneBlock->vAdditionalTxs)
    {
        const uint256 &hash = tx->GetHash();
        uint64_t cheapHash = grapheneBlock->pGrapheneSet->GetShortID(hash);
        mapPartialTxHash.insert(std::make_pair(cheapHash, tx));
    }

    // Add full transactions collected during failure recovery
    for (auto &tx : grapheneBlock->vRecoveredTxs)
    {
        const uint256 &hash = tx->GetHash();
        uint64_t cheapHash = grapheneBlock->pGrapheneSet->GetShortID(hash);
        mapPartialTxHash.insert(std::make_pair(cheapHash, tx));
    }

    // Add full transactions from grapheneBlockTx.vMissingTx
    for (auto &tx : grapheneBlockTx.vMissingTx)
    {
        CTransactionRef txRef = MakeTransactionRef(tx);
        const uint256 &hash = tx.GetHash();
        uint64_t cheapHash = grapheneBlock->pGrapheneSet->GetShortID(hash);
        mapPartialTxHash.insert(std::make_pair(cheapHash, txRef));
    }

    if (!grapheneBlock->ValidateAndRecontructBlock(
            grapheneBlockTx.blockhash, pblock, mapPartialTxHash, strCommand, pfrom, vRecv))
    {
        RequestFailoverBlock(pfrom, backup);
        return error("Graphene ValidateAndRecontructBlock failed");
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
    CRequestGrapheneBlockTx grapheneRequestBlockTx;
    vRecv >> grapheneRequestBlockTx;
    uint256 blkHash = grapheneRequestBlockTx.blockhash;

    // Message consistency checking
    if (grapheneRequestBlockTx.setCheapHashesToRequest.empty() || blkHash.IsNull())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("Incorrectly constructed get_grblocktx received.  Banning peer=%s", pfrom->GetLogName());
    }

    LOG(GRAPHENE, "Received get_grblocktx for %s peer=%s\n", blkHash.ToString(), pfrom->GetLogName());

    try
    {
        std::vector<CTransaction> vTx =
            TransactionsFromBlockByCheapHash(grapheneRequestBlockTx.setCheapHashesToRequest, blkHash, pfrom);
        CGrapheneBlockTx grapheneBlockTx(grapheneRequestBlockTx.blockhash, vTx);
        pfrom->PushMessage(NetMsgType::GRAPHENETX, grapheneBlockTx);
        pfrom->txsSent += vTx.size();
        if (vTx.size() == 0)
        {
            LOG(GRAPHENE, "Sent empty grapheneBlockTx.  Requested %d\n",
                grapheneRequestBlockTx.setCheapHashesToRequest.size());
        }
    }
    catch (const std::exception &e)
    {
        return error(GRAPHENE, e.what());
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
    // Deserialize grapheneblock and store a block to reconstruct
    CGrapheneBlock tmp(NegotiateGrapheneVersion(pfrom), NegotiateFastFilterSupport(pfrom));
    vRecv >> tmp;
    auto pblock = thinrelay.SetBlockToReconstruct(pfrom, tmp.header.GetHash());
    pblock->grapheneblock = std::make_shared<CGrapheneBlock>(std::forward<CGrapheneBlock>(tmp));

    std::shared_ptr<CGrapheneBlock> grapheneBlock = pblock->grapheneblock;

    LOG(GRAPHENE, "Block %s from peer %s using Graphene version %d\n", grapheneBlock->header.GetHash().ToString(),
        pfrom->GetLogName(), grapheneBlock->version);

    // Message consistency checking (FIXME: some redundancy here with AcceptBlockHeader)
    if (!IsGrapheneBlockValid(pfrom, grapheneBlock->header))
    {
        dosMan.Misbehaving(pfrom, 100);
        thinrelay.ClearAllBlockData(pfrom, grapheneBlock->header.GetHash());
        return error("Received an invalid %s from peer %s\n", strCommand, pfrom->GetLogName());
    }

    // Is there a previous block or header to connect with?
    if (!LookupBlockIndex(grapheneBlock->header.hashPrevBlock))
    {
        dosMan.Misbehaving(pfrom, 10);
        thinrelay.ClearAllBlockData(pfrom, pblock->GetHash());
        return error(GRAPHENE, "Graphene block from peer %s will not connect, unknown previous block %s",
            pfrom->GetLogName(), grapheneBlock->header.hashPrevBlock.ToString());
    }

    {
        LOCK(cs_main);
        CValidationState state;
        CBlockIndex *pIndex = nullptr;
        if (!AcceptBlockHeader(grapheneBlock->header, state, Params(), &pIndex))
        {
            int nDoS;
            if (state.IsInvalid(nDoS))
            {
                if (nDoS > 0)
                    dosMan.Misbehaving(pfrom, nDoS);
                LOGA("Received an invalid %s header from peer %s\n", strCommand, pfrom->GetLogName());
            }

            thinrelay.ClearAllBlockData(pfrom, grapheneBlock->header.GetHash());
            return false;
        }

        // pIndex should always be set by AcceptBlockHeader
        if (!pIndex)
        {
            LOGA("INTERNAL ERROR: pIndex null in CGrapheneBlock::HandleMessage");
            thinrelay.ClearAllBlockData(pfrom, grapheneBlock->header.GetHash());
            return true;
        }

        CInv inv(MSG_BLOCK, pIndex->GetBlockHash());
        requester.UpdateBlockAvailability(pfrom->GetId(), inv.hash);

        // Return early if we already have the block data
        if (AlreadyHaveBlock(inv))
        {
            // Tell the Request Manager we received this block
            requester.AlreadyReceived(pfrom, inv);

            thinrelay.ClearAllBlockData(pfrom, grapheneBlock->header.GetHash());
            LOG(GRAPHENE, "Received grapheneblock but returning because we already have block data %s from peer %s hop"
                          " %d size %d bytes\n",
                inv.hash.ToString(), pfrom->GetLogName(), nHops, grapheneBlock->GetSize());
            return true;
        }

        // Request full block if this one isn't extending the best chain
        if (pIndex->nChainWork <= chainActive.Tip()->nChainWork)
        {
            thinrelay.RequestBlock(pfrom, inv.hash);
            thinrelay.ClearAllBlockData(pfrom, grapheneBlock->header.GetHash());

            LOGA("%s %s from peer %s received but does not extend longest chain; requesting full block\n", strCommand,
                inv.hash.ToString(), pfrom->GetLogName());
            return true;
        }

        {
            LOG(GRAPHENE, "Received %s %s from peer %s. Size %d bytes.\n", strCommand, inv.hash.ToString(),
                pfrom->GetLogName(), grapheneBlock->GetSize());

            // Do not process unrequested grapheneblocks.
            if (!thinrelay.IsBlockInFlight(pfrom, NetMsgType::GRAPHENEBLOCK, inv.hash))
            {
                dosMan.Misbehaving(pfrom, 10);
                return error(
                    "%s %s from peer %s but was unrequested\n", strCommand, inv.hash.ToString(), pfrom->GetLogName());
            }
        }
    }

    bool result = grapheneBlock->process(pfrom, strCommand, pblock);

    return result;
}

void CGrapheneBlock::FillTxMapFromPools(std::map<uint64_t, CTransactionRef> &mapTxFromPools)
{
    {
        boost::unique_lock<boost::mutex> lock(csCommitQ);
        for (auto &kv : *txCommitQ)
        {
            uint64_t cheapHash = GetShortID(shorttxidk0, shorttxidk1, kv.first, version);
            auto shTx = kv.second.entry.GetSharedTx();
            if (shTx != nullptr)
                mapTxFromPools.insert(std::make_pair(cheapHash, shTx));
        }
    }

    {
        READLOCK(orphanpool.cs_orphanpool);
        for (auto &kv : orphanpool.mapOrphanTransactions)
        {
            uint64_t cheapHash = GetShortID(shorttxidk0, shorttxidk1, kv.first, version);
            auto shTx = kv.second.ptx;
            if (shTx != nullptr)
                mapTxFromPools.insert(std::make_pair(cheapHash, shTx));
        }
    }

    std::vector<uint256> memPoolHashes;
    mempool.queryHashes(memPoolHashes);

    for (const uint256 &hash : memPoolHashes)
    {
        uint64_t cheapHash = GetShortID(shorttxidk0, shorttxidk1, hash, version);
        auto shTx = mempool.get(hash);
        if (shTx != nullptr) // otherwise mempool got updated between the query and this iteration
            mapTxFromPools.insert(std::make_pair(cheapHash, shTx));
    }
}

void CGrapheneBlock::SituateCoinbase(std::vector<uint64_t> blockCheapHashes,
    CTransactionRef coinbase,
    uint64_t grapheneVersion)
{
    // Ensure coinbase is first
    if (blockCheapHashes[0] != GetShortID(shorttxidk0, shorttxidk1, coinbase->GetHash(), version))
    {
        auto it = std::find(blockCheapHashes.begin(), blockCheapHashes.end(),
            GetShortID(shorttxidk0, shorttxidk1, coinbase->GetHash(), version));

        if (it == blockCheapHashes.end())
            throw std::runtime_error("No coinbase transaction found in graphene block");

        auto idx = std::distance(blockCheapHashes.begin(), it);

        blockCheapHashes[idx] = blockCheapHashes[0];
        blockCheapHashes[0] = GetShortID(shorttxidk0, shorttxidk1, coinbase->GetHash(), version);
    }
}

void CGrapheneBlock::SituateCoinbase(CTransactionRef coinbase)
{
    std::vector<uint256>::iterator it = std::find(vTxHashes256.begin(), vTxHashes256.end(), coinbase->GetHash());

    if (it == vTxHashes256.end())
        return;

    std::swap(vTxHashes256[0], vTxHashes256[std::distance(vTxHashes256.begin(), it)]);
}

std::set<uint64_t> CGrapheneBlock::UpdateResolvedTxsAndIdentifyMissing(
    const std::map<uint64_t, CTransactionRef> &mapPartialTxHash,
    const std::vector<uint64_t> &blockCheapHashes,
    uint64_t grapheneVersion)
{
    std::set<uint64_t> setHashesToRequest;
    uint256 nullhash;

    // Sort out what hashes we have from the complete set of cheapHashes
    for (size_t i = 0; i < blockCheapHashes.size(); i++)
    {
        uint64_t cheapHash = blockCheapHashes[i];

        // If canonical order is not enabled or xversion is less than 1, update mapHashOrderIndex so
        // it is available if we later receive missing txs
        if (!fCanonicalTxsOrder || grapheneVersion < 1)
            mapHashOrderIndex[cheapHash] = i;

        const auto &elem = mapPartialTxHash.find(cheapHash);
        if ((elem != mapPartialTxHash.end()) && (elem->second != nullptr))
        {
            const auto repeat = std::find(vTxHashes256.begin(), vTxHashes256.end(), elem->second->GetHash());
            if (repeat == vTxHashes256.end())
                vTxHashes256.push_back(elem->second->GetHash());
        }
        else
        {
            vTxHashes256.push_back(nullhash);
            setHashesToRequest.insert(cheapHash);
        }
    }

    return setHashesToRequest;
}

bool CGrapheneBlock::process(CNode *pfrom, std::string strCommand, std::shared_ptr<CBlockThinRelay> pblock)
{
    // In PV we must prevent two graphene blocks from simulaneously processing that were recieved from the
    // same peer. This would only happen as in the example of an expedited block coming in
    // after an graphene request, because we would never explicitly request two graphene blocks from the same peer.
    if (PV->IsAlreadyValidating(pfrom->id, pblock->GetHash()))
    {
        LOGA("Not processing this grapheneblock from %s because %s is already validating in another thread\n",
            pfrom->GetLogName(), pblock->GetHash().ToString().c_str());
        return false;
    }

    DbgAssert(pblock->grapheneblock != nullptr, return false);
    DbgAssert(pblock->grapheneblock.get() == this, return false);
    std::shared_ptr<CGrapheneBlock> grapheneBlock = pblock->grapheneblock;

    pblock->nVersion = header.nVersion;
    pblock->nBits = header.nBits;
    pblock->nNonce = header.nNonce;
    pblock->nTime = header.nTime;
    pblock->hashMerkleRoot = header.hashMerkleRoot;
    pblock->hashPrevBlock = header.hashPrevBlock;
    pfrom->gr_shorttxidk0.store(shorttxidk0);
    pfrom->gr_shorttxidk1.store(shorttxidk1);

    // Create a map of all 8 bytes tx hashes pointing to their full tx hash counterpart
    bool fRequestFailureRecovery = false;
    std::set<uint256> passingTxHashes;
    std::map<uint64_t, CTransactionRef> mapPartialTxHash;
    std::set<uint64_t> setHashesToRequest;
    std::vector<uint256> vSenderFilterPositiveHahses;

    bool fMerkleRootCorrect = true;
    {
        FillTxMapFromPools(mapPartialTxHash);

        // Add full transactions included in the block
        CTransactionRef coinbase = nullptr;
        for (auto &tx : vAdditionalTxs)
        {
            const uint256 &hash = tx->GetHash();
            uint64_t cheapHash = GetShortID(shorttxidk0, shorttxidk1, hash, version);
            mapPartialTxHash.insert(std::make_pair(cheapHash, tx));

            if (tx->IsCoinBase())
                coinbase = tx;
        }

        if (coinbase == nullptr)
        {
            LOG(GRAPHENE, "Error: No coinbase transaction found in graphene block, peer=%s", pfrom->GetLogName());
            return false;
        }

        try
        {
            std::set<uint64_t> setSenderFilterPositiveCheapHashes;

            // Populate tx hash array and cheap hash set for use by Graphene.
            // Do it outside of CGrapheneSet so that we can reuse the tx hashes
            // if failure recovery is necessary.
            bool grSetComputeOpt = pGrapheneSet->GetComputeOptimized();
            for (const auto &entry : mapPartialTxHash)
            {
                auto txptr = entry.second;
                if (entry.second == nullptr)
                {
                    LOG(GRAPHENE, "Error: Empty transaction in mapPartialTxHash");
                }
                else
                {
                    if ((grSetComputeOpt && pGrapheneSet->GetFastFilter()->contains(entry.second->GetHash())) ||
                        (!grSetComputeOpt && pGrapheneSet->GetRegularFilter()->contains(entry.second->GetHash())))
                    {
                        setSenderFilterPositiveCheapHashes.insert(entry.first);
                        vSenderFilterPositiveHahses.push_back(entry.second->GetHash());
                    }
                }
            }

            std::vector<uint64_t> blockCheapHashes = pGrapheneSet->Reconcile(setSenderFilterPositiveCheapHashes);
            setHashesToRequest = grapheneBlock->UpdateResolvedTxsAndIdentifyMissing(
                mapPartialTxHash, blockCheapHashes, NegotiateGrapheneVersion(pfrom));
            grapheneBlock->SituateCoinbase(coinbase);

            // Sort order transactions if canonical order is enabled and graphene version is late enough
            if (fCanonicalTxsOrder && NegotiateGrapheneVersion(pfrom) >= 1)
            {
                // coinbase is always first
                std::sort(grapheneBlock->vTxHashes256.begin() + 1, grapheneBlock->vTxHashes256.end());
                LOG(GRAPHENE, "Using canonical order for block from peer=%s\n", pfrom->GetLogName());
            }
        }
        catch (const std::runtime_error &e)
        {
            fRequestFailureRecovery = true;
            graphenedata.IncrementDecodeFailures();
            if (version >= 6)
            {
                LOG(GRAPHENE, "Graphene set could not be reconciled; requesting recovery from peer %s: %s\n",
                    pfrom->GetLogName(), e.what());
            }
            else
            {
                LOG(GRAPHENE, "Graphene set could not be reconciled; requesting failover for peer %s: %s\n",
                    pfrom->GetLogName(), e.what());
            }
        }

        // Reconstruct the block if there are no hashes to re-request
        if (setHashesToRequest.empty() && !fRequestFailureRecovery)
        {
            bool mutated;
            uint256 merkleroot = ComputeMerkleRoot(grapheneBlock->vTxHashes256, &mutated);
            if (header.hashMerkleRoot != merkleroot || mutated)
                fMerkleRootCorrect = false;
            else
            {
                if (!ReconstructBlock(pfrom, pblock, mapPartialTxHash))
                    return false;
            }
        }

    } // End locking cs_orphancache, mempool.cs
    LOG(GRAPHENE, "Current in-memory graphene bytes size is %ld bytes\n", pblock->nCurrentBlockSize);

    // This must be checked outside of the above section or deadlock may occur.
    if (fRequestFailureRecovery)
    {
        RequestFailureRecovery(pfrom, grapheneBlock, vSenderFilterPositiveHahses);
        return true;
    }

    // These must be checked outside of the mempool.cs lock or deadlock may occur.
    // A merkle root mismatch here does not cause a ban because and expedited node will forward an graphene
    // without checking the merkle root, therefore we don't want to ban our expedited nodes. Just request
    // a failover block if a mismatch occurs.
    if (!fMerkleRootCorrect)
    {
        RequestFailoverBlock(pfrom, pblock);
        return error(
            "Mismatched merkle root on grapheneblock: requesting failover block, peer=%s", pfrom->GetLogName());
    }

    this->nWaitingFor = setHashesToRequest.size();
    LOG(GRAPHENE, "Graphene block waiting for: %d, total txns: %d received txns: %d\n", this->nWaitingFor,
        pblock->vtx.size(), grapheneBlock->mapMissingTx.size());

    // If there are any missing hashes or transactions then we request them here.
    // This must be done outside of the mempool.cs lock or may deadlock.
    if (setHashesToRequest.size() > 0)
    {
        grapheneBlock->nWaitingFor = setHashesToRequest.size();
        CRequestGrapheneBlockTx grapheneBlockTx(header.GetHash(), setHashesToRequest);
        pfrom->PushMessage(NetMsgType::GET_GRAPHENETX, grapheneBlockTx);

        // Update run-time statistics of graphene block bandwidth savings
        graphenedata.UpdateInBoundReRequestedTx(grapheneBlock->nWaitingFor);

        return true;
    }

    // We now have all the transactions that are in this block
    grapheneBlock->nWaitingFor = 0;
    int blockSize = pblock->GetBlockSize();
    float nCompressionRatio = 0.0;
    if (grapheneBlock->GetSize() > 0)
        nCompressionRatio = (float)blockSize / (float)grapheneBlock->GetSize();
    LOG(GRAPHENE,
        "Reassembled graphene block for %s (%d bytes). Message was %d bytes, compression ratio %3.2f, peer=%s\n",
        pblock->GetHash().ToString(), blockSize, grapheneBlock->GetSize(), nCompressionRatio, pfrom->GetLogName());

    // Update run-time statistics of graphene block bandwidth savings
    graphenedata.UpdateInBound(grapheneBlock->GetSize(), blockSize);
    LOG(GRAPHENE, "Graphene block stats: %s\n", graphenedata.ToString().c_str());

    // Process the full block
    PV->HandleBlockMessage(pfrom, strCommand, pblock, GetInv());

    return true;
}

static bool ReconstructBlock(CNode *pfrom,
    std::shared_ptr<CBlockThinRelay> pblock,
    const std::map<uint64_t, CTransactionRef> &mapTxFromPools)
{
    std::shared_ptr<CGrapheneBlock> grapheneBlock = pblock->grapheneblock;

    // We must have all the full tx hashes by this point.  We first check for any repeating
    // sequences in transaction id's.  This is a possible attack vector and has been used in the past.
    {
        std::set<uint256> setHashes(grapheneBlock->vTxHashes256.begin(), grapheneBlock->vTxHashes256.end());
        if (setHashes.size() != grapheneBlock->vTxHashes256.size())
        {
            thinrelay.ClearAllBlockData(pfrom, grapheneBlock->header.GetHash());
            return error("Repeating Transaction Id sequence, peer=%s", pfrom->GetLogName());
        }
    }

    // Add the header size to the current size being tracked
    thinrelay.AddBlockBytes(::GetSerializeSize(pblock->GetBlockHeader(), SER_NETWORK, PROTOCOL_VERSION), pblock);

    // If we have incomplete infomation about this block, resize the block transaction count to accomodate new data
    if (pblock->vtx.size() < grapheneBlock->vTxHashes256.size())
        pblock->vtx.resize(grapheneBlock->vTxHashes256.size());

    // Collect hashes of txs that will need to be verified
    std::set<uint256> toVerify;
    {
        READLOCK(orphanpool.cs_orphanpool);
        for (auto &kv : orphanpool.mapOrphanTransactions)
        {
            toVerify.insert(kv.first);
        }
    }
    for (auto &tx : grapheneBlock->vAdditionalTxs)
    {
        toVerify.insert(tx->GetHash());
    }
    for (auto &tx : grapheneBlock->vRecoveredTxs)
    {
        toVerify.insert(tx->GetHash());
    }
    for (auto &kv : grapheneBlock->mapMissingTx)
    {
        toVerify.insert(kv.second->GetHash());
    }

    // Locate each transaction in pre-populated mapTxFromPools.
    int idx = -1;
    CTransactionRef ptx = nullptr;
    for (const uint256 &hash : grapheneBlock->vTxHashes256)
    {
        idx++;
        uint64_t nShortId = GetShortID(
            pfrom->gr_shorttxidk0.load(), pfrom->gr_shorttxidk1.load(), hash, NegotiateGrapheneVersion(pfrom));
        const auto iter = mapTxFromPools.find(nShortId);

        if ((iter != mapTxFromPools.end()) && (iter->second != nullptr))
        {
            ptx = iter->second;
            pblock->vtx[idx] = ptx;
        }
        else
        {
            thinrelay.ClearAllBlockData(pfrom, grapheneBlock->header.GetHash());
            return error("Malformed mapTxFromPools, null transaction reference found, peer=%s", pfrom->GetLogName());
        }

        // XVal: these transactions still need to be verified since they were not in the mempool
        // or CommitQ.
        if (toVerify.count(hash) > 0)
            pblock->setUnVerifiedTxns.insert(hash);

        // In order to prevent a memory exhaustion attack we track transaction bytes used to recreate the block
        // in order to see if we've exceeded any limits and if so clear out data and return.
        thinrelay.AddBlockBytes(ptx->GetTxSize(), pblock);
        if (pblock->nCurrentBlockSize > thinrelay.GetMaxAllowedBlockSize())
        {
            uint64_t nBlockBytes = pblock->nCurrentBlockSize;
            thinrelay.ClearAllBlockData(pfrom, grapheneBlock->header.GetHash());
            pfrom->fDisconnect = true;
            return error(
                "Reconstructed block %s (size:%llu) has caused max memory limit %llu bytes to be exceeded, peer=%s",
                pblock->GetHash().ToString(), nBlockBytes, thinrelay.GetMaxAllowedBlockSize(), pfrom->GetLogName());
        }
    }

    // Now that we've rebuilt the block successfully we can set the XVal flag which is used in
    // ConnectBlock() to determine which if any inputs we can skip the checking of inputs.
    pblock->fXVal = true;

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

double CGrapheneBlockData::computeTotalBandwidthSavingsInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_graphenestats)
{
    AssertLockHeld(cs_graphenestats);

    return double(nOriginalSize() - nGrapheneSize() - nTotalMemPoolInfoBytes());
}

double CGrapheneBlockData::compute24hAverageCompressionInternal(
    std::map<int64_t, std::pair<uint64_t, uint64_t> > &mapGrapheneBlocks,
    std::map<int64_t, uint64_t> &mapMemPoolInfo) EXCLUSIVE_LOCKS_REQUIRED(cs_graphenestats)
{
    AssertLockHeld(cs_graphenestats);

    expireStats(mapGrapheneBlocks);
    expireStats(mapMemPoolInfo);

    double nCompressionRate = 0;
    uint64_t nGrapheneSizeTotal = 0;
    uint64_t nOriginalSizeTotal = 0;
    for (const auto &mi : mapGrapheneBlocks)
    {
        nGrapheneSizeTotal += mi.second.first;
        nOriginalSizeTotal += mi.second.second;
    }
    // We count up the CMemPoolInfo sizes from the opposite direction as the blocks.
    // Outbound CMemPoolInfo sizes go with Inbound graphene blocks and vice versa.
    uint64_t nMemPoolInfoSize = 0;
    for (const auto &mi : mapMemPoolInfo)
    {
        nMemPoolInfoSize += mi.second;
    }

    if (nOriginalSizeTotal > 0)
        nCompressionRate = 100 - (100 * (double)(nGrapheneSizeTotal + nMemPoolInfoSize) / nOriginalSizeTotal);

    return nCompressionRate;
}

double CGrapheneBlockData::compute24hInboundRerequestTxPercentInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_graphenestats)
{
    AssertLockHeld(cs_graphenestats);

    expireStats(mapGrapheneBlocksInBoundReRequestedTx);
    expireStats(mapGrapheneBlocksInBound);

    double nReRequestRate = 0;
    uint64_t nTotalReRequests = 0;
    uint64_t nTotalReRequestedTxs = 0;
    for (const auto &mi : mapGrapheneBlocksInBoundReRequestedTx)
    {
        nTotalReRequests += 1;
        nTotalReRequestedTxs += mi.second;
    }

    if (mapGrapheneBlocksInBound.size() > 0)
        nReRequestRate = 100 * (double)nTotalReRequests / mapGrapheneBlocksInBound.size();

    return nReRequestRate;
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
    double size = computeTotalBandwidthSavingsInternal();
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

    double nCompressionRate = compute24hAverageCompressionInternal(mapGrapheneBlocksInBound, mapMemPoolInfoOutBound);

    // NOTE: Potential gotcha, compute24hInboundCompressionInternal has a side-effect of calling
    //       expireStats which modifies the contents of mapGrapheneBlocksInBound
    // We currently rely on this side-effect for the string produced below
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Compression for " << mapGrapheneBlocksInBound.size()
       << " Inbound graphene blocks (last 24hrs): " << nCompressionRate << "%";

    return ss.str();
}

// Calculate the graphene percentage compression over the last 24 hours
std::string CGrapheneBlockData::OutBoundPercentToString()
{
    LOCK(cs_graphenestats);

    double nCompressionRate = compute24hAverageCompressionInternal(mapGrapheneBlocksOutBound, mapMemPoolInfoInBound);

    // NOTE: Potential gotcha, compute24hOutboundCompressionInternal has a side-effect of calling
    //       expireStats which modifies the contents of mapGrapheneBlocksOutBound
    // We currently rely on this side-effect for the string produced below
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

    expireStats(mapGrapheneBlockResponseTime);

    std::vector<double> vResponseTime;

    double nResponseTimeAverage = 0;
    double nPercentile = 0;
    double nTotalResponseTime = 0;
    double nTotalEntries = 0;
    for (const auto &mi : mapGrapheneBlockResponseTime)
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

// Calculate the graphene average block validation time over the last 24 hours
std::string CGrapheneBlockData::ValidationTimeToString()
{
    LOCK(cs_graphenestats);
    expireStats(mapGrapheneBlockValidationTime);

    std::vector<double> vValidationTime;
    double nValidationTimeAverage = 0;
    double nPercentile = 0;
    double nTotalValidationTime = 0;
    double nTotalEntries = 0;
    for (const auto &mi : mapGrapheneBlockValidationTime)
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

// Calculate the graphene average tx re-requested ratio over the last 24 hours
std::string CGrapheneBlockData::ReRequestedTxToString()
{
    LOCK(cs_graphenestats);
    double nReRequestRate = compute24hInboundRerequestTxPercentInternal();

    // NOTE: Potential gotcha, compute24hInboundRerequestTxPercentInternal has a side-effect of calling
    //       expireStats which modifies the contents of mapGrapheneBlocksInBoundReRequestedTx
    // We currently rely on this side-effect for the string produced below
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Tx re-request rate (last 24hrs): " << nReRequestRate
       << "% Total re-requests:" << mapGrapheneBlocksInBoundReRequestedTx.size();
    return ss.str();
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

void CGrapheneBlockData::FillGrapheneQuickStats(GrapheneQuickStats &stats)
{
    if (!IsGrapheneBlockEnabled())
        return;

    LOCK(cs_graphenestats);
    stats.nTotalInbound = nInBoundBlocks();
    stats.nTotalOutbound = nOutBoundBlocks();
    stats.nTotalDecodeFailures = nDecodeFailures();
    stats.nTotalBandwidthSavings = computeTotalBandwidthSavingsInternal();

    // NOTE: The following calls rely on the side-effect of the compute*Internal
    //       calls also calling expireStats on the associated statistics maps
    //       This is why we set the % value first, then the count second for compression values
    stats.fLast24hInboundCompression =
        compute24hAverageCompressionInternal(mapGrapheneBlocksInBound, mapMemPoolInfoOutBound);
    stats.nLast24hInbound = mapGrapheneBlocksInBound.size();
    stats.fLast24hOutboundCompression =
        compute24hAverageCompressionInternal(mapGrapheneBlocksOutBound, mapMemPoolInfoInBound);
    stats.nLast24hOutbound = mapGrapheneBlocksOutBound.size();
    stats.fLast24hRerequestTxPercent = compute24hInboundRerequestTxPercentInternal();
    stats.nLast24hRerequestTx = mapGrapheneBlocksInBoundReRequestedTx.size();
}

bool IsGrapheneBlockEnabled() { return GetBoolArg("-use-grapheneblocks", DEFAULT_USE_GRAPHENE_BLOCKS); }
void SendGrapheneBlock(CBlockRef pblock, CNode *pfrom, const CInv &inv, const CMemPoolInfo &mempoolinfo)
{
    if (inv.type == MSG_GRAPHENEBLOCK)
    {
        try
        {
            uint64_t nSenderMempoolPlusBlock =
                GetGrapheneMempoolInfo().nTx + pblock->vtx.size() - 1; // exclude coinbase

            CGrapheneBlock grapheneBlock(pblock, mempoolinfo.nTx, nSenderMempoolPlusBlock,
                NegotiateGrapheneVersion(pfrom), NegotiateFastFilterSupport(pfrom));

            LOG(GRAPHENE, "Block %s to peer %s using Graphene version %d\n", grapheneBlock.header.GetHash().ToString(),
                pfrom->GetLogName(), grapheneBlock.version);

            pfrom->gr_shorttxidk0.store(grapheneBlock.shorttxidk0);
            pfrom->gr_shorttxidk1.store(grapheneBlock.shorttxidk1);
            uint64_t nSizeBlock = pblock->GetBlockSize();
            uint64_t nSizeGrapheneBlock = grapheneBlock.GetSize();

            // If graphene block is larger than a regular block then send a regular block instead
            if (nSizeGrapheneBlock > nSizeBlock)
            {
                pfrom->PushMessage(NetMsgType::BLOCK, *pblock);
                LOG(GRAPHENE, "Sent regular block instead - graphene block size: %d vs block size: %d => peer: %s\n",
                    nSizeGrapheneBlock, nSizeBlock, pfrom->GetLogName());
            }
            else
            {
                graphenedata.UpdateOutBound(nSizeGrapheneBlock, nSizeBlock);
                pfrom->PushMessage(NetMsgType::GRAPHENEBLOCK, grapheneBlock);

                // First add transaction hashes to local graphene block
                for (auto &tx : pblock->vtx)
                {
                    grapheneBlock.vTxHashes256.push_back(tx->GetHash());
                }
                // Next store graphene block in case receiver attempts failure recovery
                thinrelay.SetSentGrapheneBlocks(pfrom->GetId(), grapheneBlock);
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
    CMemPoolInfo mempoolinfo;
    CInv inv;
    vRecv >> inv >> mempoolinfo;
    graphenedata.UpdateInBoundMemPoolInfo(::GetSerializeSize(mempoolinfo, SER_NETWORK, PROTOCOL_VERSION));

    // Message consistency checking
    if (inv.hash.IsNull())
    {
        dosMan.Misbehaving(pfrom, 100);
        return error("invalid GET_GRAPHENE message type=%u hash=%s", inv.type, inv.hash.ToString());
    }

    CBlock block;
    {
        auto *hdr = LookupBlockIndex(inv.hash);
        if (!hdr)
            return error("Peer %s requested nonexistent block %s", pfrom->GetLogName(), inv.hash.ToString());

        const Consensus::Params &consensusParams = Params().GetConsensus();
        if (!ReadBlockFromDisk(block, hdr, consensusParams))
        {
            // We don't have the block yet, although we know about it.
            return error("Peer %s requested block %s that cannot be read", pfrom->GetLogName(), inv.hash.ToString());
        }
        else
            SendGrapheneBlock(MakeBlockRef(block), pfrom, inv, mempoolinfo);
    }

    return true;
}

bool HandleGrapheneBlockRecoveryRequest(CDataStream &vRecv, CNode *pfrom, const CChainParams &chainparams)
{
    CRequestGrapheneReceiverRecover recoveryRequest;
    vRecv >> recoveryRequest;

    std::shared_ptr<CGrapheneBlock> grapheneBlock = thinrelay.GetSentGrapheneBlocks(pfrom->GetId());
    if (!grapheneBlock)
        return error("No block available to reconstruct for get_grrec");

    // We had a block stored but it was the wrong one
    if (grapheneBlock->header.GetHash() != recoveryRequest.blockhash)
        return error("Sender does not have block for requested hash");

    CGrapheneReceiverRecover recoveryResponse = CGrapheneReceiverRecover(
        *recoveryRequest.pReceiverFilter, *grapheneBlock, recoveryRequest.nSenderFilterPositives, pfrom);
    pfrom->PushMessage(NetMsgType::GRAPHENE_RECOVERY, recoveryResponse);

    return true;
}

bool HandleGrapheneBlockRecoveryResponse(CDataStream &vRecv, CNode *pfrom, const CChainParams &chainparams)
{
    CGrapheneReceiverRecover recoveryResponse;
    vRecv >> recoveryResponse;

    auto pblock = thinrelay.GetBlockToReconstruct(pfrom, recoveryResponse.blockhash);
    if (pblock == nullptr)
        return error("No block available to reconstruct for grrec");
    DbgAssert(pblock->grapheneblock != nullptr, return false);
    CGrapheneBlock grapheneBlock = *(pblock->grapheneblock);

    CIblt localIblt((*recoveryResponse.pRevisedIblt));
    localIblt.reset();

    // Initialize map with txs from various pools
    std::map<uint64_t, CTransactionRef> mapTxFromPools;
    pblock->grapheneblock->FillTxMapFromPools(mapTxFromPools);

    // Insert additional txs and identify coinbase
    CTransactionRef coinbase = nullptr;
    for (auto &tx : pblock->grapheneblock->vAdditionalTxs)
    {
        const uint256 &hash = tx->GetHash();
        uint64_t cheapHash = pblock->grapheneblock->pGrapheneSet->GetShortID(hash);

        mapTxFromPools.insert(std::make_pair(cheapHash, tx));

        if (tx->IsCoinBase())
            coinbase = tx;
    }

    if (coinbase == nullptr)
    {
        LOG(GRAPHENE, "Error: No coinbase transaction found in graphene block, peer=%s", pfrom->GetLogName());
        return false;
    }

    // Insert latest transactions just sent over
    for (auto &tx : recoveryResponse.vMissingTxs)
    {
        const uint256 &hash = tx.GetHash();
        uint64_t cheapHash = pblock->grapheneblock->pGrapheneSet->GetShortID(hash);

        CTransactionRef txRef = MakeTransactionRef(tx);
        mapTxFromPools.insert(std::make_pair(cheapHash, txRef));
        pblock->grapheneblock->mapMissingTx[cheapHash] = txRef;
        // Used during reconstruction if other txs need to be rerequested
        pblock->grapheneblock->vRecoveredTxs.insert(txRef);
    }

    // Determine which txs pass filter and populate IBLT
    std::set<uint64_t> setSenderFilterPositiveCheapHashes;
    for (auto &pair : mapTxFromPools)
    {
        if ((pblock->grapheneblock->pGrapheneSet->GetComputeOptimized() &&
                pblock->grapheneblock->pGrapheneSet->GetFastFilter()->contains(pair.second->GetHash())) ||
            (!pblock->grapheneblock->pGrapheneSet->GetComputeOptimized() &&
                pblock->grapheneblock->pGrapheneSet->GetRegularFilter()->contains(pair.second->GetHash())))
        {
            localIblt.insert(pair.first, IBLT_NULL_VALUE);
            setSenderFilterPositiveCheapHashes.insert(pair.first);
        }
    }

    // Attempt to reconcile IBLT
    static std::vector<uint64_t> blockCheapHashes;
    try
    {
        blockCheapHashes = CGrapheneSet::Reconcile(setSenderFilterPositiveCheapHashes, localIblt,
            recoveryResponse.pRevisedIblt, pblock->grapheneblock->pGrapheneSet->GetEncodedRank(),
            pblock->grapheneblock->pGrapheneSet->GetOrdered());
    }
    catch (const std::runtime_error &error)
    {
        // Graphene set still could not be reconciled
        LOG(GRAPHENE, "Could not reconcile failure recovery Graphene set from peer=%s; requesting failover block\n",
            pfrom->GetLogName());
        RequestFailoverBlock(pfrom, pblock);
        return true;
    }

    LOG(GRAPHENE, "Successfully reconciled failure recovery Graphene set from peer=%s\n", pfrom->GetLogName());

    std::set<uint64_t> setHashesToRequest = pblock->grapheneblock->UpdateResolvedTxsAndIdentifyMissing(
        mapTxFromPools, blockCheapHashes, NegotiateGrapheneVersion(pfrom));
    pblock->grapheneblock->SituateCoinbase(coinbase);

    // If there are missing transactions, we must request them here
    if (setHashesToRequest.size() > 0)
    {
        pblock->grapheneblock->nWaitingFor = setHashesToRequest.size();
        CRequestGrapheneBlockTx grapheneBlockTx(recoveryResponse.blockhash, setHashesToRequest);
        pfrom->PushMessage(NetMsgType::GET_GRAPHENETX, grapheneBlockTx);

        // Update run-time statistics of graphene block bandwidth savings
        graphenedata.UpdateInBoundReRequestedTx(grapheneBlock.nWaitingFor);

        return true;
    }

    if (!pblock->grapheneblock->ValidateAndRecontructBlock(
            recoveryResponse.blockhash, pblock, mapTxFromPools, NetMsgType::GRAPHENE_RECOVERY, pfrom, vRecv))
    {
        RequestFailoverBlock(pfrom, pblock);
        return error("Graphene ValidateAndRecontructBlock failed");
    }

    return true;
}

CRequestGrapheneReceiverRecover::CRequestGrapheneReceiverRecover(std::vector<uint256> &relevantHashes,
    CGrapheneBlock &grapheneBlock,
    uint64_t _nSenderFilterPositives)
{
    uint64_t grapheneSetVersion = CGrapheneBlock::GetGrapheneSetVersion(GRAPHENE_MAX_VERSION_SUPPORTED);
    nSenderFilterPositives = _nSenderFilterPositives;
    blockhash = grapheneBlock.header.GetHash();
    uint64_t nReceiverUniverseItems = (uint64_t)std::max(_nSenderFilterPositives,
        GetGrapheneMempoolInfo().nTx); // _nSenderFilterPositives could be larger when it contains the coinbase
    uint64_t nItems = grapheneBlock.nBlockTxs;
    pReceiverFilter = std::make_shared<CVariableFastFilter>(
        grapheneBlock.pGrapheneSet->FailureRecoveryFilter(relevantHashes, nItems, nSenderFilterPositives,
            nReceiverUniverseItems, FAILURE_RECOVERY_SUCCESS_RATE, grapheneBlock.fpr, grapheneSetVersion));

    graphenedata.UpdateFilter(::GetSerializeSize(*pReceiverFilter, SER_NETWORK, PROTOCOL_VERSION));
}

CGrapheneReceiverRecover::CGrapheneReceiverRecover(CVariableFastFilter &receiverFilter,
    CGrapheneBlock &grapheneBlock,
    uint64_t nSenderFilterPositiveItems,
    CNode *pfrom)
{
    blockhash = grapheneBlock.header.GetHash();
    uint64_t grapheneSetVersion = CGrapheneBlock::GetGrapheneSetVersion(GRAPHENE_MAX_VERSION_SUPPORTED);
    uint64_t nReceiverUniverseItems = grapheneBlock.pGrapheneSet->GetNReceiverUniverseItems();
    uint64_t nItems = grapheneBlock.nBlockTxs;

    std::vector<uint256> vMissingTxIds;
    std::set<uint64_t> vAllCheapHashes;
    std::set<uint64_t> vMissingCheapHashes;
    for (auto &hash : grapheneBlock.vTxHashes256)
    {
        if (!receiverFilter.contains(hash))
            vMissingTxIds.push_back(hash);
        else
            vMissingCheapHashes.insert(grapheneBlock.pGrapheneSet->GetShortID(hash));

        vAllCheapHashes.insert(grapheneBlock.pGrapheneSet->GetShortID(hash));
    }

    pRevisedIblt = std::make_shared<CIblt>(grapheneBlock.pGrapheneSet->FailureRecoveryIblt(vAllCheapHashes, nItems,
        nSenderFilterPositiveItems, nReceiverUniverseItems, FAILURE_RECOVERY_SUCCESS_RATE, grapheneBlock.fpr,
        grapheneSetVersion, (uint32_t)grapheneBlock.shorttxidk0));
    std::vector<CTransaction> vTx = TransactionsFromBlockByCheapHash(vMissingCheapHashes, blockhash, pfrom);
    std::copy(vTx.begin(), vTx.end(), back_inserter(vMissingTxs));

    graphenedata.UpdateIblt(::GetSerializeSize(*pRevisedIblt, SER_NETWORK, PROTOCOL_VERSION));
}

CMemPoolInfo GetGrapheneMempoolInfo()
{
    // We need the number of transactions in the mempool and orphanpools but also the number
    // in the txCommitQ that have been processed and valid, and which will be in the mempool shortly.
    uint64_t nCommitQ = 0;
    {
        boost::unique_lock<boost::mutex> lock(csCommitQ);
        nCommitQ = txCommitQ->size();
    }
    return CMemPoolInfo(mempool.size() + orphanpool.GetOrphanPoolSize() + nCommitQ);
}

void RequestFailureRecovery(CNode *pfrom,
    std::shared_ptr<CGrapheneBlock> grapheneBlock,
    std::vector<uint256> vSenderFilterPositiveHahses)
{
    CRequestGrapheneReceiverRecover recoveryRequest = CRequestGrapheneReceiverRecover(
        vSenderFilterPositiveHahses, *grapheneBlock, vSenderFilterPositiveHahses.size());

    pfrom->PushMessage(NetMsgType::GET_GRAPHENE_RECOVERY, recoveryRequest);
}

void RequestFailoverBlock(CNode *pfrom, std::shared_ptr<CBlockThinRelay> pblock)
{
    // Since we were unable process this graphene block then clear out the data and the graphene
    // block in flight making sure to get the blockhash before you clear all the data.
    //
    // This must be done before we request the failover block otherwise it will still appear
    // as though we have a graphene block in flight, which could prevent us from receiving
    // the new thinblock or compactblock, if such is requested.
    uint256 blockhash = pblock->GetHash();
    thinrelay.ClearAllBlockData(pfrom, blockhash);

    if (IsThinBlocksEnabled() && pfrom->ThinBlockCapable())
    {
        if (!thinrelay.AddBlockInFlight(pfrom, blockhash, NetMsgType::XTHINBLOCK))
            return;

        LOG(GRAPHENE | THIN, "Requesting xthinblock %s as failover from peer %s\n", blockhash.ToString(),
            pfrom->GetLogName());
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        CBloomFilter filterMemPool;
        CInv inv(MSG_XTHINBLOCK, blockhash);

        std::vector<uint256> vOrphanHashes;
        {
            READLOCK(orphanpool.cs_orphanpool);
            for (auto &mi : orphanpool.mapOrphanTransactions)
                vOrphanHashes.emplace_back(mi.first);
        }
        BuildSeededBloomFilter(filterMemPool, vOrphanHashes, inv.hash, pfrom);
        ss << inv;
        ss << filterMemPool;
        pfrom->PushMessage(NetMsgType::GET_XTHIN, ss);
    }
    else if (IsCompactBlocksEnabled() && pfrom->CompactBlockCapable())
    {
        if (!thinrelay.AddBlockInFlight(pfrom, blockhash, NetMsgType::CMPCTBLOCK))
            return;

        LOG(GRAPHENE | CMPCT, "Requesting a compactblock %s as failover from peer %s\n", blockhash.ToString(),
            pfrom->GetLogName());
        CInv inv(MSG_CMPCT_BLOCK, blockhash);
        std::vector<CInv> vGetData;
        vGetData.push_back(inv);
        pfrom->PushMessage(NetMsgType::GETDATA, vGetData);
    }
    else
    {
        LOG(GRAPHENE, "Requesting full block %s as failover from peer %s\n", blockhash.ToString(), pfrom->GetLogName());
        thinrelay.RequestBlock(pfrom, blockhash);
    }
}

std::vector<CTransaction> TransactionsFromBlockByCheapHash(std::set<uint64_t> &vCheapHashes,
    uint256 blockhash,
    CNode *pfrom)
{
    std::vector<CTransaction> vTx;
    CBlockIndex *hdr = LookupBlockIndex(blockhash);
    if (!hdr)
    {
        dosMan.Misbehaving(pfrom, 20);
        throw std::runtime_error("Requested block is not available");
    }
    else
    {
        if (hdr->nHeight < (chainActive.Tip()->nHeight - (int)thinrelay.MAX_THINTYPE_BLOCKS_IN_FLIGHT))
            throw std::runtime_error("get_grblocktx request too far from the tip");

        CBlock block;
        const Consensus::Params &consensusParams = Params().GetConsensus();
        if (!ReadBlockFromDisk(block, hdr, consensusParams))
        {
            // We do not assign misbehavior for not being able to read a block from disk because we already
            // know that the block is in the block index from the step above. Secondly, a failure to read may
            // be our own issue or the remote peer's issue in requesting too early.  We can't know at this point.
            throw std::runtime_error(
                "Cannot load block from disk -- Block txn request possibly received before assembled");
        }
        else
        {
            for (auto &tx : block.vtx)
            {
                uint64_t cheapHash = GetShortID(pfrom->gr_shorttxidk0.load(), pfrom->gr_shorttxidk1.load(),
                    tx->GetHash(), NegotiateGrapheneVersion(pfrom));

                if (vCheapHashes.count(cheapHash))
                    vTx.push_back(*tx);
            }
        }
    }

    return vTx;
}

// Generate cheap hash from seeds using SipHash
uint64_t GetShortID(uint64_t shorttxidk0, uint64_t shorttxidk1, const uint256 &txhash, uint64_t grapheneVersion)
{
    if (grapheneVersion < 2)
        return txhash.GetCheapHash();

    // If both shorttxidk0 and shorttxidk1 are equal to 0, then it is very likely
    // that the values have not been properly instantiated using FillShortTxIDSelector,
    // but are instead unchanged from the default initialization value.
    DbgAssert(!(shorttxidk0 == 0 && shorttxidk1 == 0), );

    static_assert(SHORTTXIDS_LENGTH == 8, "shorttxids calculation assumes 8-byte shorttxids");
    return SipHashUint256(shorttxidk0, shorttxidk1, txhash) & 0xffffffffffffffL;
}

bool NegotiateFastFilterSupport(CNode *pfrom)
{
    uint64_t peerFastFilterPref;
    {
        LOCK(pfrom->cs_xversion);
        peerFastFilterPref = pfrom->xVersion.as_u64c(XVer::BU_GRAPHENE_FAST_FILTER_PREF);
    }

    if (grapheneFastFilterCompatibility.Value() == EITHER)
    {
        if (peerFastFilterPref == EITHER)
            return true;
        else if (peerFastFilterPref == FAST)
            return true;
        else
            return false;
    }
    else if (grapheneFastFilterCompatibility.Value() == FAST)
    {
        if (peerFastFilterPref == EITHER)
            return true;
        else if (peerFastFilterPref == FAST)
            return true;
        else
            throw std::runtime_error("Sender and receiver have incompatible fast filter preferences");
    }
    else
    {
        if (peerFastFilterPref == EITHER)
            return false;
        else if (peerFastFilterPref == FAST)
            throw std::runtime_error("Sender and receiver have incompatible fast filter preferences");
        else
            return false;
    }
}


uint64_t NegotiateGrapheneVersion(CNode *pfrom)
{
    DbgAssert(pfrom, throw std::runtime_error("null CNode"));
    if (pfrom->negotiatedGrapheneVersion == GRAPHENE_NO_VERSION_SUPPORTED)
        throw std::runtime_error("Sender and receiver support incompatible Graphene versions");
    return pfrom->negotiatedGrapheneVersion;
}
