// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "net_processing.h"

#include "DoubleSpendProof.h"
#include "DoubleSpendProofStorage.h"
#include "addrman.h"
#include "blockrelay/blockrelay_common.h"
#include "blockrelay/compactblock.h"
#include "blockrelay/graphene.h"
#include "blockrelay/mempool_sync.h"
#include "blockrelay/thinblock.h"
#include "blockstorage/blockstorage.h"
#include "chain.h"
#include "dosman.h"
#include "electrum/electrs.h"
#include "expedited.h"
#include "main.h"
#include "merkleblock.h"
#include "nodestate.h"
#include "requestManager.h"
#include "timedata.h"
#include "txadmission.h"
#include "validation/validation.h"
#include "validationinterface.h"
#include "version.h"
#include "xversionkeys.h"

extern std::atomic<int64_t> nTimeBestReceived;
extern std::atomic<int> nPreferredDownload;
extern int nSyncStarted;
extern std::map<uint256, std::pair<CBlockHeader, int64_t> > mapUnConnectedHeaders;
extern CTweak<unsigned int> maxBlocksInTransitPerPeer;
extern CTweak<uint64_t> grapheneMinVersionSupported;
extern CTweak<uint64_t> grapheneMaxVersionSupported;
extern CTweak<uint64_t> grapheneFastFilterCompatibility;
extern CTweak<uint64_t> mempoolSyncMinVersionSupported;
extern CTweak<uint64_t> mempoolSyncMaxVersionSupported;
extern CTweak<uint64_t> syncMempoolWithPeers;
extern CTweak<uint32_t> randomlyDontInv;
extern CTweak<uint32_t> doubleSpendProofs;

/** How many inbound connections will we track before pruning entries */
const uint32_t MAX_INBOUND_CONNECTIONS_TRACKED = 10000;
/** maximum size (in bytes) of a batched set of transactions */
static const uint32_t MAX_TXN_BATCH_SIZE = 10000;

// Requires cs_main
bool CanDirectFetch(const Consensus::Params &consensusParams)
{
    return chainActive.Tip()->GetBlockTime() > GetAdjustedTime() - consensusParams.nPowTargetSpacing * 20;
}

void UpdatePreferredDownload(CNode *node)
{
    CNodeStateAccessor state(nodestate, node->GetId());
    DbgAssert(state != nullptr, return );
    nPreferredDownload.fetch_sub(state->fPreferredDownload);

    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = !node->fOneShot && !node->fClient;
    // BU allow downloads from inbound nodes; this may have been limited to stop attackers from connecting
    // and offering a bad chain.  However, we are connecting to multiple nodes and so can choose the most work
    // chain on that basis.
    // state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;
    // LOG(NET, "node %s preferred DL: %d because (%d || %d) && %d && %d\n", node->GetLogName(),
    //   state->fPreferredDownload, !node->fInbound, node->fWhitelisted, !node->fOneShot, !node->fClient);

    nPreferredDownload.fetch_add(state->fPreferredDownload);
}

// Requires cs_main
bool PeerHasHeader(const CNodeState *state, CBlockIndex *pindex)
{
    if (pindex == nullptr)
        return false;
    if (state->pindexBestKnownBlock && pindex == state->pindexBestKnownBlock->GetAncestor(pindex->nHeight))
        return true;
    if (state->pindexBestHeaderSent && pindex == state->pindexBestHeaderSent->GetAncestor(pindex->nHeight))
        return true;
    return false;
}

void static ProcessGetData(CNode *pfrom, const Consensus::Params &consensusParams, std::deque<CInv> &vInv)
{
    std::vector<CInv> vNotFound;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

    std::deque<CInv>::iterator it = vInv.begin();
    while (it != vInv.end())
    {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize() + ss.size())
        {
            LOG(REQ, "Postponing %d getdata requests.  Send buffer is too large: %d\n", vInv.size(), pfrom->nSendSize);
            break;
        }
        if (shutdown_threads.load() == true)
        {
            return;
        }

        // start processing inventory here
        const CInv &inv = *it;
        it++;

        if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK || inv.type == MSG_CMPCT_BLOCK)
        {
            auto *mi = LookupBlockIndex(inv.hash);
            if (mi)
            {
                bool fSend = false;
                {
                    LOCK(cs_main);
                    if (chainActive.Contains(mi))
                    {
                        fSend = true;
                    }
                    else
                    {
                        static const int nOneMonth = 30 * 24 * 60 * 60;
                        // To prevent fingerprinting attacks, only send blocks outside of the active
                        // chain if they are valid, and no more than a month older (both in time, and in
                        // best equivalent proof of work) than the best header chain we know about.
                        {
                            READLOCK(cs_mapBlockIndex);
                            fSend = mi->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != NULL) &&
                                    (pindexBestHeader.load()->GetBlockTime() - mi->GetBlockTime() < nOneMonth) &&
                                    (GetBlockProofEquivalentTime(
                                         *pindexBestHeader, *mi, *pindexBestHeader, consensusParams) < nOneMonth);
                        }
                        if (!fSend)
                        {
                            LOG(NET, "%s: ignoring request from peer=%s for old block that isn't in the main chain\n",
                                __func__, pfrom->GetLogName());
                        }
                        else
                        {
                            // Don't relay excessive blocks that are not on the active chain
                            if (mi->nStatus & BLOCK_EXCESSIVE)
                                fSend = false;
                            if (!fSend)
                                LOG(NET, "%s: ignoring request from peer=%s for excessive block of height %d not on "
                                         "the main chain\n",
                                    __func__, pfrom->GetLogName(), mi->nHeight);
                        }
                        // TODO: in the future we can throttle old block requests by setting send=false if we are out
                        // of bandwidth
                    }
                }
                // disconnect node in case we have reached the outbound limit for serving historical blocks
                // never disconnect whitelisted nodes
                static const int nOneWeek = 7 * 24 * 60 * 60; // assume > 1 week = historical
                if (fSend && CNode::OutboundTargetReached(true) &&
                    (((pindexBestHeader != nullptr) &&
                         (pindexBestHeader.load()->GetBlockTime() - mi->GetBlockTime() > nOneWeek)) ||
                        inv.type == MSG_FILTERED_BLOCK) &&
                    !pfrom->fWhitelisted)
                {
                    LOG(NET, "historical block serving limit reached, disconnect peer %s\n", pfrom->GetLogName());

                    // disconnect node
                    pfrom->fDisconnect = true;
                    fSend = false;
                }
                // Avoid leaking prune-height by never sending blocks below the
                // NODE_NETWORK_LIMITED threshold.
                // Add two blocks buffer extension for possible races
                if (fSend && !pfrom->fWhitelisted &&
                    ((((nLocalServices & NODE_NETWORK_LIMITED) == NODE_NETWORK_LIMITED) &&
                        ((nLocalServices & NODE_NETWORK) != NODE_NETWORK) &&
                        (chainActive.Tip()->nHeight - mi->nHeight > (int)NODE_NETWORK_LIMITED_MIN_BLOCKS + 2))))
                {
                    LOG(NET, "Ignore block request below NODE_NETWORK_LIMITED threshold from peer=%d\n",
                        pfrom->GetId());
                    // disconnect node and prevent it from stalling (would
                    // otherwise wait for the missing block)
                    pfrom->fDisconnect = true;
                    fSend = false;
                }
                // Pruned nodes may have deleted the block, so check whether
                // it's available before trying to send.
                if (fSend && mi->nStatus & BLOCK_HAVE_DATA)
                {
                    // Send block from disk
                    CBlock block;
                    if (!ReadBlockFromDisk(block, mi, consensusParams))
                    {
                        // its possible that I know about it but haven't stored it yet
                        LOG(THIN, "unable to load block %s from disk\n",
                            mi->phashBlock ? mi->phashBlock->ToString() : "");
                        // no response
                    }
                    else
                    {
                        if (inv.type == MSG_BLOCK)
                        {
                            pfrom->blocksSent += 1;
                            pfrom->PushMessage(NetMsgType::BLOCK, block);
                        }
                        else if (inv.type == MSG_CMPCT_BLOCK)
                        {
                            LOG(CMPCT, "Sending compactblock via getdata message\n");
                            SendCompactBlock(MakeBlockRef(block), pfrom, inv);
                        }
                        else // MSG_FILTERED_BLOCK)
                        {
                            LOCK(pfrom->cs_filter);
                            if (pfrom->pfilter)
                            {
                                CMerkleBlock merkleBlock(block, *pfrom->pfilter);
                                pfrom->PushMessage(NetMsgType::MERKLEBLOCK, merkleBlock);
                                pfrom->blocksSent += 1;
                                // CMerkleBlock just contains hashes, so also push any transactions in the block the
                                // client did not see. This avoids hurting performance by pointlessly requiring a
                                // round-trip.
                                //
                                // Note that there is currently no way for a node to request any single transactions
                                // we didn't send here - they must either disconnect and retry or request the full
                                // block. Thus, the protocol spec specified allows for us to provide duplicate txn
                                // here, however we MUST always provide at least what the remote peer needs
                                typedef std::pair<unsigned int, uint256> PairType;
                                for (PairType &pair : merkleBlock.vMatchedTxn)
                                {
                                    pfrom->txsSent += 1;
                                    pfrom->PushMessage(NetMsgType::TX, block.vtx[pair.first]);
                                }
                            }
                            // else
                            // no response
                        }

                        // Trigger the peer node to send a getblocks request for the next batch of inventory
                        if (inv.hash == pfrom->hashContinue)
                        {
                            // Bypass PushInventory, this must send even if redundant,
                            // and we want it right after the last block so they don't
                            // wait for other stuff first.
                            std::vector<CInv> oneInv;
                            oneInv.push_back(CInv(MSG_BLOCK, chainActive.Tip()->GetBlockHash()));
                            pfrom->PushMessage(NetMsgType::INV, oneInv);
                            pfrom->hashContinue.SetNull();
                        }
                    }
                }
            }
        }
        else if (inv.IsKnownType())
        {
            CTransactionRef ptx = nullptr;

            // Send stream from relay memory
            {
                // We need to release this lock before push message. There is a potential deadlock because
                // cs_vSend is often taken before cs_mapRelay
                LOCK(cs_mapRelay);
                std::map<CInv, CTransactionRef>::iterator mi = mapRelay.find(inv);
                if (mi != mapRelay.end())
                {
                    ptx = (*mi).second;
                }
            }
            if (!ptx)
            {
                ptx = CommitQGet(inv.hash);
                if (!ptx)
                {
                    ptx = mempool.get(inv.hash);
                }
            }

            // If we found a txn then push it
            if (ptx)
            {
                if (pfrom->txConcat != 0)
                {
                    ss << *ptx;

                    // Send the concatenated txns if we're over the limit. We don't want to batch
                    // too many and end up delaying the send.
                    if (ss.size() > MAX_TXN_BATCH_SIZE)
                    {
                        pfrom->PushMessage(NetMsgType::TX, ss);
                        ss.clear();
                    }
                }
                else if (inv.type == MSG_DOUBLESPENDPROOF && doubleSpendProofs.Value() == true)
                {
                    DoubleSpendProof dsp = mempool.doubleSpendProofStorage()->lookup(inv.hash);
                    if (!dsp.isEmpty())
                    {
                        CDataStream ssDSP(SER_NETWORK, PROTOCOL_VERSION);
                        ssDSP.reserve(600);
                        ssDSP << dsp;
                        pfrom->PushMessage(NetMsgType::DSPROOF, ssDSP);
                    }
                    else
                    {
                        pfrom->PushMessage(NetMsgType::REJECT, std::string(NetMsgType::DSPROOF), REJECT_INVALID,
                            std::string("dsproof requested was not found"));
                    }
                }
                else
                {
                    // Or if this is not a peer that supports
                    // concatenation then send the transaction right away.
                    pfrom->PushMessage(NetMsgType::TX, ptx);
                }
                pfrom->txsSent += 1;
            }
            else
            {
                vNotFound.push_back(inv);
            }
        }

        // Track requests for our stuff.
        GetMainSignals().Inventory(inv.hash);

        // Send only one of these message type before breaking. These type of requests use more
        // resources to process and send, therefore we don't want some a peer to, intentionlally or
        // unintentionally, dominate our network layer.
        if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK || inv.type == MSG_CMPCT_BLOCK)
            break;
    }
    // Send the batched transactions if any to send.
    if (!ss.empty())
    {
        pfrom->PushMessage(NetMsgType::TX, ss);
    }

    // Erase all messages inv's we processed
    vInv.erase(vInv.begin(), it);

    if (!vNotFound.empty())
    {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage(NetMsgType::NOTFOUND, vNotFound);
    }
}

static void handleAddressAfterInit(CNode *pfrom)
{
    if (!pfrom->fInbound)
    {
        // Advertise our address
        if (fListen && !IsInitialBlockDownload())
        {
            CAddress addr = GetLocalAddress(&pfrom->addr);
            FastRandomContext insecure_rand;
            if (addr.IsRoutable())
            {
                LOG(NET, "ProcessMessages: advertising address %s\n", addr.ToString());
                pfrom->PushAddress(addr, insecure_rand);
            }
            else if (IsPeerAddrLocalGood(pfrom))
            {
                addr.SetIP(pfrom->addrLocal);
                LOG(NET, "ProcessMessages: advertising address %s\n", addr.ToString());
                pfrom->PushAddress(addr, insecure_rand);
            }
        }
        // Get recent addresses
        pfrom->fGetAddr = true;
        pfrom->PushMessage(NetMsgType::GETADDR);
        addrman.Good(pfrom->addr);
    }
    else
    {
        if (((CNetAddr)pfrom->addr) == (CNetAddr)pfrom->addrFrom_advertised)
        {
            addrman.Add(pfrom->addrFrom_advertised, pfrom->addrFrom_advertised);
            addrman.Good(pfrom->addrFrom_advertised);
        }
    }
}

static void enableSendHeaders(CNode *pfrom)
{
    // Tell our peer we prefer to receive headers rather than inv's
    // We send this to non-NODE NETWORK peers as well, because even
    // non-NODE NETWORK peers can announce blocks (such as pruning
    // nodes)
    if (pfrom->nVersion >= SENDHEADERS_VERSION)
        pfrom->PushMessage(NetMsgType::SENDHEADERS);
}

static void enableCompactBlocks(CNode *pfrom)
{
    // Tell our peer that we support compact blocks
    if (IsCompactBlocksEnabled() && (pfrom->nVersion >= COMPACTBLOCKS_VERSION))
    {
        bool fHighBandwidth = false;
        uint64_t nVersion = 1;
        pfrom->PushMessage(NetMsgType::SENDCMPCT, fHighBandwidth, nVersion);
    }
}

bool ProcessMessage(CNode *pfrom, std::string strCommand, CDataStream &vRecv, int64_t nStopwatchTimeReceived)
{
    int64_t receiptTime = GetTime();
    const CChainParams &chainparams = Params();
    unsigned int msgSize = vRecv.size(); // BU for statistics
    UpdateRecvStats(pfrom, strCommand, msgSize, nStopwatchTimeReceived);
    LOG(NET, "received: %s (%u bytes) peer=%s\n", SanitizeString(strCommand), msgSize, pfrom->GetLogName());
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        LOGA("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (!(nLocalServices & NODE_BLOOM) &&
        (strCommand == NetMsgType::FILTERLOAD || strCommand == NetMsgType::FILTERADD ||
            strCommand == NetMsgType::FILTERCLEAR))
    {
        if (pfrom->nVersion >= NO_BLOOM_VERSION)
        {
            dosMan.Misbehaving(pfrom, 100);
            return false;
        }
        else
        {
            LOG(NET, "Inconsistent bloom filter settings peer %s\n", pfrom->GetLogName());
            pfrom->fDisconnect = true;
            return false;
        }
    }

    bool grapheneVersionCompatible = true;
    try
    {
        NegotiateGrapheneVersion(pfrom);
        NegotiateFastFilterSupport(pfrom);
    }
    catch (const std::runtime_error &e)
    {
        grapheneVersionCompatible = false;
    }
    // ------------------------- BEGIN INITIAL COMMAND SET PROCESSING
    if (strCommand == NetMsgType::VERSION)
    {
        int64_t nTime;
        CAddress addrMe;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;

        // Update thin type peer counters. This should be at the top here before we have any
        // potential disconnects, because on disconnect the counters will then get decremented.
        thinrelay.AddPeers(pfrom);

        if (pfrom->nVersion < MIN_PEER_PROTO_VERSION)
        {
            // ban peers older than this proto version
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                strprintf("Protocol Version must be %d or greater", MIN_PEER_PROTO_VERSION));
            dosMan.Misbehaving(pfrom, 100);
            return error("Using obsolete protocol version %i - banning peer=%s version=%s", pfrom->nVersion,
                pfrom->GetLogName(), pfrom->cleanSubVer);
        }

        if (!vRecv.empty())
            vRecv >> pfrom->addrFrom_advertised >> nNonce;
        if (!vRecv.empty())
        {
            vRecv >> LIMITED_STRING(pfrom->strSubVer, MAX_SUBVERSION_LENGTH);
            pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);

            // Track the user agent string
            {
                LOCK(cs_mapInboundConnectionTracker);

                // Remove a random entry if we've gotten too big.
                if (mapInboundConnectionTracker.size() >= MAX_INBOUND_CONNECTIONS_TRACKED)
                {
                    size_t nIndex = GetRandInt(mapInboundConnectionTracker.size() - 1);
                    auto rand_iter = std::next(mapInboundConnectionTracker.begin(), nIndex);
                    mapInboundConnectionTracker.erase(rand_iter);
                }

                // Add the subver string.
                mapInboundConnectionTracker[(CNetAddr)pfrom->addr].userAgent = pfrom->cleanSubVer;
            }

            // ban SV peers
            if (pfrom->strSubVer.find("Bitcoin SV") != std::string::npos ||
                pfrom->strSubVer.find("(SV;") != std::string::npos)
            {
                dosMan.Misbehaving(pfrom, 100, BanReasonInvalidPeer);
            }
        }
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;
        if (!vRecv.empty())
            vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
        else
            pfrom->fRelayTxes = true;

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            LOGA("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        pfrom->addrLocal = addrMe;
        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();


        // set nodes not relaying blocks and tx and not serving (parts) of the historical blockchain as "clients"
        pfrom->fClient = (!(pfrom->nServices & NODE_NETWORK) && !(pfrom->nServices & NODE_NETWORK_LIMITED));

        // set nodes not capable of serving the complete blockchain history as "limited nodes"
        pfrom->m_limited_node = (!(pfrom->nServices & NODE_NETWORK) && (pfrom->nServices & NODE_NETWORK_LIMITED));

        // Potentially mark this peer as a preferred download peer.
        UpdatePreferredDownload(pfrom);

        // only send extversion message if both peers are using the protocol
        if ((nLocalServices & NODE_XVERSION) && (pfrom->nServices & NODE_XVERSION))
        {
            // BU expedited procecessing requires the exchange of the listening port id
            // The former BUVERSION message has now been integrated into the xmap field in CXVersionMessage.

            // prepare xversion message. This must be sent before we send a verack message in the new xversion spec
            CXVersionMessage xver;
            xver.set_u64c(XVer::XVERSION_VERSION_KEY, XVERSION_VERSION_VALUE);
            xver.set_u64c(XVer::BU_LISTEN_PORT, GetListenPort());
            xver.set_u64c(XVer::BU_MSG_IGNORE_CHECKSUM, 1); // we will ignore 0 value msg checksums
            xver.set_u64c(XVer::BU_GRAPHENE_MAX_VERSION_SUPPORTED, grapheneMaxVersionSupported.Value());
            xver.set_u64c(XVer::BU_GRAPHENE_MIN_VERSION_SUPPORTED, grapheneMinVersionSupported.Value());
            xver.set_u64c(XVer::BU_GRAPHENE_FAST_FILTER_PREF, grapheneFastFilterCompatibility.Value());
            xver.set_u64c(XVer::BU_MEMPOOL_SYNC, syncMempoolWithPeers.Value());
            xver.set_u64c(XVer::BU_MEMPOOL_SYNC_MAX_VERSION_SUPPORTED, mempoolSyncMaxVersionSupported.Value());
            xver.set_u64c(XVer::BU_MEMPOOL_SYNC_MIN_VERSION_SUPPORTED, mempoolSyncMinVersionSupported.Value());
            xver.set_u64c(XVer::BU_XTHIN_VERSION, 2); // xthin version

            size_t nLimitAncestors = GetArg("-limitancestorcount", BU_DEFAULT_ANCESTOR_LIMIT);
            size_t nLimitAncestorSize = GetArg("-limitancestorsize", BU_DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000;
            size_t nLimitDescendants = GetArg("-limitdescendantcount", BU_DEFAULT_DESCENDANT_LIMIT);
            size_t nLimitDescendantSize = GetArg("-limitdescendantsize", BU_DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000;

            xver.set_u64c(XVer::BU_MEMPOOL_ANCESTOR_COUNT_LIMIT, nLimitAncestors);
            xver.set_u64c(XVer::BU_MEMPOOL_ANCESTOR_SIZE_LIMIT, nLimitAncestorSize);
            xver.set_u64c(XVer::BU_MEMPOOL_DESCENDANT_COUNT_LIMIT, nLimitDescendants);
            xver.set_u64c(XVer::BU_MEMPOOL_DESCENDANT_SIZE_LIMIT, nLimitDescendantSize);
            xver.set_u64c(XVer::BU_TXN_CONCATENATION, 1);

            electrum::set_xversion_flags(xver, chainparams.NetworkIDString());

            pfrom->xVersionExpected = true;
            pfrom->PushMessage(NetMsgType::XVERSION, xver);
        }
        else
        {
            // Send VERACK handshake message
            pfrom->PushMessage(NetMsgType::VERACK);
        }

        // Change version
        {
            LOCK(pfrom->cs_vSend);
            pfrom->ssSend.SetVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));
        }

        LOG(NET, "receive version message: %s: version %d, blocks=%d, us=%s, peer=%s\n", pfrom->cleanSubVer,
            pfrom->nVersion, pfrom->nStartingHeight, addrMe.ToString(), pfrom->GetLogName());

        int64_t nTimeOffset = nTime - GetTime();
        pfrom->nTimeOffset = nTimeOffset;
        AddTimeData(pfrom->addr, nTimeOffset);

        // Feeler connections exist only to verify if address is online.
        if (pfrom->fFeeler)
        {
            // Should never occur but if it does correct the value.
            // We can't have an inbound "feeler" connection, so the value must be improperly set.
            DbgAssert(pfrom->fInbound == false, pfrom->fFeeler = false);
            if (pfrom->fInbound == false)
            {
                LOG(NET, "Disconnecting feeler to peer %s\n", pfrom->GetLogName());
                pfrom->fDisconnect = true;
            }
        }
    }

    else if ((pfrom->nVersion == 0 || pfrom->tVersionSent < 0) && !pfrom->fWhitelisted)
    {
        // Must have a version message before anything else
        dosMan.Misbehaving(pfrom, 1);
        pfrom->fDisconnect = true;
        return error("%s receieved before VERSION message - disconnecting peer=%s", strCommand, pfrom->GetLogName());
    }

    else if (strCommand == NetMsgType::XVERSION)
    {
        // set expected to false, we got the message
        pfrom->xVersionExpected = false;
        if (pfrom->fSuccessfullyConnected == true)
        {
            dosMan.Misbehaving(pfrom, 1);
            pfrom->fDisconnect = true;
            return error("odd peer behavior: received verack message before xversion, disconnecting \n");
        }

        vRecv >> pfrom->xVersion;

        if (pfrom->addrFromPort != 0)
        {
            LOG(NET, "Encountered odd node that sent BUVERSION before XVERSION. Ignoring duplicate addrFromPort "
                     "setting. peer=%s version=%s\n",
                pfrom->GetLogName(), pfrom->cleanSubVer);
        }

        pfrom->ReadConfigFromXVersion();

        pfrom->PushMessage(NetMsgType::VERACK);
    }

    else if (!pfrom->fSuccessfullyConnected && GetTime() - pfrom->tVersionSent > VERACK_TIMEOUT &&
             pfrom->tVersionSent >= 0)
    {
        // If verack is not received within timeout then disconnect.
        // The peer may be slow so disconnect them only, to give them another chance if they try to re-connect.
        // If they are a bad peer and keep trying to reconnect and still do not VERACK, they will eventually
        // get banned by the connection slot algorithm which tracks disconnects and reconnects.
        pfrom->fDisconnect = true;
        LOG(NET, "ERROR: disconnecting - VERACK not received within %d seconds for peer=%s version=%s\n",
            VERACK_TIMEOUT, pfrom->GetLogName(), pfrom->cleanSubVer);

        // update connection tracker which is used by the connection slot algorithm.
        LOCK(cs_mapInboundConnectionTracker);
        CNetAddr ipAddress = (CNetAddr)pfrom->addr;
        mapInboundConnectionTracker[ipAddress].nEvictions += 1;
        mapInboundConnectionTracker[ipAddress].nLastEvictionTime = GetTime();
        mapInboundConnectionTracker[ipAddress].userAgent = pfrom->cleanSubVer;

        return true; // return true so we don't get any process message failures in the log.
    }

    else if (strCommand == NetMsgType::VERACK)
    {
        if (pfrom->fSuccessfullyConnected == true)
        {
            dosMan.Misbehaving(pfrom, 1);
            pfrom->fDisconnect = true;
            return error("duplicate verack messages");
        }
        pfrom->SetRecvVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));

        if (pfrom->xVersionExpected.load() == true)
        {
            // if we expected xversion but got a verack it is possible there is a service bit
            // mismatch so we should send a verack response because the peer might not
            // support xversion
            pfrom->PushMessage(NetMsgType::VERACK);
        }

        /// LEGACY xversion code (old spec)
        if (!(pfrom->nServices & NODE_XVERSION))
        {
            // prepare xversion message. This *must* be the next message after the verack has been received,
            // if it comes at all in the old xversion spec.
            CXVersionMessage xver;
            xver.set_u64c(XVer::BU_LISTEN_PORT_OLD, GetListenPort());
            xver.set_u64c(XVer::BU_MSG_IGNORE_CHECKSUM_OLD, 1); // we will ignore 0 value msg checksums
            xver.set_u64c(XVer::BU_GRAPHENE_MAX_VERSION_SUPPORTED_OLD, grapheneMaxVersionSupported.Value());
            xver.set_u64c(XVer::BU_GRAPHENE_MIN_VERSION_SUPPORTED_OLD, grapheneMinVersionSupported.Value());
            xver.set_u64c(XVer::BU_GRAPHENE_FAST_FILTER_PREF_OLD, grapheneFastFilterCompatibility.Value());
            xver.set_u64c(XVer::BU_MEMPOOL_SYNC_OLD, syncMempoolWithPeers.Value());
            xver.set_u64c(XVer::BU_MEMPOOL_SYNC_MAX_VERSION_SUPPORTED_OLD, mempoolSyncMaxVersionSupported.Value());
            xver.set_u64c(XVer::BU_MEMPOOL_SYNC_MIN_VERSION_SUPPORTED_OLD, mempoolSyncMinVersionSupported.Value());
            xver.set_u64c(XVer::BU_XTHIN_VERSION_OLD, 2); // xthin version

            size_t nLimitAncestors = GetArg("-limitancestorcount", BU_DEFAULT_ANCESTOR_LIMIT);
            size_t nLimitAncestorSize = GetArg("-limitancestorsize", BU_DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000;
            size_t nLimitDescendants = GetArg("-limitdescendantcount", BU_DEFAULT_DESCENDANT_LIMIT);
            size_t nLimitDescendantSize = GetArg("-limitdescendantsize", BU_DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000;

            xver.set_u64c(XVer::BU_MEMPOOL_ANCESTOR_COUNT_LIMIT_OLD, nLimitAncestors);
            xver.set_u64c(XVer::BU_MEMPOOL_ANCESTOR_SIZE_LIMIT_OLD, nLimitAncestorSize);
            xver.set_u64c(XVer::BU_MEMPOOL_DESCENDANT_COUNT_LIMIT_OLD, nLimitDescendants);
            xver.set_u64c(XVer::BU_MEMPOOL_DESCENDANT_SIZE_LIMIT_OLD, nLimitDescendantSize);
            xver.set_u64c(XVer::BU_TXN_CONCATENATION_OLD, 1);

            electrum::set_xversion_flags(xver, chainparams.NetworkIDString());

            pfrom->PushMessage(NetMsgType::XVERSION_OLD, xver);
        }

        handleAddressAfterInit(pfrom);
        enableSendHeaders(pfrom);
        enableCompactBlocks(pfrom);

        // Tell the peer what maximum xthin bloom filter size we will consider acceptable.
        // FIXME: integrate into xversion as well
        if (pfrom->ThinBlockCapable() && IsThinBlocksEnabled())
        {
            pfrom->PushMessage(NetMsgType::FILTERSIZEXTHIN, nXthinBloomFilterSize);
        }

        // This step done after final handshake
        CheckAndRequestExpeditedBlocks(pfrom);

        pfrom->fSuccessfullyConnected = true;
    }

    else if (strCommand == NetMsgType::XVERSION_OLD)
    {
        vRecv >> pfrom->xVersion;

        if (pfrom->addrFromPort != 0)
        {
            LOG(NET, "Encountered odd node that sent BUVERSION before XVERSION. Ignoring duplicate addrFromPort "
                     "setting. peer=%s version=%s\n",
                pfrom->GetLogName(), pfrom->cleanSubVer);
        }

        pfrom->ReadConfigFromXVersion_OLD();

        pfrom->PushMessage(NetMsgType::XVERACK_OLD);
        // handleAddressAfterInit(pfrom);
        // enableSendHeaders(pfrom);
        // enableCompactBlocks(pfrom);
    }
    else if (strCommand == NetMsgType::XVERACK_OLD)
    {
        // This step done after final handshake
        // CheckAndRequestExpeditedBlocks(pfrom);
    }

    else if (strCommand == NetMsgType::XUPDATE)
    {
        CXVersionMessage xUpdate;
        vRecv >> xUpdate;
        // check for peer trying to change non-changeable key
        for (auto entry : xUpdate.xmap)
        {
            if (XVer::IsChangableKey(entry.first))
            {
                LOCK(pfrom->cs_xversion);
                pfrom->xVersion.xmap[entry.first] = xUpdate.xmap[entry.first];
            }
        }
    }

    // XVERSION NOTICE: If you read this code as a reference to implement
    // xversion, *please* refrain from sending 'sendheaders' or
    // 'filtersizexthin' during the initial handshake to allow further
    // simplification and streamlining of the connection handshake down the
    // road. Allowing receipt of 'sendheaders'/'filtersizexthin' here is to
    // allow connection with BUCash 1.5.0.x nodes that introduced parallelized
    // message processing but not the state machine for (x)version
    // serialization.  This is valid protocol behavior (as in not breaking any
    // existing implementation) but likely still makes sense to be phased out
    // down the road.
    else if (strCommand == NetMsgType::SENDHEADERS)
    {
        CNodeStateAccessor(nodestate, pfrom->GetId())->fPreferHeaders = true;
    }

    else if (strCommand == NetMsgType::FILTERSIZEXTHIN)
    {
        if (pfrom->ThinBlockCapable())
        {
            uint32_t nSize = 0;
            vRecv >> nSize;
            pfrom->nXthinBloomfilterSize.store(nSize);

            // As a safeguard don't allow a smaller max bloom filter size than the default max size.
            if (!pfrom->nXthinBloomfilterSize || (pfrom->nXthinBloomfilterSize < SMALLEST_MAX_BLOOM_FILTER_SIZE))
            {
                pfrom->PushMessage(
                    NetMsgType::REJECT, strCommand, REJECT_INVALID, std::string("filter size was too small"));
                LOG(NET, "Disconnecting %s: bloom filter size too small\n", pfrom->GetLogName());
                pfrom->fDisconnect = true;
                return false;
            }
        }
        else
        {
            pfrom->fDisconnect = true;
            return false;
        }
    }

    // XVERSION notice: Reply to pings before initial xversion handshake is
    // complete. This behavior should also not be relied upon and it is likely
    // better to phase this out later (requiring only proper, expected
    // messages during the initial (x)version handshake).
    else if (strCommand == NetMsgType::PING)
    {
        LeaveCritical(&pfrom->csMsgSerializer);
        EnterCritical("pfrom.csMsgSerializer", __FILE__, __LINE__, (void *)(&pfrom->csMsgSerializer),
            LockType::SHARED_MUTEX, OwnershipType::EXCLUSIVE);
        uint64_t nonce = 0;
        vRecv >> nonce;
        // although PONG was enabled in BIP31, all clients should handle it at this point
        // and unknown messages are silently dropped.  So for simplicity, always respond with PONG
        // Echo the message back with the nonce. This allows for two useful features:
        //
        // 1) A remote node can quickly check if the connection is operational
        // 2) Remote nodes can measure the latency of the network thread. If this node
        //    is overloaded it won't respond to pings quickly and the remote node can
        //    avoid sending us more work, like chain download requests.
        //
        // The nonce stops the remote getting confused between different pings: without
        // it, if the remote node sends a ping once per second and this node takes 5
        // seconds to respond to each, the 5th ping the remote sends would appear to
        // return very quickly.
        pfrom->PushMessage(NetMsgType::PONG, nonce);
        LeaveCritical(&pfrom->csMsgSerializer);
        EnterCritical("pfrom.csMsgSerializer", __FILE__, __LINE__, (void *)(&pfrom->csMsgSerializer),
            LockType::SHARED_MUTEX, OwnershipType::SHARED);
    }

    // ------------------------- END INITIAL COMMAND SET PROCESSING
    else if (!pfrom->fSuccessfullyConnected)
    {
        LOG(NET, "Ignoring command %s that comes in before initial handshake is finished. peer=%s version=%s\n",
            strCommand, pfrom->GetLogName(), pfrom->cleanSubVer);

        // Ignore any other commands early in the handshake
        return false;
    }
    else if (strCommand == NetMsgType::ADDR)
    {
        std::vector<CAddress> vAddr;
        vRecv >> vAddr;

        if (vAddr.size() > 1000)
        {
            dosMan.Misbehaving(pfrom, 20);
            return error("message addr size() = %u", vAddr.size());
        }

        // To avoid malicious flooding of our address table, only allow unsolicited ADDR messages to
        // insert the connecting IP.  We need to allow this IP to be inserted, or there is no way for that node
        // to tell the network about itself if its behind a NAT.

        // Digression about how things work behind a NAT:
        //     Node A periodically ADDRs node B with the address that B reported to A as A's own
        //     address (in the VERSION message).
        //
        // The purpose of using exchange here is to atomically set to false and also get whether I asked for an addr
        if (!pfrom->fGetAddr.exchange(0) && pfrom->fInbound)
        {
            bool reportedOwnAddr = false;
            CAddress ownAddr;
            for (CAddress &addr : vAddr)
            {
                // server listen port will be different.  We want to compare IPs and then use provided port
                if ((CNetAddr)addr == (CNetAddr)pfrom->addr)
                {
                    ownAddr = addr;
                    reportedOwnAddr = true;
                    break;
                }
            }
            if (reportedOwnAddr)
            {
                vAddr.resize(1); // Get rid of every address the remote node tried to inject except itself.
                vAddr[0] = ownAddr;
            }
            else
            {
                // Today unsolicited ADDRs are not illegal, but we should consider misbehaving on this (if we add logic
                // to unmisbehaving over time), because a few unsolicited ADDRs are ok from a DOS perspective but lots
                // are not.
                // dosMan.Misbehaving(pfrom, 1);
                return true; // We don't want to process any other addresses, but giving them is not an error
            }
        }

        // Store the new addresses
        std::vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        FastRandomContext insecure_rand;
        for (CAddress &addr : vAddr)
        {
            if (shutdown_threads.load() == true)
            {
                return false;
            }

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the addrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = ArithToUint256(
                        UintToArith256(hashSalt) ^ (hashAddr << 32) ^ ((GetTime() + hashAddr) / (24 * 60 * 60)));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    std::multimap<uint256, CNode *> mapMix;
                    for (CNode *pnode : vNodes)
                    {
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = ArithToUint256(UintToArith256(hashRand) ^ nPointer);
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(std::make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (std::multimap<uint256, CNode *>::iterator mi = mapMix.begin();
                         mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr, insecure_rand);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (pfrom->fOneShot)
        {
            LOG(NET, "Disconnecting %s: one shot\n", pfrom->GetLogName());
            pfrom->fDisconnect = true;
        }
    }

    // Ignore this message if sent from a node advertising a version earlier than the first CB release (70014)
    else if (strCommand == NetMsgType::SENDCMPCT && pfrom->nVersion >= COMPACTBLOCKS_VERSION)
    {
        bool fHighBandwidth = false;
        uint64_t nVersion = 0;
        vRecv >> fHighBandwidth >> nVersion;

        // BCH network currently only supports version 1 (v2 is segwit support on BTC)
        // May need to be updated in the future if other clients deploy a new version
        pfrom->fSupportsCompactBlocks = nVersion == 1;

        // Increment compact block peer counter.
        thinrelay.AddCompactBlockPeer(pfrom);
    }

    else if (strCommand == NetMsgType::INV)
    {
        if (fImporting || fReindex)
            return true;

        std::vector<CInv> vInv;
        vRecv >> vInv;
        LOG(NET, "Received INV list of size %d\n", vInv.size());

        // Message Consistency Checking
        //   Check size == 0 to be intolerant of an empty and useless request.
        //   Validate that INVs are a valid type and not null.
        if (vInv.size() > MAX_INV_SZ || vInv.empty())
        {
            dosMan.Misbehaving(pfrom, 20);
            return error("message inv size() = %u", vInv.size());
        }

        // Allow whitelisted peers to send data other than blocks in blocks only mode if whitelistrelay is true
        if (pfrom->fWhitelisted && GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY))
            fBlocksOnly = false;

        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            if (shutdown_threads.load() == true)
                return false;

            const CInv &inv = vInv[nInv];
            if (!((inv.type == MSG_TX) || (inv.type == MSG_BLOCK) || inv.type == MSG_DOUBLESPENDPROOF))
            {
                LOG(NET, "message inv invalid type = %u hash %s", inv.type, inv.hash.ToString());
                return false;
            }
            else if (inv.hash.IsNull())
            {
                LOG(NET, "message inv has null hash %s", inv.type, inv.hash.ToString());
                return false;
            }

            if (inv.type == MSG_BLOCK)
            {
                LOCK(cs_main);
                bool fAlreadyHaveBlock = AlreadyHaveBlock(inv);
                LOG(NET, "got inv: %s  %s peer=%d\n", inv.ToString(), fAlreadyHaveBlock ? "have" : "new", pfrom->id);

                requester.UpdateBlockAvailability(pfrom->GetId(), inv.hash);
                // RE !IsInitialBlockDownload(): We do not want to get the block if the system is executing the initial
                // block download because
                // blocks are stored in block files in the order of arrival.  So grabbing blocks "early" will cause new
                // blocks to be sprinkled
                // throughout older block files.  This will stop those files from being pruned.
                // !IsInitialBlockDownload() can be removed if
                // a better block storage system is devised.
                if ((!fAlreadyHaveBlock && !IsInitialBlockDownload()) ||
                    (!fAlreadyHaveBlock && Params().NetworkIDString() == "regtest"))
                {
                    // Since we now only rely on headers for block requests, if we get an INV from an older node or
                    // if there was a very large re-org which resulted in a revert to block announcements via INV,
                    // we will instead request the header rather than the block.  This is safer and prevents an
                    // attacker from sending us fake INV's for blocks that do not exist or try to get us to request
                    // and download fake blocks.
                    pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexBestHeader), inv.hash);
                }
                else
                {
                    LOG(NET, "skipping request of block %s.  already have: %d  importing: %d  reindex: %d  "
                             "isChainNearlySyncd: %d\n",
                        inv.hash.ToString(), fAlreadyHaveBlock, fImporting, fReindex, IsChainNearlySyncd());
                }
            }
            else if (inv.type == MSG_TX)
            {
                bool fAlreadyHaveTx = TxAlreadyHave(inv);
                // LOG(NET, "got inv: %s  %d peer=%s\n", inv.ToString(), fAlreadyHaveTx ? "have" : "new",
                // pfrom->GetLogName());
                LOG(NET, "got inv: %s  have: %d peer=%s\n", inv.ToString(), fAlreadyHaveTx, pfrom->GetLogName());

                pfrom->AddInventoryKnown(inv);
                if (fBlocksOnly)
                {
                    LOG(NET, "transaction (%s) inv sent in violation of protocol peer=%d\n", inv.hash.ToString(),
                        pfrom->id);
                }
                // RE !IsInitialBlockDownload(): during IBD, its a waste of bandwidth to grab transactions, they will
                // likely be included in blocks that we IBD download anyway.  This is especially important as
                // transaction volumes increase.
                else if (!fAlreadyHaveTx && !IsInitialBlockDownload())
                {
                    requester.AskFor(inv, pfrom);
                }
            }
            else if (inv.type == MSG_DOUBLESPENDPROOF && doubleSpendProofs.Value() == true)
            {
                std::vector<CInv> vGetData;
                vGetData.push_back(inv);
                pfrom->PushMessage(NetMsgType::GETDATA, vGetData);
            }

            // Track requests for our stuff.
            GetMainSignals().Inventory(inv.hash);

            if (pfrom->nSendSize > (SendBufferSize() * 2))
            {
                dosMan.Misbehaving(pfrom, 50);
                return error("send buffer size() = %u", pfrom->nSendSize);
            }
        }
    }


    else if (strCommand == NetMsgType::GETDATA)
    {
        if (fImporting || fReindex)
        {
            LOG(NET, "received getdata from %s but importing\n", pfrom->GetLogName());
            return true;
        }

        std::vector<CInv> vInv;
        vRecv >> vInv;
        // BU check size == 0 to be intolerant of an empty and useless request
        if ((vInv.size() > MAX_INV_SZ) || (vInv.size() == 0))
        {
            dosMan.Misbehaving(pfrom, 20);
            return error("message getdata size() = %u", vInv.size());
        }

        // Validate that INVs are a valid type
        std::deque<CInv> invDeque;
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            const CInv &inv = vInv[nInv];
            if (!((inv.type == MSG_TX) || (inv.type == MSG_BLOCK) || (inv.type == MSG_FILTERED_BLOCK) ||
                    (inv.type == MSG_CMPCT_BLOCK) || inv.type == MSG_DOUBLESPENDPROOF))
            {
                dosMan.Misbehaving(pfrom, 20, BanReasonInvalidInventory);
                return error("message inv invalid type = %u", inv.type);
            }

            // Make basic checks
            if (inv.type == MSG_CMPCT_BLOCK)
            {
                if (!requester.CheckForRequestDOS(pfrom, chainparams))
                    return false;
            }

            invDeque.push_back(inv);
        }

        if (fDebug || (invDeque.size() != 1))
            LOG(NET, "received getdata (%u invsz) peer=%s\n", invDeque.size(), pfrom->GetLogName());

        if ((fDebug && invDeque.size() > 0) || (invDeque.size() == 1))
            LOG(NET, "received getdata for: %s peer=%s\n", invDeque[0].ToString(), pfrom->GetLogName());

        // Run process getdata and process as much of the getdata's as we can before taking the lock
        // and appending the remainder to the vRecvGetData queue.
        ProcessGetData(pfrom, chainparams.GetConsensus(), invDeque);
        if (!invDeque.empty())
        {
            LOCK(pfrom->csRecvGetData);
            pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), invDeque.begin(), invDeque.end());
        }
    }


    else if (strCommand == NetMsgType::GETBLOCKS)
    {
        if (fImporting || fReindex)
            return true;

        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        CBlockIndex *pindex = FindForkInGlobalIndex(chainActive, locator);

        // Send the rest of the chain
        if (pindex)
            pindex = chainActive.Next(pindex);
        int nLimit = 500;
        LOG(NET, "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1),
            hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                LOG(NET, "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            // If pruning, don't inv blocks unless we have on disk and are likely to still have
            // for some reasonable time window (1 hour) that block relay might require.
            const int nPrunedBlocksLikelyToHave =
                MIN_BLOCKS_TO_KEEP - 3600 / chainparams.GetConsensus().nPowTargetSpacing;
            {
                READLOCK(cs_mapBlockIndex); // for nStatus
                if (fPruneMode && (!(pindex->nStatus & BLOCK_HAVE_DATA) ||
                                      pindex->nHeight <= chainActive.Tip()->nHeight - nPrunedBlocksLikelyToHave))
                {
                    LOG(NET, " getblocks stopping, pruned or too old block at %d %s\n", pindex->nHeight,
                        pindex->GetBlockHash().ToString());
                    break;
                }
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll
                // trigger the peer to getblocks the next batch of inventory.
                LOG(NET, "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }


    else if (strCommand == NetMsgType::GETHEADERS)
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        CBlockIndex *pindex = nullptr;
        if (locator.IsNull())
        {
            pindex = LookupBlockIndex(hashStop);
            if (!pindex)
                return true;
        }

        std::vector<CBlock> vHeaders;
        {
            LOCK(cs_main); // for chainActive
            if (!locator.IsNull())
            {
                // Find the last block the caller has in the main chain
                pindex = FindForkInGlobalIndex(chainActive, locator);
                if (pindex)
                    pindex = chainActive.Next(pindex);
            }

            // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
            int nLimit = MAX_HEADERS_RESULTS;
            LOG(NET, "getheaders height %d for block %s from peer %s\n", (pindex ? pindex->nHeight : -1),
                hashStop.ToString(), pfrom->GetLogName());
            for (; pindex; pindex = chainActive.Next(pindex))
            {
                vHeaders.push_back(pindex->GetBlockHeader());
                if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                    break;
            }
        }
        // pindex can be nullptr either if we sent chainActive.Tip() OR
        // if our peer has chainActive.Tip() (and thus we are sending an empty
        // headers message). In both cases it's safe to update
        // pindexBestHeaderSent to be our tip.
        {
            CNodeStateAccessor state(nodestate, pfrom->GetId());
            state->pindexBestHeaderSent = pindex ? pindex : chainActive.Tip();
        }
        pfrom->PushMessage(NetMsgType::HEADERS, vHeaders);
    }


    else if (strCommand == NetMsgType::TX)
    {
        // Stop processing the transaction early if
        // We are in blocks only mode and peer is either not whitelisted or whitelistrelay is off
        if (GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY) &&
            (!pfrom->fWhitelisted || !GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY)))
        {
            LOG(NET, "transaction sent in violation of protocol peer=%d\n", pfrom->id);
            return true;
        }

        // Process as many concatenated txns as there may be in this message
        while (!vRecv.empty())
        {
            // Put the tx on the tx admission queue for processing
            CTxInputData txd;
            vRecv >> txd.tx;

            // Indicate that the tx was received and is about to be processed. Setting the processing flag
            // prevents us from re-requesting the txn during the time of processing and before mempool acceptance.
            requester.ProcessingTxn(txd.tx->GetHash(), pfrom);

            // Processing begins here where we enqueue the transaction.
            txd.nodeId = pfrom->id;
            txd.nodeName = pfrom->GetLogName();
            txd.whitelisted = pfrom->fWhitelisted;
            EnqueueTxForAdmission(txd);

            CInv inv(MSG_TX, txd.tx->GetHash());
            pfrom->AddInventoryKnown(inv);
            requester.UpdateTxnResponseTime(inv, pfrom);
        }
    }


    else if (strCommand == NetMsgType::HEADERS) // Ignore headers received while importing
    {
        if (fImporting)
        {
            LOG(NET, "skipping processing of HEADERS because importing\n");
            return true;
        }
        if (fReindex)
        {
            LOG(NET, "skipping processing of HEADERS because reindexing\n");
            return true;
        }
        std::vector<CBlockHeader> headers;

        // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
        unsigned int nCount = ReadCompactSize(vRecv);
        if (nCount > MAX_HEADERS_RESULTS)
        {
            dosMan.Misbehaving(pfrom, 20);
            return error("headers message size = %u", nCount);
        }
        headers.resize(nCount);
        for (unsigned int n = 0; n < nCount; n++)
        {
            vRecv >> headers[n];
            ReadCompactSize(vRecv); // ignore tx count; assume it is 0.
        }

        LOCK(cs_main);

        // Nothing interesting. Stop asking this peers for more headers.
        if (nCount == 0)
            return true;

        // Check all headers to make sure they are continuous before attempting to accept them.
        // This prevents and attacker from keeping us from doing direct fetch by giving us out
        // of order headers.
        bool fNewUnconnectedHeaders = false;
        uint256 hashLastBlock;
        hashLastBlock.SetNull();
        for (const CBlockHeader &header : headers)
        {
            // check that the first header has a previous block in the blockindex.
            if (hashLastBlock.IsNull())
            {
                if (LookupBlockIndex(header.hashPrevBlock))
                    hashLastBlock = header.hashPrevBlock;
            }

            // Add this header to the map if it doesn't connect to a previous header
            if (header.hashPrevBlock != hashLastBlock)
            {
                // If we still haven't finished downloading the initial headers during node sync and we get
                // an out of order header then we must disconnect the node so that we can finish downloading
                // initial headers from a diffeent peer. An out of order header at this point is likely an attack
                // to prevent the node from syncing.
                if (header.GetBlockTime() < GetAdjustedTime() - 24 * 60 * 60)
                {
                    pfrom->fDisconnect = true;
                    return error("non-continuous-headers sequence during node sync - disconnecting peer=%s",
                        pfrom->GetLogName());
                }
                fNewUnconnectedHeaders = true;
            }

            // if we have an unconnected header then add every following header to the unconnected headers cache.
            if (fNewUnconnectedHeaders)
            {
                uint256 hash = header.GetHash();
                if (mapUnConnectedHeaders.size() < MAX_UNCONNECTED_HEADERS)
                    mapUnConnectedHeaders[hash] = std::make_pair(header, GetTime());

                // update hashLastUnknownBlock so that we'll be able to download the block from this peer even
                // if we receive the headers, which will connect this one, from a different peer.
                requester.UpdateBlockAvailability(pfrom->GetId(), hash);
            }

            hashLastBlock = header.GetHash();
        }
        // return without error if we have an unconnected header.  This way we can try to connect it when the next
        // header arrives.
        if (fNewUnconnectedHeaders)
            return true;

        // If possible add any previously unconnected headers to the headers vector and remove any expired entries.
        std::map<uint256, std::pair<CBlockHeader, int64_t> >::iterator mi = mapUnConnectedHeaders.begin();
        while (mi != mapUnConnectedHeaders.end())
        {
            std::map<uint256, std::pair<CBlockHeader, int64_t> >::iterator toErase = mi;

            // Add the header if it connects to the previous header
            if (headers.back().GetHash() == (*mi).second.first.hashPrevBlock)
            {
                headers.push_back((*mi).second.first);
                mapUnConnectedHeaders.erase(toErase);

                // if you found one to connect then search from the beginning again in case there is another
                // that will connect to this new header that was added.
                mi = mapUnConnectedHeaders.begin();
                continue;
            }

            // Remove any entries that have been in the cache too long.  Unconnected headers should only exist
            // for a very short while, typically just a second or two.
            int64_t nTimeHeaderArrived = (*mi).second.second;
            uint256 headerHash = (*mi).first;
            mi++;
            if (GetTime() - nTimeHeaderArrived >= UNCONNECTED_HEADERS_TIMEOUT)
            {
                mapUnConnectedHeaders.erase(toErase);
            }
            // At this point we know the headers in the list received are known to be in order, therefore,
            // check if the header is equal to some other header in the list. If so then remove it from the cache.
            else
            {
                for (const CBlockHeader &header : headers)
                {
                    if (header.GetHash() == headerHash)
                    {
                        mapUnConnectedHeaders.erase(toErase);
                        break;
                    }
                }
            }
        }

        // Check and accept each header in dependency order (oldest block to most recent)
        CBlockIndex *pindexLast = nullptr;
        int i = 0;
        for (const CBlockHeader &header : headers)
        {
            CValidationState state;
            if (!AcceptBlockHeader(header, state, chainparams, &pindexLast))
            {
                int nDos;
                if (state.IsInvalid(nDos))
                {
                    if (nDos > 0)
                    {
                        dosMan.Misbehaving(pfrom, nDos);
                    }
                }
                // all headers from this one forward reference a fork that we don't follow, so erase them
                headers.erase(headers.begin() + i, headers.end());
                nCount = headers.size();
                break;
            }
            else
                PV->UpdateMostWorkOurFork(header);

            i++;
        }

        if (pindexLast)
            requester.UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

        if (nCount == MAX_HEADERS_RESULTS && pindexLast)
        {
            // Headers message had its maximum size; the peer may have more headers.
            // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
            // from there instead.
            LOG(NET, "more getheaders (%d) to end to peer=%s (startheight:%d)\n", pindexLast->nHeight,
                pfrom->GetLogName(), pfrom->nStartingHeight);
            pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexLast), uint256());

            {
                int64_t now = GetTime();
                CNodeStateAccessor state(nodestate, pfrom->GetId());
                DbgAssert(state != nullptr, );
                if (state != nullptr)
                    state->nSyncStartTime = now; // reset the time because more headers needed
            }

            // During the process of IBD we need to update block availability for every connected peer. To do that we
            // request, from each NODE_NETWORK peer, a header that matches the last blockhash found in this recent set
            // of headers. Once the requested header is received then the block availability for this peer will get
            // updated.
            if (IsInitialBlockDownload())
            {
                // To maintain locking order with cs_main we have to addrefs for each node and then release
                // the lock on cs_vNodes before aquiring cs_main further down.
                std::vector<CNode *> vNodesCopy;
                {
                    LOCK(cs_vNodes);
                    vNodesCopy = vNodes;
                    for (CNode *pnode : vNodes)
                    {
                        pnode->AddRef();
                    }
                }

                for (CNode *pnode : vNodesCopy)
                {
                    if (!pnode->fClient && pnode != pfrom)
                    {
                        bool ask = false;
                        {
                            CNodeStateAccessor state(nodestate, pfrom->GetId());
                            DbgAssert(state != nullptr, ); // do not return, we need to release refs later.
                            if (state == nullptr)
                                continue;

                            ask = (state->pindexBestKnownBlock == nullptr ||
                                   pindexLast->nChainWork > state->pindexBestKnownBlock->nChainWork);
                        } // let go of the CNodeState lock before we PushMessage since that is trapping op.

                        if (ask)
                        {
                            // We only want one single header so we pass a null for CBlockLocator.
                            pnode->PushMessage(NetMsgType::GETHEADERS, CBlockLocator(), pindexLast->GetBlockHash());
                            LOG(NET | BLK, "Requesting header for blockavailability, peer=%s block=%s height=%d\n",
                                pnode->GetLogName(), pindexLast->GetBlockHash().ToString().c_str(),
                                pindexBestHeader.load()->nHeight);
                        }
                    }
                }

                // release refs
                for (CNode *pnode : vNodesCopy)
                    pnode->Release();
            }
        }

        bool fCanDirectFetch = CanDirectFetch(chainparams.GetConsensus());

        {
            CNodeStateAccessor state(nodestate, pfrom->GetId());
            DbgAssert(state != nullptr, return false);

            // During the initial peer handshake we must receive the initial headers which should be greater
            // than or equal to our block height at the time of requesting GETHEADERS. This is because the peer has
            // advertised a height >= to our own. Furthermore, because the headers max returned is as much as 2000 this
            // could not be a mainnet re-org.
            if (!state->fFirstHeadersReceived)
            {
                // We want to make sure that the peer doesn't just send us any old valid header. The block height of the
                // last header they send us should be equal to our block height at the time we made the GETHEADERS
                // request.
                if (pindexLast && state->nFirstHeadersExpectedHeight <= pindexLast->nHeight)
                {
                    state->fFirstHeadersReceived = true;
                    LOG(NET, "Initial headers received for peer=%s\n", pfrom->GetLogName());
                }

                // Allow for very large reorgs (> 2000 blocks) on the nol test chain or other test net.
                if (Params().NetworkIDString() != "main" && Params().NetworkIDString() != "regtest")
                    state->fFirstHeadersReceived = true;
            }
        }

        // update the syncd status.  This should come before we make calls to requester.AskFor().
        IsChainNearlySyncdInit();
        IsInitialBlockDownloadInit();

        // If this set of headers is valid and ends in a block with at least as
        // much work as our tip, download as much as possible.
        if (fCanDirectFetch && pindexLast && pindexLast->IsValid(BLOCK_VALID_TREE) &&
            chainActive.Tip()->nChainWork <= pindexLast->nChainWork)
        {
            // Set tweak value.  Mostly used in testing direct fetch.
            if (maxBlocksInTransitPerPeer.Value() != 0)
                pfrom->nMaxBlocksInTransit.store(maxBlocksInTransitPerPeer.Value());

            std::vector<CBlockIndex *> vToFetch;
            CBlockIndex *pindexWalk = pindexLast;
            // Calculate all the blocks we'd need to switch to pindexLast.
            while (pindexWalk && !chainActive.Contains(pindexWalk))
            {
                vToFetch.push_back(pindexWalk);
                pindexWalk = pindexWalk->pprev;
            }

            // Download as much as possible, from earliest to latest.
            unsigned int nAskFor = 0;
            for (auto pindex_iter = vToFetch.rbegin(); pindex_iter != vToFetch.rend(); pindex_iter++)
            {
                CBlockIndex *pindex = *pindex_iter;
                // pindex must be nonnull because we populated vToFetch a few lines above
                CInv inv(MSG_BLOCK, pindex->GetBlockHash());
                if (!AlreadyHaveBlock(inv))
                {
                    requester.AskFor(inv, pfrom);
                    LOG(REQ, "AskFor block via headers direct fetch %s (%d) peer=%d\n",
                        pindex->GetBlockHash().ToString(), pindex->nHeight, pfrom->id);
                    nAskFor++;
                }
                // We don't care about how many blocks are in flight.  We just need to make sure we don't
                // ask for more than the maximum allowed per peer because the request manager will take care
                // of any duplicate requests.
                if (nAskFor >= pfrom->nMaxBlocksInTransit.load())
                {
                    LOG(NET, "Large reorg, could only direct fetch %d blocks\n", nAskFor);
                    break;
                }
            }
            if (nAskFor > 1)
            {
                LOG(NET, "Downloading blocks toward %s (%d) via headers direct fetch\n",
                    pindexLast->GetBlockHash().ToString(), pindexLast->nHeight);
            }
        }

        CheckBlockIndex(chainparams.GetConsensus());
    }

    // Handle Xthinblocks and Thinblocks
    else if (strCommand == NetMsgType::GET_XTHIN && !fImporting && !fReindex && IsThinBlocksEnabled())
    {
        if (!requester.CheckForRequestDOS(pfrom, chainparams))
            return false;

        CBloomFilter filterMemPool;
        CInv inv;
        vRecv >> inv >> filterMemPool;

        // Message consistency checking
        if (inv.hash.IsNull())
        {
            dosMan.Misbehaving(pfrom, 100);
            return error("invalid get_xthin type=%u hash=%s", inv.type, inv.hash.ToString());
        }


        // Validates that the filter is reasonably sized.
        LoadFilter(pfrom, &filterMemPool);
        {
            auto *invIndex = LookupBlockIndex(inv.hash);
            if (!invIndex)
            {
                dosMan.Misbehaving(pfrom, 100);
                return error("Peer %srequested nonexistent block %s", pfrom->GetLogName(), inv.hash.ToString());
            }

            CBlock block;
            const Consensus::Params &consensusParams = Params().GetConsensus();
            if (!ReadBlockFromDisk(block, invIndex, consensusParams))
            {
                // We don't have the block yet, although we know about it.
                return error(
                    "Peer %s requested block %s that cannot be read", pfrom->GetLogName(), inv.hash.ToString());
            }
            else
            {
                SendXThinBlock(MakeBlockRef(block), pfrom, inv);
            }
        }
    }
    else if (strCommand == NetMsgType::GET_THIN && !fImporting && !fReindex && IsThinBlocksEnabled())
    {
        if (!requester.CheckForRequestDOS(pfrom, chainparams))
            return false;

        CInv inv;
        vRecv >> inv;

        // Message consistency checking
        if (inv.hash.IsNull())
        {
            dosMan.Misbehaving(pfrom, 100);
            return error("invalid get_thin type=%u hash=%s", inv.type, inv.hash.ToString());
        }

        auto *invIndex = LookupBlockIndex(inv.hash);
        if (!invIndex)
        {
            dosMan.Misbehaving(pfrom, 100);
            return error("Peer %srequested nonexistent block %s", pfrom->GetLogName(), inv.hash.ToString());
        }

        CBlock block;
        const Consensus::Params &consensusParams = Params().GetConsensus();
        if (!ReadBlockFromDisk(block, invIndex, consensusParams))
        {
            // We don't have the block yet, although we know about it.
            return error("Peer %s requested block %s that cannot be read", pfrom->GetLogName(), inv.hash.ToString());
        }
        else
        {
            SendXThinBlock(MakeBlockRef(block), pfrom, inv);
        }
    }
    else if (strCommand == NetMsgType::XPEDITEDREQUEST)
    {
        return HandleExpeditedRequest(vRecv, pfrom);
    }
    else if (strCommand == NetMsgType::XPEDITEDBLK)
    {
        // ignore the expedited message unless we are at the chain tip...
        if (!fImporting && !fReindex && !IsInitialBlockDownload())
        {
            LOCK(pfrom->cs_thintype);
            if (!HandleExpeditedBlock(vRecv, pfrom))
                return false;
        }
    }

    else if (strCommand == NetMsgType::XTHINBLOCK && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsThinBlocksEnabled())
    {
        LOCK(pfrom->cs_thintype);
        return CXThinBlock::HandleMessage(vRecv, pfrom, strCommand, 0);
    }


    else if (strCommand == NetMsgType::THINBLOCK && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsThinBlocksEnabled())
    {
        LOCK(pfrom->cs_thintype);
        return CThinBlock::HandleMessage(vRecv, pfrom);
    }


    else if (strCommand == NetMsgType::GET_XBLOCKTX && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsThinBlocksEnabled())
    {
        if (!requester.CheckForRequestDOS(pfrom, chainparams))
            return false;

        LOCK(pfrom->cs_thintype);
        return CXRequestThinBlockTx::HandleMessage(vRecv, pfrom);
    }


    else if (strCommand == NetMsgType::XBLOCKTX && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsThinBlocksEnabled())
    {
        LOCK(pfrom->cs_thintype);
        return CXThinBlockTx::HandleMessage(vRecv, pfrom);
    }

    // Handle Graphene blocks
    else if (strCommand == NetMsgType::GET_GRAPHENE && !fImporting && !fReindex && IsGrapheneBlockEnabled() &&
             grapheneVersionCompatible)
    {
        if (!requester.CheckForRequestDOS(pfrom, chainparams))
            return false;

        LOCK(pfrom->cs_thintype);
        return HandleGrapheneBlockRequest(vRecv, pfrom, chainparams);
    }

    else if (strCommand == NetMsgType::GRAPHENEBLOCK && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsGrapheneBlockEnabled() && grapheneVersionCompatible)
    {
        LOCK(pfrom->cs_thintype);
        return CGrapheneBlock::HandleMessage(vRecv, pfrom, strCommand, 0);
    }


    else if (strCommand == NetMsgType::GET_GRAPHENETX && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsGrapheneBlockEnabled() && grapheneVersionCompatible)
    {
        if (!requester.CheckForRequestDOS(pfrom, chainparams))
            return false;

        LOCK(pfrom->cs_thintype);
        return CRequestGrapheneBlockTx::HandleMessage(vRecv, pfrom);
    }


    else if (strCommand == NetMsgType::GRAPHENETX && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsGrapheneBlockEnabled() && grapheneVersionCompatible)
    {
        LOCK(pfrom->cs_thintype);
        return CGrapheneBlockTx::HandleMessage(vRecv, pfrom);
    }

    else if (strCommand == NetMsgType::GET_GRAPHENE_RECOVERY && IsGrapheneBlockEnabled() && grapheneVersionCompatible)
    {
        if (!requester.CheckForRequestDOS(pfrom, chainparams))
            return false;

        LOCK(pfrom->cs_thintype);
        return HandleGrapheneBlockRecoveryRequest(vRecv, pfrom, chainparams);
    }

    else if (strCommand == NetMsgType::GRAPHENE_RECOVERY && IsGrapheneBlockEnabled() && grapheneVersionCompatible)
    {
        if (!requester.CheckForRequestDOS(pfrom, chainparams))
            return false;

        LOCK(pfrom->cs_thintype);
        return HandleGrapheneBlockRecoveryResponse(vRecv, pfrom, chainparams);
    }

    // Handle Compact Blocks
    else if (strCommand == NetMsgType::CMPCTBLOCK && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsCompactBlocksEnabled())
    {
        LOCK(pfrom->cs_thintype);
        return CompactBlock::HandleMessage(vRecv, pfrom);
    }
    else if (strCommand == NetMsgType::GETBLOCKTXN && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsCompactBlocksEnabled())
    {
        if (!requester.CheckForRequestDOS(pfrom, chainparams))
            return false;

        LOCK(pfrom->cs_thintype);
        return CompactReRequest::HandleMessage(vRecv, pfrom);
    }
    else if (strCommand == NetMsgType::BLOCKTXN && !fImporting && !fReindex && !IsInitialBlockDownload() &&
             IsCompactBlocksEnabled())
    {
        LOCK(pfrom->cs_thintype);
        return CompactReReqResponse::HandleMessage(vRecv, pfrom);
    }

    // Mempool synchronization request
    else if (strCommand == NetMsgType::GET_MEMPOOLSYNC)
    {
        return HandleMempoolSyncRequest(vRecv, pfrom);
    }

    else if (strCommand == NetMsgType::MEMPOOLSYNC)
    {
        return CMempoolSync::ReceiveMempoolSync(vRecv, pfrom, strCommand);
    }

    // Mempool synchronization transaction request
    else if (strCommand == NetMsgType::GET_MEMPOOLSYNCTX)
    {
        return CRequestMempoolSyncTx::HandleMessage(vRecv, pfrom);
    }

    else if (strCommand == NetMsgType::MEMPOOLSYNCTX)
    {
        return CMempoolSyncTx::HandleMessage(vRecv, pfrom);
    }


    // Handle full blocks
    else if (strCommand == NetMsgType::BLOCK && !fImporting && !fReindex) // Ignore blocks received while importing
    {
        CBlockRef pblock(new CBlock());
        {
            uint64_t nCheckBlockSize = vRecv.size();
            vRecv >> *pblock;

            // Sanity check. The serialized block size should match the size that is in our receive queue.  If not
            // this could be an attack block of some kind.
            DbgAssert(nCheckBlockSize == pblock->GetBlockSize(), return true);
        }

        CInv inv(MSG_BLOCK, pblock->GetHash());
        LOG(BLK, "received block %s peer=%d\n", inv.hash.ToString(), pfrom->id);
        UnlimitedLogBlock(*pblock, inv.hash.ToString(), receiptTime);

        if (IsChainNearlySyncd()) // BU send the received block out expedited channels quickly
        {
            CValidationState state;
            if (CheckBlockHeader(*pblock, state, true)) // block header is fine
                SendExpeditedBlock(*pblock, pfrom);
        }

        { // reset the getheaders time because block can consume all bandwidth
            int64_t now = GetTime();
            CNodeStateAccessor state(nodestate, pfrom->GetId());
            DbgAssert(state != nullptr, );
            if (state != nullptr)
                state->nSyncStartTime = now; // reset the time because more headers needed
        }
        pfrom->nPingUsecStart = GetStopwatchMicros(); // Reset ping time because block can consume all bandwidth

        // Message consistency checking
        // NOTE: consistency checking is handled by checkblock() which is called during
        //       ProcessNewBlock() during HandleBlockMessage.
        PV->HandleBlockMessage(pfrom, strCommand, pblock, inv);
    }


    else if (strCommand == NetMsgType::GETADDR)
    {
        // This asymmetric behavior for inbound and outbound connections was introduced
        // to prevent a fingerprinting attack: an attacker can send specific fake addresses
        // to users' AddrMan and later request them by sending getaddr messages.
        // Making nodes which are behind NAT and can only make outgoing connections ignore
        // the getaddr message mitigates the attack.
        if (!pfrom->fInbound)
        {
            LOG(NET, "Ignoring \"getaddr\" from outbound connection. peer=%d\n", pfrom->id);
            return true;
        }

        // Only send one GetAddr response per connection to reduce resource waste
        //  and discourage addr stamping of INV announcements.
        if (pfrom->fSentAddr)
        {
            LOG(NET, "Ignoring repeated \"getaddr\". peer=%d\n", pfrom->id);
            return true;
        }
        pfrom->fSentAddr = true;
        LOCK(pfrom->cs_vSend);
        pfrom->vAddrToSend.clear();
        std::vector<CAddress> vAddr = addrman.GetAddr();
        FastRandomContext insecure_rand;
        for (const CAddress &addr : vAddr)
            pfrom->PushAddress(addr, insecure_rand);
    }


    else if (strCommand == NetMsgType::MEMPOOL)
    {
        if (CNode::OutboundTargetReached(false) && !pfrom->fWhitelisted)
        {
            LOG(NET, "mempool request with bandwidth limit reached, disconnect peer %s\n", pfrom->GetLogName());
            pfrom->fDisconnect = true;
            return true;
        }
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        std::vector<CInv> vInv;

        // Because we have to take cs_filter after mempool.cs, in order to maintain locking order, we
        // need find out if a filter is present first before later doing the mempool.get().
        bool fHaveFilter = false;
        {
            LOCK(pfrom->cs_filter);
            fHaveFilter = pfrom->pfilter ? true : false;
        }

        for (uint256 &hash : vtxid)
        {
            CInv inv(MSG_TX, hash);
            if (fHaveFilter)
            {
                CTransactionRef ptx = nullptr;
                ptx = mempool.get(inv.hash);
                if (ptx == nullptr)
                    continue; // another thread removed since queryHashes, maybe...

                LOCK(pfrom->cs_filter);
                if (!pfrom->pfilter->IsRelevantAndUpdate(ptx))
                    continue;
            }
            vInv.push_back(inv);
            if (vInv.size() == MAX_INV_SZ)
            {
                pfrom->PushMessage(NetMsgType::INV, vInv);
                vInv.clear();
            }
        }
        if (vInv.size() > 0)
            pfrom->PushMessage(NetMsgType::INV, vInv);
    }

    else if (strCommand == NetMsgType::PONG)
    {
        int64_t pingUsecEnd = nStopwatchTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce))
        {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0)
            {
                if (nonce == pfrom->nPingNonceSent)
                {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0)
                    {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = std::min(pfrom->nMinPingUsecTime, pingUsecTime);
                    }
                    else
                    {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                }
                else
                {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0)
                    {
                        // This is most likely a bug in another implementation somewhere; cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            }
            else
            {
                sProblem = "Unsolicited pong without ping";
            }
        }
        else
        {
            // This is most likely a bug in another implementation somewhere; cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty()))
        {
            LOG(NET, "pong peer=%d: %s, %x expected, %x received, %u bytes\n", pfrom->id, sProblem,
                pfrom->nPingNonceSent, nonce, nAvail);
        }
        if (bPingFinished)
        {
            pfrom->nPingNonceSent = 0;
        }
    }


    else if (strCommand == NetMsgType::FILTERLOAD)
    {
        CBloomFilter filter;
        vRecv >> filter;

        if (!filter.IsWithinSizeConstraints())
        {
            // There is no excuse for sending a too-large filter
            dosMan.Misbehaving(pfrom, 100);
            return false;
        }

        LOCK(pfrom->cs_filter);
        delete pfrom->pfilter;
        pfrom->pfilter = new CBloomFilter(filter);
        if (!pfrom->pfilter->IsEmpty())
            pfrom->fRelayTxes = true;
        else
            pfrom->fRelayTxes = false;
    }


    else if (strCommand == NetMsgType::FILTERADD)
    {
        std::vector<unsigned char> vData;
        vRecv >> vData;

        // Nodes must NEVER send a data item > 520 bytes (the max size for a script data object,
        // and thus, the maximum size any matched object can have) in a filteradd message
        if (vData.size() > MAX_SCRIPT_ELEMENT_SIZE)
        {
            dosMan.Misbehaving(pfrom, 100);
        }
        else
        {
            LOCK(pfrom->cs_filter);
            if (pfrom->pfilter)
                pfrom->pfilter->insert(vData);
            else
                dosMan.Misbehaving(pfrom, 100);
        }
    }


    else if (strCommand == NetMsgType::FILTERCLEAR)
    {
        LOCK(pfrom->cs_filter);
        delete pfrom->pfilter;
        pfrom->pfilter = new CBloomFilter();
        pfrom->fRelayTxes = true;
    }

    else if (strCommand == NetMsgType::DSPROOF)
    {
        if (doubleSpendProofs.Value() == true)
        {
            LOG(DSPROOF, "Received a double spend proof from peer:%d\n", pfrom->GetId());
            uint256 dspHash;
            try
            {
                DoubleSpendProof dsp;
                vRecv >> dsp;
                if (dsp.isEmpty())
                    throw std::runtime_error("Double spend proof is empty");

                dspHash = dsp.GetHash();
                DoubleSpendProof::Validity validity;
                {
                    READLOCK(mempool.cs_txmempool);
                    validity = dsp.validate(mempool);
                }
                switch (validity)
                {
                case DoubleSpendProof::Valid:
                {
                    LOG(DSPROOF, "Double spend proof is valid from peer:%d\n", pfrom->GetId());
                    const auto ptx = mempool.addDoubleSpendProof(dsp);
                    if (ptx.get())
                    {
                        // find any descendants of this double spent transaction. If there are any
                        // then we must also forward this double spend proof to any SPV peers that
                        // want to know about this tx or its descendants.
                        CTxMemPool::setEntries setDescendants;
                        {
                            READLOCK(mempool.cs_txmempool);
                            CTxMemPool::indexed_transaction_set::const_iterator iter =
                                mempool.mapTx.find(ptx->GetHash());
                            if (iter == mempool.mapTx.end())
                            {
                                break;
                            }
                            else
                            {
                                mempool._CalculateDescendants(iter, setDescendants);
                            }
                        }

                        // added to mempool correctly, then forward to nodes.
                        broadcastDspInv(ptx, dspHash, &setDescendants);
                    }
                    break;
                }
                case DoubleSpendProof::MissingUTXO:
                case DoubleSpendProof::MissingTransaction:
                    LOG(DSPROOF, "Double spend proof is orphan: postponed\n");
                    mempool.doubleSpendProofStorage()->addOrphan(dsp, pfrom->GetId());
                    break;
                case DoubleSpendProof::Invalid:
                    throw std::runtime_error(strprintf("Double spend proof didn't validate (%s)", dspHash.ToString()));
                default:
                    return false;
                }
            }
            catch (const std::exception &e)
            {
                LOG(DSPROOF, "Failure handling double spend proof. Peer: %d Reason: %s\n", pfrom->GetId(), e.what());
                if (!dspHash.IsNull())
                    mempool.doubleSpendProofStorage()->markProofRejected(dspHash);
                dosMan.Misbehaving(pfrom->GetId(), 10);
                return false;
            }
        }
    }

    else if (strCommand == NetMsgType::REJECT)
    {
        // BU: Request manager: this was restructured to not just be active in fDebug mode so that the request manager
        // can be notified of request rejections.
        try
        {
            std::string strMsg;
            unsigned char ccode;
            std::string strReason;
            uint256 hash;

            vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >>
                LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);
            std::ostringstream ss;
            ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

            // BU: Check request manager reject codes
            if (strMsg == NetMsgType::BLOCK || strMsg == NetMsgType::TX)
            {
                vRecv >> hash;
                ss << ": hash " << hash.ToString();

                // We need to see this reject message in either "req" or "net" debug mode
                LOG(REQ | NET, "Reject %s\n", SanitizeString(ss.str()));

                if (strMsg == NetMsgType::BLOCK)
                {
                    requester.Rejected(CInv(MSG_BLOCK, hash), pfrom, ccode);
                }
                else if (strMsg == NetMsgType::TX)
                {
                    requester.Rejected(CInv(MSG_TX, hash), pfrom, ccode);
                }
            }
            // if (fDebug) {
            // ostringstream ss;
            // ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

            // if (strMsg == NetMsgType::BLOCK || strMsg == NetMsgType::TX)
            //  {
            //    ss << ": hash " << hash.ToString();
            //  }
            // LOG(NET, "Reject %s\n", SanitizeString(ss.str()));
            // }
        }
        catch (const std::ios_base::failure &)
        {
            // Avoid feedback loops by preventing reject messages from triggering a new reject message.
            LOG(NET, "Unparseable reject message received\n");
            LOG(REQ, "Unparseable reject message received\n");
        }
    }

    else
    {
        // Ignore unknown commands for extensibility
        LOG(NET, "Unknown command \"%s\" from peer=%d\n", SanitizeString(strCommand), pfrom->id);
    }

    return true;
}

bool ProcessMessages(CNode *pfrom)
{
    const CChainParams &chainparams = Params();
    // if (fDebug)
    //    LOGA("%s(%u messages)\n", __func__, pfrom->vRecvMsg.size());

    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fOk = true;

    // Check getdata requests first if there are no priority messages waiting.
    if (!fPriorityRecvMsg.load())
    {
        TRY_LOCK(pfrom->csRecvGetData, locked);
        if (locked && !pfrom->vRecvGetData.empty())
        {
            ProcessGetData(pfrom, chainparams.GetConsensus(), pfrom->vRecvGetData);
        }
    }

    int msgsProcessed = 0;
    // Don't bother if send buffer is too full to respond anyway
    CNode *pfrom_original = pfrom;
    std::deque<std::pair<CNodeRef, CNetMessage> > vPriorityRecvQ_delay;
    while ((!pfrom->fDisconnect) && (pfrom->nSendSize < SendBufferSize()) && (shutdown_threads.load() == false))
    {
        CNodeRef noderef;
        bool fIsPriority = false;
        READLOCK(pfrom->csMsgSerializer);
        CNetMessage msg;
        bool fUseLowPriorityMsg = true;
        bool fUsePriorityMsg = true;
        // try to complete the handshake before handling messages that require us to be fSuccessfullyConnected
        if (pfrom->fSuccessfullyConnected == false)
        {
            TRY_LOCK(pfrom->cs_vRecvMsg, lockRecv);
            if (!lockRecv)
                break;
            if (pfrom->vRecvMsg_handshake.empty())
                break;

            // get the message from the queue
            // simply getting the front message should be sufficient, the only time a xversion or verack is sent is
            // once the previous has been processed so tracking which stage of the handshake we are on is overkill
            std::swap(msg, pfrom->vRecvMsg_handshake.front());
            pfrom->vRecvMsg_handshake.pop_front();
            pfrom->currentRecvMsgSize -= msg.size();
            msgsProcessed++;
        }
        else
        {
            {
                TRY_LOCK(pfrom->cs_vRecvMsg, lockRecv);
                if (!lockRecv)
                    break;
                if (pfrom->vRecvMsg_handshake.empty() == false)
                {
                    std::string frontCommand = pfrom->vRecvMsg_handshake.front().hdr.GetCommand();
                    if (frontCommand == NetMsgType::VERSION || frontCommand == NetMsgType::VERACK ||
                        frontCommand == NetMsgType::XVERSION)
                    {
                        pfrom->vRecvMsg_handshake.clear();
                        pfrom->fDisconnect = true;
                        dosMan.Misbehaving(pfrom, 1);
                        return error(
                            "recieved early handshake message after successfully connected, disconnecting peer=%s",
                            pfrom->GetLogName());
                    }

                    // this code should only handle XVERSION_OLD and XVERACK_OLD messages
                    std::swap(msg, pfrom->vRecvMsg_handshake.front());
                    pfrom->vRecvMsg_handshake.pop_front();
                    pfrom->currentRecvMsgSize -= msg.size();
                    msgsProcessed++;
                    fUsePriorityMsg = false;
                    fUseLowPriorityMsg = false;
                }
            }
            // Get next message to process checking whether it is a priority messasge and if so then
            // process it right away. It doesn't matter that the peer where the message came from is
            // different than the one we are currently procesing as we will switch to the correct peer
            // automatically. Furthermore by using and holding the CNodeRef we automatically maintain
            // a node reference to the priority peer.
            if (fUsePriorityMsg && fPriorityRecvMsg.load())
            {
                TRY_LOCK(cs_priorityRecvQ, locked);
                if (locked && !vPriorityRecvQ.empty())
                {
                    // Get the message out of queue.
                    std::swap(noderef, vPriorityRecvQ.front().first);
                    std::swap(msg, vPriorityRecvQ.front().second);
                    vPriorityRecvQ.pop_front();

                    if (vPriorityRecvQ.empty())
                        fPriorityRecvMsg.store(false);

                    // check if we should process the message.
                    CNode *pnode = noderef.get();
                    if (pnode->fDisconnect)
                    {
                        // if the node is to be disconnected dont bother responding
                        continue;
                    }
                    if (pnode->nSendSize > SendBufferSize())
                    {
                        // if the nodes send is full, delay the processing of this message until a time when
                        // send is not full
                        vPriorityRecvQ_delay.emplace_back(std::move(noderef), std::move(msg));
                        continue;
                    }

                    fIsPriority = true;
                    fUseLowPriorityMsg = false;
                }
                else if (locked && vPriorityRecvQ.empty())
                {
                    fPriorityRecvMsg.store(false);
                    fUseLowPriorityMsg = true;
                }
            }

            if (fUseLowPriorityMsg)
            {
                TRY_LOCK(pfrom->cs_vRecvMsg, lockRecv);
                if (!lockRecv)
                    break;
                if (pfrom->vRecvMsg.empty())
                    break;

                // get the message from the queue
                std::swap(msg, pfrom->vRecvMsg.front());
                pfrom->vRecvMsg.pop_front();
            }

            // Check if this is a priority message and if so then modify pfrom to be the peer which
            // this priority message came from.
            if (fIsPriority)
                pfrom = noderef.get();
            else
                pfrom->currentRecvMsgSize -= msg.size();

            msgsProcessed++;
        }

        // if (fDebug)
        //    LOGA("%s(message %u msgsz, %u bytes, complete:%s)\n", __func__,
        //            msg.hdr.nMessageSize, msg.vRecv.size(),
        //            msg.complete() ? "Y" : "N");

        // Scan for message start
        if (memcmp(msg.hdr.pchMessageStart, pfrom->GetMagic(chainparams), MESSAGE_START_SIZE) != 0)
        {
            // Setting the cleanSubVer string allows us to present this peer in the bantable
            // with a likely peer type if it uses the BitcoinCore network magic.
            if (memcmp(msg.hdr.pchMessageStart, chainparams.MessageStart(), MESSAGE_START_SIZE) == 0)
            {
                pfrom->cleanSubVer = "BitcoinCore Network application";
            }

            LOG(NET, "PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%s\n", SanitizeString(msg.hdr.GetCommand()),
                pfrom->GetLogName());
            if (!pfrom->fWhitelisted)
            {
                // ban for 4 hours
                dosMan.Ban(pfrom->addr, pfrom->cleanSubVer, BanReasonInvalidMessageStart, 4 * 60 * 60);
            }
            fOk = false;
            break;
        }

        // Read header
        CMessageHeader &hdr = msg.hdr;
        if (!hdr.IsValid(pfrom->GetMagic(chainparams)))
        {
            LOG(NET, "PROCESSMESSAGE: ERRORS IN HEADER %s peer=%s\n", SanitizeString(hdr.GetCommand()),
                pfrom->GetLogName());
            continue;
        }
        std::string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;

        CDataStream &vRecv = msg.vRecv;

#if 0 // Do not waste my CPU calculating a checksum provided by an untrusted node
      // TCP already has one that is sufficient for network errors.  The checksum does not increase security since
      // an attacker can always provide a bad message with a good checksum.
      // This code is removed by comment so it is clear that it is a deliberate omission.
        if (hdr.nChecksum != 0)
        {
            uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
            unsigned int nChecksum = ReadLE32((unsigned char *)&hash);
            if (nChecksum != hdr.nChecksum)
            {
                LOG(NET, "%s(%s, %u bytes): CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n", __func__,
                    SanitizeString(strCommand), nMessageSize, nChecksum, hdr.nChecksum);
                continue;
            }
        }
#endif

        // Process message
        bool fRet = false;
        try
        {
            fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nStopwatch);
            if (shutdown_threads.load() == true)
            {
                return false;
            }
        }
        catch (const std::ios_base::failure &e)
        {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_MALFORMED, std::string("error parsing message"));
            if (strstr(e.what(), "end of data"))
            {
                // Allow exceptions from under-length message on vRecv
                LOG(NET, "%s(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than "
                         "its stated length\n",
                    __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
                // Allow exceptions from over-long size
                LOG(NET, "%s(%s, %u bytes): Exception '%s' caught\n", __func__, SanitizeString(strCommand),
                    nMessageSize, e.what());
            }
            else
            {
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
        }
        catch (const boost::thread_interrupted &)
        {
            throw;
        }
        catch (const std::exception &e)
        {
            PrintExceptionContinue(&e, "ProcessMessages()");
        }
        catch (...)
        {
            PrintExceptionContinue(nullptr, "ProcessMessages()");
        }

        if (!fRet)
            LOG(NET, "%s(%s, %u bytes) FAILED peer %s\n", __func__, SanitizeString(strCommand), nMessageSize,
                pfrom->GetLogName());

        if (msgsProcessed > 2000)
            break; // let someone else do something periodically

        // Swap back to the original peer if we just processed a priority message
        if (fIsPriority)
            pfrom = pfrom_original;
    }

    {
        LOCK(cs_priorityRecvQ);
        // re-add the priority messages we delayed back to the queue so that we can try them again later
        vPriorityRecvQ.insert(vPriorityRecvQ.end(), vPriorityRecvQ_delay.begin(), vPriorityRecvQ_delay.end());
        if (!vPriorityRecvQ.empty())
            fPriorityRecvMsg.store(true);
    }

    return fOk;
}

bool SendMessages(CNode *pto)
{
    const Consensus::Params &consensusParams = Params().GetConsensus();
    {
        // First set fDisconnect if appropriate.
        pto->DisconnectIfBanned();

        // Check for an internal disconnect request and if true then set fDisconnect. This would typically happen
        // during initial sync when a peer has a slow connection and we want to disconnect them.  We want to then
        // wait for any blocks that are still in flight before disconnecting, rather than re-requesting them again.
        if (pto->fDisconnectRequest)
        {
            NodeId nodeid = pto->GetId();
            int nInFlight = requester.GetNumBlocksInFlight(nodeid);
            LOG(IBD, "peer %s, checking disconnect request with %d in flight blocks\n", pto->GetLogName(), nInFlight);
            if (nInFlight == 0)
            {
                pto->fDisconnect = true;
                LOG(IBD, "peer %s, disconnect request was set, so disconnected\n", pto->GetLogName());
            }
        }

        // Now exit early if disconnecting or the version handshake is not complete.  We must not send PING or other
        // connection maintenance messages before the handshake is done.
        if (pto->fDisconnect || !pto->fSuccessfullyConnected)
            return true;

        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued)
        {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < (int64_t)GetStopwatchMicros())
        {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend)
        {
            uint64_t nonce = 0;
            while (nonce == 0)
            {
                GetRandBytes((unsigned char *)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetStopwatchMicros();
            pto->nPingNonceSent = nonce;
            pto->PushMessage(NetMsgType::PING, nonce);
        }

        // Check to see if there are any thin type blocks in flight that have gone beyond the
        // timeout interval. If so then we need to disconnect them so that the thintype data is nullified.
        // We could null the associated data here but that would possibly cause a node to be banned later if
        // the thin type block finally did show up, so instead we just disconnect this slow node.
        thinrelay.CheckForDownloadTimeout(pto);

        // Check for block download timeout and disconnect node if necessary. Does not require cs_main.
        int64_t nNow = GetStopwatchMicros();
        requester.DisconnectOnDownloadTimeout(pto, consensusParams, nNow);

        // Address refresh broadcast
        if (!IsInitialBlockDownload() && pto->nNextLocalAddrSend < nNow)
        {
            AdvertiseLocal(pto);
            pto->nNextLocalAddrSend = PoissonNextSend(nNow, AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL);
        }

        //
        // Message: addr
        //
        if (pto->nNextAddrSend < nNow)
        {
            LOCK(pto->cs_vSend);
            pto->nNextAddrSend = PoissonNextSend(nNow, AVG_ADDRESS_BROADCAST_INTERVAL);
            std::vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            for (const CAddress &addr : pto->vAddrToSend)
            {
                if (!pto->addrKnown.contains(addr.GetKey()))
                {
                    pto->addrKnown.insert(addr.GetKey());
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        pto->PushMessage(NetMsgType::ADDR, vAddr);
                        vAddr.clear();
                    }
                }
            }
            if (!vAddr.empty())
                pto->PushMessage(NetMsgType::ADDR, vAddr);
            pto->vAddrToSend.clear();
        }

        CNodeState statem(CAddress(), "");
        const CNodeState *state = &statem;
        {
            CNodeStateAccessor stateAccess(nodestate, pto->GetId());
            if (state == nullptr)
            {
                return true;
            }
            statem = *stateAccess;
        }

        // If a sync has been started check whether we received the first batch of headers requested within the timeout
        // period. If not then disconnect and ban the node and a new node will automatically be selected to start the
        // headers download.
        if ((state->fSyncStarted) && (state->nSyncStartTime < GetTime() - INITIAL_HEADERS_TIMEOUT) &&
            (!state->fFirstHeadersReceived) && !pto->fWhitelisted)
        {
            // pto->fDisconnect = true;
            LOGA("Initial headers were either not received or not received before the timeout\n", pto->GetLogName());
        }

        // Start block sync
        if (pindexBestHeader == nullptr)
            pindexBestHeader = chainActive.Tip();
        // Download if this is a nice peer, or we have no nice peers and this one might do.
        bool fFetch = state->fPreferredDownload || (nPreferredDownload.load() == 0 && !pto->fOneShot);
        if (!state->fSyncStarted && !fImporting && !fReindex)
        {
            // Only allow the downloading of headers from a single pruned peer.
            static int nSyncStartedPruned = 0;
            if (pto->fClient && nSyncStartedPruned >= 1)
                fFetch = false;

            // Only actively request headers from a single peer, unless we're close to today.
            if ((nSyncStarted < MAX_HEADER_REQS_DURING_IBD && fFetch) ||
                chainActive.Tip()->GetBlockTime() > GetAdjustedTime() - SINGLE_PEER_REQUEST_MODE_AGE)
            {
                const CBlockIndex *pindexStart = chainActive.Tip();
                /* If possible, start at the block preceding the currently
                   best known header.  This ensures that we always get a
                   non-empty list of headers back as long as the peer
                   is up-to-date.  With a non-empty response, we can initialise
                   the peer's known best block.  This wouldn't be possible
                   if we requested starting at pindexBestHeader and
                   got back an empty response.  */
                if (pindexStart->pprev)
                    pindexStart = pindexStart->pprev;
                // BU Bug fix for Core:  Don't start downloading headers unless our chain is shorter
                if (pindexStart->nHeight < pto->nStartingHeight)
                {
                    CNodeStateAccessor modableState(nodestate, pto->GetId());
                    modableState->fSyncStarted = true;
                    modableState->nSyncStartTime = GetTime();
                    modableState->fRequestedInitialBlockAvailability = true;
                    modableState->nFirstHeadersExpectedHeight = pindexStart->nHeight;
                    nSyncStarted++;

                    if (pto->fClient)
                        nSyncStartedPruned++;

                    LOG(NET, "initial getheaders (%d) to peer=%s (startheight:%d)\n", pindexStart->nHeight,
                        pto->GetLogName(), pto->nStartingHeight);
                    pto->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexStart), uint256());
                }
            }
        }

        // During IBD and when a new NODE_NETWORK peer connects we have to ask for if it has our best header in order
        // to update our block availability. We only want/need to do this only once per peer (if the initial batch of
        // headers has still not been etirely donwnloaded yet then the block availability will be updated during that
        // process rather than here).
        if (IsInitialBlockDownload() && !state->fRequestedInitialBlockAvailability &&
            state->pindexBestKnownBlock == nullptr && !fReindex && !fImporting)
        {
            if (!pto->fClient)
            {
                CNodeStateAccessor(nodestate, pto->GetId())->fRequestedInitialBlockAvailability = true;

                // We only want one single header so we pass a null CBlockLocator.
                pto->PushMessage(NetMsgType::GETHEADERS, CBlockLocator(), pindexBestHeader.load()->GetBlockHash());
                LOG(NET | BLK, "Requesting header for initial blockavailability, peer=%s block=%s height=%d\n",
                    pto->GetLogName(), pindexBestHeader.load()->GetBlockHash().ToString(),
                    pindexBestHeader.load()->nHeight);
            }
        }

        // Resend wallet transactions that haven't gotten in a block yet
        // Except during reindex, importing and IBD, when old wallet
        // transactions become unconfirmed and spams other nodes.
        if (!fReindex && !fImporting && !IsInitialBlockDownload())
        {
            GetMainSignals().Broadcast(nTimeBestReceived.load());
        }

        //
        // Try sending block announcements via headers
        //
        {
            // If we have less than MAX_BLOCKS_TO_ANNOUNCE in our
            // list of block hashes we're relaying, and our peer wants
            // headers announcements, then find the first header
            // not yet known to our peer but would connect, and send.
            // If no header would connect, or if we have too many
            // blocks, or if the peer doesn't want headers, just
            // add all to the inv queue.
            std::vector<uint256> vBlocksToAnnounce;
            {
                // Make a copy so that we do not need to keep
                // cs_inventory which cannot be taken before cs_main.
                LOCK(pto->cs_inventory);
                vBlocksToAnnounce.swap(pto->vBlockHashesToAnnounce);
            }

            std::vector<CBlock> vHeaders;
            bool fRevertToInv = (!state->fPreferHeaders || vBlocksToAnnounce.size() > MAX_BLOCKS_TO_ANNOUNCE);
            CBlockIndex *pBestIndex = nullptr; // last header queued for delivery

            // Ensure pindexBestKnownBlock is up-to-date
            requester.ProcessBlockAvailability(pto->id);

            if (!fRevertToInv)
            {
                bool fFoundStartingHeader = false;
                // Try to find first header that our peer doesn't have, and
                // then send all headers past that one.  If we come across any
                // headers that aren't on chainActive, give up.
                for (const uint256 &hash : vBlocksToAnnounce)
                {
                    CBlockIndex *pindex = nullptr;
                    pindex = LookupBlockIndex(hash);

                    // Skip blocks that we don't know about.
                    if (!pindex)
                        continue;

                    if (pBestIndex != nullptr && pindex->pprev != pBestIndex)
                    {
                        // This means that the list of blocks to announce don't
                        // connect to each other.
                        // This shouldn't really be possible to hit during
                        // regular operation (because reorgs should take us to
                        // a chain that has some block not on the prior chain,
                        // which should be caught by the prior check), but one
                        // way this could happen is by using invalidateblock /
                        // reconsiderblock repeatedly on the tip, causing it to
                        // be added multiple times to vBlocksToAnnounce.
                        // Robustly deal with this rare situation by reverting
                        // to an inv.
                        fRevertToInv = true;
                        break;
                    }
                    pBestIndex = pindex;
                    if (fFoundStartingHeader)
                    {
                        // add this to the headers message
                        vHeaders.push_back(pindex->GetBlockHeader());
                    }
                    else if (PeerHasHeader(state, pindex))
                    {
                        continue; // keep looking for the first new block
                    }
                    else if (pindex->pprev == nullptr || PeerHasHeader(state, pindex->pprev))
                    {
                        // Peer doesn't have this header but they do have the prior one.
                        // Start sending headers.
                        fFoundStartingHeader = true;
                        vHeaders.push_back(pindex->GetBlockHeader());
                    }
                    else
                    {
                        // Peer doesn't have this header or the prior one -- nothing will
                        // connect, so bail out.
                        fRevertToInv = true;
                        break;
                    }
                }
            }
            if (fRevertToInv)
            {
                // If falling back to using an inv, just try to inv the tip.
                // The last entry in vBlocksToAnnounce was our tip at some point
                // in the past.
                if (!vBlocksToAnnounce.empty())
                {
                    for (const uint256 &hashToAnnounce : vBlocksToAnnounce)
                    {
                        CBlockIndex *pindex = nullptr;
                        pindex = LookupBlockIndex(hashToAnnounce);

                        // Skip blocks that we don't know about.
                        if (!pindex)
                            continue;

                        // If the peer announced this block to us, don't inv it back.
                        // (Since block announcements may not be via inv's, we can't solely rely on
                        // setInventoryKnown to track this.)
                        if (!PeerHasHeader(state, pindex))
                        {
                            pto->PushInventory(CInv(MSG_BLOCK, hashToAnnounce));
                            LOG(NET, "%s: sending inv peer=%d hash=%s\n", __func__, pto->id, hashToAnnounce.ToString());
                        }
                    }
                }
            }
            else if (!vHeaders.empty())
            {
                if (vHeaders.size() > 1)
                {
                    LOG(NET, "%s: %u headers, range (%s, %s), to peer=%d\n", __func__, vHeaders.size(),
                        vHeaders.front().GetHash().ToString(), vHeaders.back().GetHash().ToString(), pto->id);
                }
                else
                {
                    LOG(NET, "%s: sending header %s to peer=%d\n", __func__, vHeaders.front().GetHash().ToString(),
                        pto->id);
                }
                {
                    LOCK(pto->cs_vSend);
                    pto->PushMessage(NetMsgType::HEADERS, vHeaders);
                }
                CNodeStateAccessor(nodestate, pto->GetId())->pindexBestHeaderSent = pBestIndex;
            }
        }

        //
        // Message: inventory
        //
        // We must send all INV's before returning otherwise, under very heavy transaction rates, we could end up
        // falling behind in sending INV's and vInventoryToSend could possibly get quite large.
        bool haveInv2Send = false;
        {
            LOCK(pto->cs_inventory);
            haveInv2Send = !pto->vInventoryToSend.empty();
        }
        if (haveInv2Send)
        {
            std::vector<CInv> vInvSend;
            FastRandomContext rnd;
            while (1)
            {
                // Send message INV up to the MAX_INV_TO_SEND. Once we reach the max then send the INV message
                // and if there is any remaining it will be sent on the next iteration until vInventoryToSend is empty.
                int nToErase = 0;
                {
                    // BU - here we only want to forward message inventory if our peer has actually been requesting
                    // useful data or giving us useful data.  We give them 2 minutes to be useful but then choke off
                    // their inventory.  This prevents fake peers from connecting and listening to our inventory
                    // while providing no value to the network.
                    // However we will still send them block inventory in the case they are a pruned node or wallet
                    // waiting for block announcements, therefore we have to check each inv in pto->vInventoryToSend.
                    bool fChokeTxInv =
                        (pto->nActivityBytes == 0 && (GetStopwatchMicros() - pto->nStopwatchConnected) > 120 * 1000000);

                    // Find INV's which should be sent, save them to vInvSend, and then erase from vInventoryToSend.
                    LOCK(pto->cs_inventory);
                    int invsz = std::min((int)pto->vInventoryToSend.size(), MAX_INV_TO_SEND);
                    vInvSend.reserve(invsz);
                    for (const CInv &inv : pto->vInventoryToSend)
                    {
                        nToErase++;
                        if (inv.type == MSG_TX)
                        {
                            if (fChokeTxInv)
                                continue;
                            // randomly don't inv but always send inventory to spv clients
                            if (((rnd.rand32() % 100) < randomlyDontInv.Value()) && !pto->fClient)
                                continue;
                            // skip if we already know about this one
                            if (pto->filterInventoryKnown.contains(inv.hash))
                                continue;
                        }
                        vInvSend.push_back(inv);
                        pto->filterInventoryKnown.insert(inv.hash);

                        if (vInvSend.size() >= MAX_INV_TO_SEND)
                            break;
                    }

                    if (nToErase > 0)
                    {
                        pto->vInventoryToSend.erase(
                            pto->vInventoryToSend.begin(), pto->vInventoryToSend.begin() + nToErase);
                    }
                    else // exit out of the while loop if nothing was done
                    {
                        break;
                    }
                }

                // To maintain proper locking order we have to push the message when we do not hold cs_inventory which
                // was held in the section above.
                if (nToErase > 0)
                {
                    LOCK(pto->cs_vSend);
                    if (!vInvSend.empty())
                    {
                        pto->PushMessage(NetMsgType::INV, vInvSend);
                        vInvSend.clear();
                    }
                }
            }
        }

        // If the chain is not entirely sync'd then look for new blocks to download.
        //
        // Also check an edge condition, where we've invalidated a chain and set the pindexBestHeader to the
        // new most work chain, as a result we may end up just connecting whatever blocks are in setblockindexcandidates
        // resulting in pindexBestHeader equalling the chainActive.Tip() causing us to stop checking for more blocks to
        // download (our chain will now not sync until the next block announcement is received). Therefore, if the
        // best invalid chain work is still greater than our chaintip then we have to keep looking for more blocks
        // to download.
        //
        // Use temporaries for the chain tip and best invalid because they are both atomics and either could
        // be nullified between the two calls.
        CBlockIndex *pTip = chainActive.Tip();
        CBlockIndex *pBestInvalid = pindexBestInvalid.load();
        if (!IsChainSyncd() || (pBestInvalid && pTip && pBestInvalid->nChainWork > pTip->nChainWork))
        {
            TRY_LOCK(cs_main, locked);
            if (locked)
            { // I don't need to deal w/ blocks as often as tx and this is time consuming
                // Request the next blocks. Mostly this will get executed during IBD but sometimes even
                // when the chain is syncd a block will get request via this method.
                requester.RequestNextBlocksToDownload(pto);
            }
        }
    }
    return true;
}
