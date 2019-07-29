// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sstream>
#include <string>

#include "connmgr.h"
#include "dosman.h"
#include "expedited.h"
#include "main.h" // Misbehaving, cs_main
#include "validation/validation.h"


#define NUM_XPEDITED_STORE 10

// Just save the last few expedited sent blocks so we don't resend (uint256)
static uint256 xpeditedBlkSent[NUM_XPEDITED_STORE];

// zeros on construction)
static int xpeditedBlkSendPos = 0;

bool CheckAndRequestExpeditedBlocks(CNode *pfrom)
{
    if (pfrom->nVersion >= EXPEDITED_VERSION)
    {
        for (std::string &strAddr : mapMultiArgs["-expeditedblock"])
        {
            std::string strListeningPeerIP;
            std::string strPeerIP = pfrom->addr.ToString();
            // Add the peer's listening port if it was provided (only misbehaving clients do not provide it)
            if (pfrom->addrFromPort != 0)
            {
                // get addrFromPort
                std::ostringstream ss;
                ss << pfrom->addrFromPort;

                int pos1 = strAddr.rfind(":");
                int pos2 = strAddr.rfind("]:");
                if (pos1 <= 0 && pos2 <= 0)
                    strAddr += ':' + ss.str();

                pos1 = strPeerIP.rfind(":");
                pos2 = strPeerIP.rfind("]:");

                // Handle both ipv4 and ipv6 cases
                if (pos1 <= 0 && pos2 <= 0)
                    strListeningPeerIP = strPeerIP + ':' + ss.str();
                else if (pos1 > 0)
                    strListeningPeerIP = strPeerIP.substr(0, pos1) + ':' + ss.str();
                else
                    strListeningPeerIP = strPeerIP.substr(0, pos2) + ':' + ss.str();
            }
            else
                strListeningPeerIP = strPeerIP;

            if (strAddr == strListeningPeerIP)
                connmgr->PushExpeditedRequest(pfrom, EXPEDITED_BLOCKS);
        }
    }
    return false;
}

bool HandleExpeditedRequest(CDataStream &vRecv, CNode *pfrom)
{
    uint64_t options;
    vRecv >> options;

    if (!pfrom->ThinBlockCapable() || !IsThinBlocksEnabled())
    {
        dosMan.Misbehaving(pfrom, 5);
        return false;
    }

    if (options & EXPEDITED_STOP)
        connmgr->DisableExpeditedSends(pfrom, options & EXPEDITED_BLOCKS, options & EXPEDITED_TXNS);
    else
        connmgr->EnableExpeditedSends(pfrom, options & EXPEDITED_BLOCKS, options & EXPEDITED_TXNS, false);

    return true;
}

static inline bool IsRecentlyExpeditedAndStore(const uint256 &hash)
{
    AssertLockHeld(connmgr->cs_expedited);

    for (int i = 0; i < NUM_XPEDITED_STORE; i++)
        if (xpeditedBlkSent[i] == hash)
            return true;

    xpeditedBlkSent[xpeditedBlkSendPos] = hash;
    xpeditedBlkSendPos++;
    if (xpeditedBlkSendPos >= NUM_XPEDITED_STORE)
        xpeditedBlkSendPos = 0;

    return false;
}

bool HandleExpeditedBlock(CDataStream &vRecv, CNode *pfrom)
{
    unsigned char hops;
    unsigned char msgType;

    if (!connmgr->IsExpeditedUpstream(pfrom))
        return false;

    vRecv >> msgType >> hops;
    if (msgType == EXPEDITED_MSG_XTHIN)
    {
        return CXThinBlock::HandleMessage(vRecv, pfrom, NetMsgType::XPEDITEDBLK, hops + 1);
    }
    else
    {
        return error(
            "Received unknown (0x%x) expedited message from peer %s hop %d\n", msgType, pfrom->GetLogName(), hops);
    }
}

static void ActuallySendExpeditedBlock(CXThinBlock &thinBlock, unsigned char hops, const CNode *pskip)
{
    VNodeRefs vNodeRefs(connmgr->ExpeditedBlockNodes());
    for (CNodeRef &nodeRef : vNodeRefs)
    {
        CNode *pnode = nodeRef.get();

        if (pnode->fDisconnect)
        {
            connmgr->RemovedNode(pnode);
        }
        else if (pnode != pskip) // Don't send back to the sending node to avoid looping
        {
            LOG(THIN, "Sending expedited block %s to %s\n", thinBlock.header.GetHash().ToString(), pnode->GetLogName());

            pnode->PushMessage(NetMsgType::XPEDITEDBLK, (unsigned char)EXPEDITED_MSG_XTHIN, hops, thinBlock);
            pnode->blocksSent += 1;
        }
    }
}

void SendExpeditedBlock(CXThinBlock &thinBlock, unsigned char hops, CNode *pskip)
{
    {
        LOCK(cs_main);

        // Check we have a valid header with correct timestamp
        CValidationState state;
        CBlockIndex *pindex = nullptr;
        if (!AcceptBlockHeader(thinBlock.header, state, Params(), &pindex))
        {
            LOGA("Received an invalid expedited header from peer %s\n", pskip ? pskip->GetLogName() : "none");
            return;
        }

        // Validate that the header has enough proof of work to advance the chain or at least be equal
        // to the current chain tip in case of a re-org.
        if (!pindex || pindex->nChainWork < chainActive.Tip()->nChainWork)
        {
            // Don't print out a log message here. We can sometimes get them during IBD which during
            // periods where the chain is almost syncd but really isn't. This typically happens in regtest
            // and is can be confusing to see this in the logs when trying to debug other issues.
            //
            // LOGA("Not sending expedited block %s from peer %s, does not extend longest chain\n",
            //    thinBlock.header.GetHash().ToString(), pskip ? pskip->GetLogName() : "none");
            return;
        }
    }

    LOCK(connmgr->cs_expedited);
    if (!IsRecentlyExpeditedAndStore(thinBlock.header.GetHash()))
    {
        ActuallySendExpeditedBlock(thinBlock, hops, pskip);
    }
    // else nothing else to do
}

void SendExpeditedBlock(const CBlock &block, CNode *pskip)
{
    CXThinBlock thinBlock(block);
    SendExpeditedBlock(thinBlock, 0, pskip);
}
