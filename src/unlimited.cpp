// Copyright (c) 2015 G. Andrew Stone
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "clientversion.h"
#include "chainparams.h"
#include "miner.h"
#include "consensus/consensus.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "leakybucket.h"
#include "main.h"
#include "net.h"
#include "policy/policy.h"
#include "primitives/block.h"
#include "rpcserver.h"
#include "thinblock.h"
#include "timedata.h"
#include "tinyformat.h"
#include "txmempool.h"
#include "unlimited.h"
#include "utilstrencodings.h"
#include "ui_interface.h"
#include "util.h"
#include "validationinterface.h"
#include "version.h"
#include "stat.h"
#include "tweak.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <iomanip>
#include <boost/thread.hpp>
#include <inttypes.h>
#include <queue>

using namespace std;

extern CTxMemPool mempool; // from main.cpp

bool IsTrafficShapingEnabled();

std::string ExcessiveBlockValidator(const unsigned int& value,unsigned int* item,bool validate)
{
  if (validate)
    {
      if (value < maxGeneratedBlock) 
	{
        std::ostringstream ret;
        ret << "Sorry, your maximum mined block (" << maxGeneratedBlock << ") is larger than your proposed excessive size (" << value << ").  This would cause you to orphan your own blocks.";    
        return ret.str();
	}
    }
  else  // Do anything to "take" the new value
    {
      // nothing needed
    }
  return std::string();
}

std::string OutboundConnectionValidator(const int& value,int* item,bool validate)
{
  if (validate)
    {
      if ((value < 0)||(value > 10000))  // sanity check
	{
        return "Invalid Value";
	}
      if (value < *item)
	{
	  return "This field cannot be reduced at run time since that would kick out existing connections";
	}
    }
  else  // Do anything to "take" the new value
    {
      if (value < *item)  // note that now value is the old value and *item has been set to the new.
        {
	  int diff = *item - value;
          if (semOutboundAddNode)  // Add the additional slots to the outbound semaphore
            for (int i=0; i<diff; i++)
              semOutboundAddNode->post();
	}
    }
  return std::string();
}

std::string SubverValidator(const std::string& value,std::string* item,bool validate)
{
  if (validate)
    {
    if (value.size() > MAX_SUBVERSION_LENGTH) 
    {
      return(std::string("Subversion string is too long")); 
    }
  }
  return std::string();
}


#define NUM_XPEDITED_STORE 10
uint256 xpeditedBlkSent[NUM_XPEDITED_STORE];  // Just save the last few expedited sent blocks so we don't resend (uint256 zeros on construction)
int xpeditedBlkSendPos=0;

// Push all transactions in the mempool to another node
void UnlimitedPushTxns(CNode* dest);


int32_t UnlimitedComputeBlockVersion(const CBlockIndex* pindexPrev, const Consensus::Params& params,uint32_t nTime)
{
    if (blockVersion != 0)  // BU: allow override of block version
      {
        return blockVersion;
      }
    
    int32_t nVersion = ComputeBlockVersion(pindexPrev, params);

    // turn BIP109 off by default by commenting this out: 
    // if (nTime <= params.SizeForkExpiration())
    //	  nVersion |= FORK_BIT_2MB;
 
    return nVersion;
}


void UpdateSendStats(CNode* pfrom, const char* strCommand, int msgSize, int64_t nTime)
{
  sendAmt += msgSize;
  std::string name("net/send/msg/");
  name.append(strCommand);
  CStatMap::iterator obj = statistics.find(name);
  CStatMap::iterator end = statistics.end();
  if (obj != end)
    {
      CStatBase* base = obj->second;
      if (base)
	{
	  CStatHistory<uint64_t>* stat = dynamic_cast<CStatHistory<uint64_t>*> (base);
          if (stat)
            *stat << msgSize;
	}
    }
}

void UpdateRecvStats(CNode* pfrom, const std::string& strCommand, int msgSize, int64_t nTimeReceived)
{
  recvAmt += msgSize;
  std::string name = "net/recv/msg/" + strCommand;
  CStatMap::iterator obj = statistics.find(name);
  CStatMap::iterator end = statistics.end();
  if (obj != end)
    {
      CStatBase* base = obj->second;
      if (base)
	{
	  CStatHistory<uint64_t>* stat = dynamic_cast<CStatHistory<uint64_t>*> (base);
          if (stat)
            *stat << msgSize;
	}
    }
}

void HandleExpeditedRequest(CDataStream& vRecv,CNode* pfrom)
{
  // TODO locks
  uint64_t options;
  vRecv >> options;
  bool stop = ((options & EXPEDITED_STOP) != 0);  // Are we starting or stopping expedited service?
  if (options & EXPEDITED_BLOCKS)
    {
      if (stop)  // If stopping, find the array element and clear it.
	{
          LogPrint("blk", "Stopping expedited blocks to peer %s (%d).\n", pfrom->addrName.c_str(),pfrom->id);
	  std::vector<CNode*>::iterator it = std::find(xpeditedBlk.begin(), xpeditedBlk.end(),pfrom);  
          if (it != xpeditedBlk.end())
	    {
            *it = NULL;
	    pfrom->Release();
	    }
	}
      else  // Otherwise, add the new node to the end
	{
	  std::vector<CNode*>::iterator it1 = std::find(xpeditedBlk.begin(), xpeditedBlk.end(),pfrom); 
          if (it1 == xpeditedBlk.end())  // don't add it twice
	    {
            unsigned int maxExpedited = GetArg("-maxexpeditedblockrecipients", 32);
            if (xpeditedBlk.size() < maxExpedited )
	      {
		LogPrint("blk", "Starting expedited blocks to peer %s (%d).\n", pfrom->addrName.c_str(),pfrom->id);
		std::vector<CNode*>::iterator it = std::find(xpeditedBlk.begin(), xpeditedBlk.end(),((CNode*)NULL));
		if (it != xpeditedBlk.end())
		  *it = pfrom;
		else
		  xpeditedBlk.push_back(pfrom);
		pfrom->AddRef();
	      }
            else
	      {
		LogPrint("blk", "Expedited blocks requested from peer %s (%d), but I am full.\n", pfrom->addrName.c_str(),pfrom->id);
	      }
	    }
	}
    }
  if (options & EXPEDITED_TXNS)
    {
      if (stop) // If stopping, find the array element and clear it.
	{
          LogPrint("blk", "Stopping expedited transactions to peer %s (%d).\n", pfrom->addrName.c_str(),pfrom->id);
	  std::vector<CNode*>::iterator it = std::find(xpeditedTxn.begin(), xpeditedTxn.end(),pfrom);
          if (it != xpeditedTxn.end())
	    {
            *it = NULL;
	    pfrom->Release();
	    }
	}
      else // Otherwise, add the new node to the end
	{
	  std::vector<CNode*>::iterator it1 = std::find(xpeditedTxn.begin(), xpeditedTxn.end(),pfrom);
          if (it1 == xpeditedTxn.end())  // don't add it twice
	    {
            unsigned int maxExpedited = GetArg("-maxexpeditedtxrecipients", 32);
            if (xpeditedTxn.size() < maxExpedited )
	      {
              LogPrint("blk", "Starting expedited transactions to peer %s (%d).\n", pfrom->addrName.c_str(),pfrom->id);
	      std::vector<CNode*>::iterator it = std::find(xpeditedTxn.begin(), xpeditedTxn.end(),((CNode*)NULL));
	      if (it != xpeditedTxn.end())
		*it = pfrom;
	      else
		xpeditedTxn.push_back(pfrom);
              pfrom->AddRef();
	      }
            else
	      {
		LogPrint("blk", "Expedited transactions requested from peer %s (%d), but I am full.\n", pfrom->addrName.c_str(),pfrom->id);
	      }
	    }
	}
    }
}

bool IsRecentlyExpeditedAndStore(const uint256& hash)
{
  for (int i=0;i<NUM_XPEDITED_STORE;i++)
    if (xpeditedBlkSent[i]==hash) return true;
  xpeditedBlkSent[xpeditedBlkSendPos] = hash;
  xpeditedBlkSendPos++;
  if (xpeditedBlkSendPos >=NUM_XPEDITED_STORE)  xpeditedBlkSendPos = 0;
  return false;
}

void HandleExpeditedBlock(CDataStream& vRecv,CNode* pfrom)
{
  unsigned char hops;
  unsigned char msgType;
  vRecv >> msgType >> hops;

  if (msgType == EXPEDITED_MSG_XTHIN)
    {
      CXThinBlock thinBlock;
      vRecv >> thinBlock;

      CInv inv(MSG_BLOCK, thinBlock.header.GetHash());

      BlockMap::iterator mapEntry = mapBlockIndex.find(thinBlock.header.GetHash());
      CBlockIndex *blkidx = NULL;
      unsigned int status = 0;
      if (mapEntry != mapBlockIndex.end())
	{
	  blkidx = mapEntry->second;
	  status = blkidx->nStatus;
	}
      bool newBlock = ((blkidx == NULL) || (!(blkidx->nStatus & BLOCK_HAVE_DATA)));  // If I have never seen the block or just seen an INV, treat the block as new
      int nSizeThinBlock = ::GetSerializeSize(thinBlock, SER_NETWORK, PROTOCOL_VERSION);  // TODO replace with size of vRecv for efficiency
      LogPrint("thin", "Received %s expedited thinblock %s from peer %s (%d). Hop %d. Size %d bytes. (status %d,0x%x)\n", newBlock ? "new":"repeated", inv.hash.ToString(), pfrom->addrName.c_str(),pfrom->id, hops, nSizeThinBlock,status,status);

      // Skip if we've already seen this block
      // TODO move thes above the print, once we ensure no unexpected dups.
      if (IsRecentlyExpeditedAndStore(thinBlock.header.GetHash())) return;
      if (!newBlock) 
	{
	  // TODO determine if we have the block or just have an INV to it.
	  return;
	}

      CValidationState state;
      if (!CheckBlockHeader(thinBlock.header, state, true))  // block header is bad
	{
	  // demerit the sender
	  return;
	}
      // TODO:  Start headers-only mining now

      SendExpeditedBlock(thinBlock,hops+1, pfrom); // I should push the vRecv rather than reserialize
      thinBlock.process(pfrom, nSizeThinBlock, NetMsgType::XPEDITEDBLK);
    }
  else
    {
      LogPrint("thin", "Received unknown (0x%x) expedited message from peer %s (%d). Hop %d.\n", msgType, pfrom->addrName.c_str(),pfrom->id, hops);
    }
}

void SendExpeditedBlock(CXThinBlock& thinBlock,unsigned char hops,const CNode* skip)
  {
    //bool cameFromUpstream = false;
  std::vector<CNode*>::iterator end = xpeditedBlk.end();
  for (std::vector<CNode*>::iterator it = xpeditedBlk.begin(); it != end; it++)
    {
      CNode* n = *it;
      //if (n == skip) cameFromUpstream = true;
      if ((n != skip)&&(n != NULL)) // Don't send it back in case there is a forwarding loop
	{
          if (n->fDisconnect)
	    {
	      *it = NULL;
              n->Release();
	    }
	  else
	    {
	      LogPrint("thin", "Sending expedited block %s to %s.\n", thinBlock.header.GetHash().ToString(),n->addrName.c_str());
              n->PushMessage(NetMsgType::XPEDITEDBLK, (unsigned char) EXPEDITED_MSG_XTHIN, hops, thinBlock);  // I should push the vRecv rather than reserialize
              n->blocksSent += 1;
	    }
	}
    }

#if 0  // Probably better to have the upstream explicitly request blocks from the downstream.
  // Upstream
  if (!cameFromUpstream)  // TODO, if it came from an upstream block I really want to delay for a short period and then check if we got it and then send.  But this solves some of the issue
    {
      std::vector<CNode*>::iterator end = xpeditedBlkUp.end();
      for (std::vector<CNode*>::iterator it = xpeditedBlkUp.begin(); it != end; it++)
	{
	  CNode* n = *it;
	  if ((n != skip)&&(n != NULL)) // Don't send it back to the sender in case there is a forwarding loop
	    {
	      if (n->fDisconnect)
		{
		  *it = NULL;
		  n->Release();
		}
	      else
		{
		  LogPrint("thin", "Sending expedited block %s upstream to %s.\n", thinBlock.header.GetHash().ToString(),n->addrName.c_str());
		  n->PushMessage(NetMsgType::XPEDITEDBLK, (unsigned char) EXPEDITED_MSG_XTHIN, hops, thinBlock);  // I should push the vRecv rather than reserialize
                  n->blocksSent += 1;
		}
	    }
	}
    }
#endif
  }

void SendExpeditedBlock(const CBlock& block,const CNode* skip)
{
  // If we've already put the block in our hash table, we've already sent it out
  //BlockMap::iterator it = mapBlockIndex.find(block.GetHash());
  //if (it != mapBlockIndex.end()) return;

  
  if (!IsRecentlyExpeditedAndStore(block.GetHash()))
    {
    CXThinBlock thinBlock(block);
    SendExpeditedBlock(thinBlock,0, skip);
    }
  else
    {
      // LogPrint("thin", "No need to send expedited block %s\n", block.GetHash().ToString());
    }
}

std::string UnlimitedCmdLineHelp()
{
    std::string strUsage;
    strUsage += HelpMessageGroup(_("Bitcoin Unlimited Options:"));
    strUsage += HelpMessageOpt("-blockversion=<n>", _("Generated block version number.  Value must be an integer"));
    strUsage += HelpMessageOpt("-excessiveblocksize=<n>", _("Blocks above this size in bytes are considered excessive"));
    strUsage += HelpMessageOpt("-excessiveacceptdepth=<n>", _("Excessive blocks are accepted anyway if this many blocks are mined on top of them"));
    strUsage += HelpMessageOpt("-receiveburst", _("The maximum rate that data can be received in kB/s.  If there has been a period of lower than average data rates, the client may receive extra data to bring the average back to '-receiveavg' but the data rate will not exceed this parameter."));
    strUsage += HelpMessageOpt("-sendburst", _("The maximum rate that data can be sent in kB/s.  If there has been a period of lower than average data rates, the client may send extra data to bring the average back to '-receiveavg' but the data rate will not exceed this parameter."));
    strUsage += HelpMessageOpt("-receiveavg", _("The average rate that data can be received in kB/s"));
    strUsage += HelpMessageOpt("-sendavg", _("The maximum rate that data can be sent in kB/s"));
    strUsage += HelpMessageOpt("-use-thinblocks=<n>", strprintf(_("Turn Thinblocks on or off (off: 0, on: 1, default: %d)"), 1));
    strUsage += HelpMessageOpt("-connect-thinblock=<ip:port>", _("Connect to a thinblock node(s). Blocks will only be downloaded from a thinblock peer.  If no connections are possible then regular blocks will then be downloaded form any other connected peers."));
    strUsage += HelpMessageOpt("-minlimitertxfee=<amt>", strprintf(_("Fees (in satoshi/byte) smaller than this are considered zero fee and subject to -limitfreerelay (default: %s)"), DEFAULT_MINLIMITERTXFEE));
    strUsage += HelpMessageOpt("-maxlimitertxfee=<amt>", strprintf(_("Fees (in satoshi/byte) larger than this are always relayed (default: %s)"), DEFAULT_MAXLIMITERTXFEE));
    strUsage += HelpMessageOpt("-bitnodes", _("Query for peer addresses via Bitnodes API, if low on addresses (default: 1 unless -connect)"));    
    strUsage += HelpMessageOpt("-forcebitnodes", strprintf(_("Always query for peer addresses via Bitnodes API (default: %u)"), DEFAULT_FORCEBITNODES));
    strUsage += HelpMessageOpt("-usednsseed=<host>", _("Add a custom DNS seed to use.  If at least one custom DNS seed is set, the default DNS seeds will be ignored."));
    strUsage += HelpMessageOpt("-expeditedblock=<host>", _("Request expedited blocks from this host whenever we are connected to it"));
    strUsage += HelpMessageOpt("-maxexpeditedblockrecipients=<n>", _("The maximum number of nodes this node will forward expedited blocks to"));
    strUsage += HelpMessageOpt("-maxexpeditedtxrecipients=<n>", _("The maximum number of nodes this node will forward expedited transactions to"));
    strUsage += HelpMessageOpt("-maxoutconnections=<n>", strprintf(_("Initiate at most <n> connections to peers (default: %u).  If this number is higher than --maxconnections, it will be reduced to --maxconnections."), DEFAULT_MAX_OUTBOUND_CONNECTIONS));
    return strUsage;
}

std::string FormatCoinbaseMessage(const std::vector<std::string>& comments,const std::string& customComment)
{
    std::ostringstream ss;
    if (!comments.empty())
    {
        std::vector<std::string>::const_iterator it(comments.begin());
        ss << "/" << *it;
        for(++it; it != comments.end(); ++it)
            ss << "/" << *it;
        ss << "/";
    }
    std::string ret = ss.str() + minerComment;
    return ret;
}

CNode* FindLikelyNode(const std::string& addrName)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes)
      if (pnode->addrName.find(addrName) != std::string::npos)
            return (pnode);
    return NULL;
}

UniValue expedited(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (fHelp || params.size() < 2)
        throw runtime_error(
            "expedited block|tx \"node IP addr\" on|off\n"
            "\nRequest expedited forwarding of blocks and/or transactions from a node.\nExpedited forwarding sends blocks or transactions to a node before the node requests them.  This reduces latency, potentially at the expense of bandwidth.\n"
            "\nArguments:\n"
            "1. \"block | tx\"        (string, required) choose block to send expedited blocks, tx to send expedited transactions\n"
            "2. \"node ip addr\"     (string, required) The node's IP address or IP and port (see getpeerinfo for nodes)\n"
            "3. \"on | off\"     (string, required) Turn expedited service on or off\n"
            "\nExamples:\n" +
            HelpExampleCli("expedited", "block \"192.168.0.6:8333\" on") + HelpExampleRpc("expedited", "\"block\", \"192.168.0.6:8333\", \"on\""));

    string obj = params[0].get_str();
    string strNode = params[1].get_str();

    CNode* node = FindLikelyNode(strNode);
    if (!node) {
        throw runtime_error("Unknown node");
    }

    uint64_t flags=0;
    if (obj.find("block")!= std::string::npos) flags |= EXPEDITED_BLOCKS;
    if (obj.find("blk")!= std::string::npos) flags |= EXPEDITED_BLOCKS;
    if (obj.find("tx")!= std::string::npos) flags |= EXPEDITED_TXNS;
    if (obj.find("transaction")!= std::string::npos) flags |= EXPEDITED_TXNS;
    if ((flags & (EXPEDITED_BLOCKS|EXPEDITED_TXNS))==0)
      {
        throw runtime_error("Unknown object, give 'block' or 'transaction'");
      }

    if (params.size() >= 3)
      {
      string onoff = params[2].get_str();    
      if (onoff == "off") flags |= EXPEDITED_STOP;
      if (onoff == "OFF") flags |= EXPEDITED_STOP;
      }

    // TODO: validate that the node can handle expedited blocks

    // Add or remove this node to our list of upstream nodes
    std::vector<CNode*>::iterator elem = std::find(xpeditedBlkUp.begin(), xpeditedBlkUp.end(),node); 
    if ((flags & EXPEDITED_BLOCKS)&&(flags & EXPEDITED_STOP))
      {
	if (elem == xpeditedBlkUp.end()) xpeditedBlkUp.erase(elem);
      }
    else if (flags & EXPEDITED_BLOCKS)
      {
      if (elem == xpeditedBlkUp.end())  // don't add it twice
        {
        xpeditedBlkUp.push_back(node);
        }
      }

    // Push the expedited message even if its a repeat to allow the operator to reissue the CLI command to trigger another message.
    node->PushMessage(NetMsgType::XPEDITEDREQUEST,flags);
    return NullUniValue;
}


UniValue pushtx(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "pushtx \"node\"\n"
            "\nPush uncommitted transactions to a node.\n"
            "\nArguments:\n"
            "1. \"node\"     (string, required) The node (see getpeerinfo for nodes)\n"
            "\nExamples:\n" +
            HelpExampleCli("pushtx", "\"192.168.0.6:8333\" ") + HelpExampleRpc("pushtx", "\"192.168.0.6:8333\", "));

    string strNode = params[0].get_str();

    //BU: Add lock on cs_vNodes as FindNode now requries it to prevent potential use-after-free errors
    CNode* node = NULL;
    {
        LOCK(cs_vNodes);
        node = FindNode(strNode);

        if (!node) {
#if 0
        if (strCommand == "onetry") {
            CAddress addr;
            OpenNetworkConnection(addr, NULL, strNode.c_str());
            return NullUniValue;
        }
#endif
            throw runtime_error("Unknown node");
        }

        //BU: Since we are passing node to another function, add a ref to prevent use-after-free
        //    This allows us to release the lock on cs_vNodes earlier while still protecting node from deletion
        node->AddRef();
    }
    
    UnlimitedPushTxns(node);

    //BU: Remember to release the reference we took on node to protect from use-after-free
    node->Release();

    return NullUniValue;
}

void UnlimitedPushTxns(CNode* dest)
{
    //LOCK2(cs_main, pfrom->cs_filter);
    LOCK(dest->cs_filter);
    std::vector<uint256> vtxid;
    mempool.queryHashes(vtxid);
    vector<CInv> vInv;
    BOOST_FOREACH (uint256& hash, vtxid) {
        CInv inv(MSG_TX, hash);
        CTransaction tx;
        bool fInMemPool = mempool.lookup(hash, tx);
        if (!fInMemPool)
            continue; // another thread removed since queryHashes, maybe...
        if ((dest->pfilter && dest->pfilter->IsRelevantAndUpdate(tx)) ||
            (!dest->pfilter))
            vInv.push_back(inv);
        if (vInv.size() == MAX_INV_SZ) {
            dest->PushMessage("inv", vInv);
            vInv.clear();
        }
    }
    if (vInv.size() > 0)
        dest->PushMessage("inv", vInv);
}

void settingsToUserAgentString()
{
    BUComments.clear();

    double ebInMegaBytes = (double)excessiveBlockSize/1000000;
    std::stringstream ebss;
    ebss <<std::fixed << std::setprecision(1) << ebInMegaBytes;
    std::string eb =  ebss.str();
    std::string eb_formatted;
    eb_formatted = (eb.at(eb.size() - 1) == '0' ? eb.substr(0, eb.size() - 2) : eb); //strip zero decimal
    BUComments.push_back("EB" + eb_formatted);

    int ad_formatted;
    ad_formatted = (excessiveAcceptDepth >= 9999999 ? 9999999 : excessiveAcceptDepth);
    BUComments.push_back("AD" + boost::lexical_cast<std::string>(ad_formatted));
}

void UnlimitedSetup(void)
{
    MIN_TX_REQUEST_RETRY_INTERVAL = GetArg("-txretryinterval", MIN_TX_REQUEST_RETRY_INTERVAL);
    MIN_BLK_REQUEST_RETRY_INTERVAL = GetArg("-blkretryinterval", MIN_BLK_REQUEST_RETRY_INTERVAL);
    maxGeneratedBlock = GetArg("-blockmaxsize", maxGeneratedBlock);
    blockVersion = GetArg("-blockversion", blockVersion);
    excessiveBlockSize = GetArg("-excessiveblocksize", excessiveBlockSize);
    excessiveAcceptDepth = GetArg("-excessiveacceptdepth", excessiveAcceptDepth);
    LoadTweaks();  // The above options are deprecated so the same parameter defined as a tweak will override them

    settingsToUserAgentString();
    //  Init network shapers
    int64_t rb = GetArg("-receiveburst", DEFAULT_MAX_RECV_BURST);
    // parameter is in KBytes/sec, leaky bucket is in bytes/sec.  But if it is "off" then don't multiply
    if (rb != LONG_LONG_MAX)
        rb *= 1024;
    int64_t ra = GetArg("-receiveavg", DEFAULT_MAX_RECV_BURST);
    if (ra != LONG_LONG_MAX)
        ra *= 1024;
    int64_t sb = GetArg("-sendburst", DEFAULT_MAX_RECV_BURST);
    if (sb != LONG_LONG_MAX)
        sb *= 1024;
    int64_t sa = GetArg("-sendavg", DEFAULT_MAX_RECV_BURST);
    if (sa != LONG_LONG_MAX)
        sa *= 1024;

    receiveShaper.set(rb, ra);
    sendShaper.set(sb, sa);

    txAdded.init("memPool/txAdded");
    poolSize.init("memPool/size",STAT_OP_AVE | STAT_KEEP);
    recvAmt.init("net/recv/total");
    recvAmt.init("net/send/total");
    std::vector<std::string> msgTypes = getAllNetMessageTypes();
    
    for (std::vector<std::string>::const_iterator i=msgTypes.begin(); i!=msgTypes.end();++i)
      {
	new CStatHistory<uint64_t >("net/recv/msg/" +  *i);  // This "leaks" in the sense that it is never freed, but is intended to last the duration of the program.
	new CStatHistory<uint64_t >("net/send/msg/" +  *i);  // This "leaks" in the sense that it is never freed, but is intended to last the duration of the program.
      }

    xpeditedBlk.reserve(256); 
    xpeditedBlkUp.reserve(256);
    xpeditedTxn.reserve(256);  

    // make outbound conns modifiable by the user
    int nUserMaxOutConnections = GetArg("-maxoutconnections", DEFAULT_MAX_OUTBOUND_CONNECTIONS);
    nMaxOutConnections = std::max(nUserMaxOutConnections,0);

    if (nMaxConnections < nMaxOutConnections) {
      // uiInterface.ThreadSafeMessageBox((strprintf(_("Reducing -maxoutconnections from %d to %d, because this value is higher than max available connections."), nUserMaxOutConnections, nMaxConnections)),"", CClientUIInterface::MSG_WARNING);
      LogPrintf("Reducing -maxoutconnections from %d to %d, because this value is higher than max available connections.\n", nUserMaxOutConnections, nMaxConnections);
      nMaxOutConnections = nMaxConnections;
    }

}


FILE* blockReceiptLog = NULL;

extern void UnlimitedLogBlock(const CBlock& block, const std::string& hash, uint64_t receiptTime)
{
#if 0  // remove block logging for official release
    if (!blockReceiptLog)
        blockReceiptLog = fopen("blockReceiptLog.txt", "a");
    if (blockReceiptLog) {
        long int byteLen = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
        CBlockHeader bh = block.GetBlockHeader();
        fprintf(blockReceiptLog, "%" PRIu64 ",%" PRIu64 ",%ld,%ld,%s\n", receiptTime, (uint64_t)bh.nTime, byteLen, block.vtx.size(), hash.c_str());
        fflush(blockReceiptLog);
    }
#endif    
}


std::string LicenseInfo()
{
    return FormatParagraph(strprintf(_("Copyright (C) 2015-%i The Bitcoin Unlimited Developers"), COPYRIGHT_YEAR)) + "\n\n" +
           FormatParagraph(strprintf(_("Portions Copyright (C) 2009-%i The Bitcoin Core Developers"), COPYRIGHT_YEAR)) + "\n\n" +
           FormatParagraph(strprintf(_("Portions Copyright (C) 2014-%i The Bitcoin XT Developers"), COPYRIGHT_YEAR)) + "\n\n" +
           "\n" +
           FormatParagraph(_("This is experimental software.")) + "\n" +
           "\n" +
           FormatParagraph(_("Distributed under the MIT software license, see the accompanying file COPYING or <http://www.opensource.org/licenses/mit-license.php>.")) + "\n" +
           "\n" +
           FormatParagraph(_("This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit <https://www.openssl.org/> and cryptographic software written by Eric Young and UPnP software written by Thomas Bernard.")) +
           "\n";
}


int chainContainsExcessive(const CBlockIndex* blk, unsigned int goBack)
{
    if (goBack == 0)
        goBack = excessiveAcceptDepth+EXCESSIVE_BLOCK_CHAIN_RESET;
    for (unsigned int i = 0; i < goBack; i++, blk = blk->pprev) 
    {
        if (!blk)
	  break; // we hit the beginning
        if (blk->nStatus & BLOCK_EXCESSIVE)
	  return true;
    }
    return false;
}

int isChainExcessive(const CBlockIndex* blk, unsigned int goBack)
{
    if (goBack == 0)
        goBack = excessiveAcceptDepth;
    bool recentExcessive = false;
    bool oldExcessive = false;
    for (unsigned int i = 0; i < goBack; i++, blk = blk->pprev) {
        if (!blk)
	  break; // we hit the beginning
        if (blk->nStatus & BLOCK_EXCESSIVE)
	  recentExcessive = true;
    }
 
    // Once an excessive block is built upon the chain is not excessive even if more large blocks appear.
    // So look back to make sure that this is the "first" excessive block for a while
    for (unsigned int i = 0; i < EXCESSIVE_BLOCK_CHAIN_RESET; i++, blk = blk->pprev) {
        if (!blk)
	  break; // we hit the beginning
        if (blk->nStatus & BLOCK_EXCESSIVE)
	  oldExcessive = true;
    }

    return (recentExcessive && !oldExcessive);
}

bool CheckExcessive(const CBlock& block, uint64_t blockSize, uint64_t nSigOps, uint64_t nTx)
{
    if (blockSize > excessiveBlockSize) {
        LogPrintf("Excessive block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64 " Sig:%d  :too many bytes\n", block.nVersion, block.nTime, blockSize, nTx, nSigOps);
        return true;
    }

    LogPrintf("Acceptable block: ver:%x time:%d size: %" PRIu64 " Tx:%" PRIu64 " Sig:%d\n", block.nVersion, block.nTime, blockSize, nTx, nSigOps);
    return false;
}

extern UniValue getminercomment(const UniValue& params, bool fHelp)
{
  if (fHelp || params.size() != 0)
        throw runtime_error(
            "getminercomment\n"
            "\nReturn the comment that will be put into each mined block's coinbase\n transaction after the Bitcoin Unlimited parameters."
            "\nResult\n"
            "  minerComment (string) miner comment\n"
            "\nExamples:\n" +
            HelpExampleCli("getminercomment", "") + HelpExampleRpc("getminercomment", ""));
  
  return minerComment;
}

extern UniValue setminercomment(const UniValue& params, bool fHelp)
{
  if (fHelp || params.size() != 1)
        throw runtime_error(
            "setminercomment\n"
            "\nSet the comment that will be put into each mined block's coinbase\n transaction after the Bitcoin Unlimited parameters.\n Comments that are too long will be truncated."
            "\nExamples:\n" +
            HelpExampleCli("setminercomment", "\"bitcoin is fundamentally emergent consensus\"") + HelpExampleRpc("setminercomment", "\"bitcoin is fundamentally emergent consensus\""));

  minerComment = params[0].getValStr();
  return NullUniValue;
}



UniValue getexcessiveblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getexcessiveblock\n"
            "\nReturn the excessive block size and accept depth."
            "\nResult\n"
            "  excessiveBlockSize (integer) block size in bytes\n"
            "  excessiveAcceptDepth (integer) if the chain gets this much deeper than the excessive block, then accept the chain as active (if it has the most work)\n"
            "\nExamples:\n" +
            HelpExampleCli("getexcessiveblock", "") + HelpExampleRpc("getexcessiveblock", ""));

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("excessiveBlockSize", (uint64_t)excessiveBlockSize));
    ret.push_back(Pair("excessiveAcceptDepth", (uint64_t)excessiveAcceptDepth));
    return ret;
}

UniValue setexcessiveblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() >= 3)
        throw runtime_error(
            "setexcessiveblock blockSize acceptDepth\n"
            "\nSet the excessive block size and accept depth.  Excessive blocks will not be used in the active chain or relayed until they are several blocks deep in the blockchain.  This discourages the propagation of blocks that you consider excessively large.  However, if the mining majority of the network builds upon the block then you will eventually accept it, maintaining consensus."
            "\nResult\n"
            "  blockSize (integer) excessive block size in bytes\n"
            "  acceptDepth (integer) if the chain gets this much deeper than the excessive block, then accept the chain as active (if it has the most work)\n"
            "\nExamples:\n" +
            HelpExampleCli("getexcessiveblock", "") + HelpExampleRpc("getexcessiveblock", ""));

    unsigned int ebs=0;
    if (params[0].isNum())
        ebs = params[0].get_int64();
    else {
        string temp = params[0].get_str();
        if (temp[0] == '-') boost::throw_exception( boost::bad_lexical_cast() );
        ebs = boost::lexical_cast<unsigned int>(temp);
    }

    if (ebs < maxGeneratedBlock) 
      {
      std::ostringstream ret;
      ret << "Sorry, your maximum mined block (" << maxGeneratedBlock << ") is larger than your proposed excessive size (" << ebs << ").  This would cause you to orphan your own blocks.";    
      throw runtime_error(ret.str());
      }
    excessiveBlockSize = ebs;

    if (params[1].isNum())
        excessiveAcceptDepth = params[1].get_int64();
    else {
        string temp = params[1].get_str();
        if (temp[0] == '-') boost::throw_exception( boost::bad_lexical_cast() );
        excessiveAcceptDepth = boost::lexical_cast<unsigned int>(temp);
    }

    settingsToUserAgentString();
    std::ostringstream ret;
    ret << "Excessive Block set to " << excessiveBlockSize << " bytes.  Accept Depth set to " << excessiveAcceptDepth << " blocks.";    
    return UniValue(ret.str());
}


UniValue getminingmaxblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getminingmaxblock\n"
            "\nReturn the max generated (mined) block size"
            "\nResult\n"
            "      (integer) maximum generated block size in bytes\n"
            "\nExamples:\n" +
            HelpExampleCli("getminingmaxblock", "") + HelpExampleRpc("getminingmaxblock", ""));

    return maxGeneratedBlock;
}


UniValue setminingmaxblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setminingmaxblock blocksize\n"
            "\nSet the maximum number of bytes to include in a generated (mined) block.  This command does not turn generation on/off.\n"
            "\nArguments:\n"
            "1. blocksize         (integer, required) the maximum number of bytes to include in a block.\n"
            "\nExamples:\n"
            "\nSet the generated block size limit to 8 MB\n" +
            HelpExampleCli("setminingmaxblock", "8000000") +
            "\nCheck the setting\n" + HelpExampleCli("getminingmaxblock", ""));

    uint64_t arg = 0;
    if (params[0].isNum())
        arg = params[0].get_int64();
    else {
        string temp = params[0].get_str();
        if (temp[0] == '-') boost::throw_exception( boost::bad_lexical_cast() );
        arg = boost::lexical_cast<uint64_t>(temp);
    }

    // I don't want to waste time testing edge conditions where no txns can fit in a block, so limit the minimum block size
    if (arg < 1000)
        throw runtime_error("max generated block size must be greater than 1KB");

    maxGeneratedBlock = arg;

    return NullUniValue;
}

UniValue getblockversion(const UniValue& params, bool fHelp)
{
  if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockversion\n"
            "\nReturn the block version used when mining."
            "\nResult\n"
            "      (integer) block version number\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockversion", "") + HelpExampleRpc("getblockversion", ""));
  const CBlockIndex* pindex = chainActive.Tip();
  return UnlimitedComputeBlockVersion(pindex, Params().GetConsensus(),pindex->nTime);
      
}

UniValue setblockversion(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setblockversion blockVersionNumber\n"
            "\nSet the block version number.\n"
            "\nArguments:\n"
            "1. blockVersionNumber         (integer, hex integer, 'BIP109', 'BASE' or 'default'.  Required) The block version number.\n"
            "\nExamples:\n"
            "\nVote for 2MB blocks\n" +
            HelpExampleCli("setblockversion", "BIP109") +
            "\nCheck the setting\n" + HelpExampleCli("getblockversion", ""));

    uint32_t arg = 0;

    string temp = params[0].get_str();
    if (temp == "default")
      {
        arg = 0;
      }
    else if (temp == "BIP109")
      {
        arg = BASE_VERSION | FORK_BIT_2MB;
      }
    else if (temp == "BASE")
      {
        arg = BASE_VERSION;
      }
    else if ((temp[0] == '0') && (temp[1] == 'x'))
      {
        std::stringstream ss;
        ss << std::hex << (temp.c_str()+2);
        ss >> arg;
      }
    else
      {
      arg = boost::lexical_cast<unsigned int>(temp);
      }
   
    blockVersion = arg;

    return NullUniValue;
}

bool IsTrafficShapingEnabled()
{
    int64_t max, avg;

    sendShaper.get(&max, &avg);
    if (avg != LONG_LONG_MAX || max != LONG_LONG_MAX)
        return true;

    receiveShaper.get(&max, &avg);
    if (avg != LONG_LONG_MAX || max != LONG_LONG_MAX)
        return true;

    return false;
}

UniValue gettrafficshaping(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (params.size() == 1) {
        strCommand = params[0].get_str();
    }

    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "gettrafficshaping"
            "\nReturns the current settings for the network send and receive bandwidth and burst in kilobytes per second.\n"
            "\nArguments: None\n"
            "\nResult:\n"
            "  {\n"
            "    \"sendBurst\" : 40,   (string) The maximum send bandwidth in Kbytes/sec\n"
            "    \"sendAve\" : 30,   (string) The average send bandwidth in Kbytes/sec\n"
            "    \"recvBurst\" : 20,   (string) The maximum receive bandwidth in Kbytes/sec\n"
            "    \"recvAve\" : 10,   (string) The average receive bandwidth in Kbytes/sec\n"
            "  }\n"
            "\n NOTE: if the send and/or recv parameters do not exist, shaping in that direction is disabled.\n"
            "\nExamples:\n" +
            HelpExampleCli("gettrafficshaping", "") + HelpExampleRpc("gettrafficshaping", ""));

    UniValue ret(UniValue::VOBJ);
    int64_t max, avg;
    sendShaper.get(&max, &avg);
    if (avg != LONG_LONG_MAX || max != LONG_LONG_MAX) {
        ret.push_back(Pair("sendBurst", max / 1024));
        ret.push_back(Pair("sendAve", avg / 1024));
    }
    receiveShaper.get(&max, &avg);
    if (avg != LONG_LONG_MAX || max != LONG_LONG_MAX) {
        ret.push_back(Pair("recvBurst", max / 1024));
        ret.push_back(Pair("recvAve", avg / 1024));
    }
    return ret;
}

UniValue settrafficshaping(const UniValue& params, bool fHelp)
{
    bool disable = false;
    bool badArg = false;
    string strCommand;
    CLeakyBucket* bucket = NULL;
    if (params.size() >= 2) {
        strCommand = params[0].get_str();
        if (strCommand == "send")
            bucket = &sendShaper;
        if (strCommand == "receive")
            bucket = &receiveShaper;
        if (strCommand == "recv")
            bucket = &receiveShaper;
    }
    if (params.size() == 2) {
        if (params[1].get_str() == "disable")
            disable = true;
        else
            badArg = true;
    } else if (params.size() != 3)
        badArg = true;

    if (fHelp || badArg || bucket == NULL)
        throw runtime_error(
            "settrafficshaping \"send|receive\" \"burstKB\" \"averageKB\""
            "\nSets the network send or receive bandwidth and burst in kilobytes per second.\n"
            "\nArguments:\n"
            "1. \"send|receive\"     (string, required) Are you setting the transmit or receive bandwidth\n"
            "2. \"burst\"  (integer, required) Specify the maximum burst size in Kbytes/sec (actual max will be 1 packet larger than this number)\n"
            "2. \"average\"  (integer, required) Specify the average throughput in Kbytes/sec\n"
            "\nExamples:\n" +
            HelpExampleCli("settrafficshaping", "\"receive\" 10000 1024") + HelpExampleCli("settrafficshaping", "\"receive\" disable") + HelpExampleRpc("settrafficshaping", "\"receive\" 10000 1024"));

    if (disable) {
        if (bucket)
            bucket->disable();
    } else {
        uint64_t burst;
        uint64_t ave;
        if (params[1].isNum())
            burst = params[1].get_int64();
        else {
            string temp = params[1].get_str();
            burst = boost::lexical_cast<uint64_t>(temp);
        }
        if (params[2].isNum())
            ave = params[2].get_int64();
        else {
            string temp = params[2].get_str();
            ave = boost::lexical_cast<uint64_t>(temp);
        }
        if (burst < ave) {
            throw runtime_error("Burst rate must be greater than the average rate"
                                "\nsettrafficshaping \"send|receive\" \"burst\" \"average\"");
        }

        bucket->set(burst * 1024, ave * 1024);
    }

    return NullUniValue;
}


/**
 *  BUIP010 Xtreme Thinblocks Section 
 */
bool HaveConnectThinblockNodes()
{
    // Strip the port from then list of all the current in and outbound ip addresses
    std::vector<std::string> vNodesIP;
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH (CNode* pnode, vNodes) {
           int pos = pnode->addrName.rfind(":");
           if (pos <= 0 )
               vNodesIP.push_back(pnode->addrName);
           else
               vNodesIP.push_back(pnode->addrName.substr(0, pos));
        }
    }

    // Create a set used to check for cross connected nodes.
    // A cross connected node is one where we have a connect-thinblock connection to
    // but we also have another inbound connection which is also using
    // connect-thinblock. In those cases we have created a dead-lock where no blocks 
    // can be downloaded unless we also have at least one additional connect-thinblock 
    // connection to a different node.
    std::set<std::string> nNotCrossConnected;
 
    int nConnectionsOpen = 0;
    BOOST_FOREACH(const std::string& strAddrNode, mapMultiArgs["-connect-thinblock"]) {
        std::string strThinblockNode;
        int pos = strAddrNode.rfind(":");
        if (pos <= 0 )
            strThinblockNode = strAddrNode;
        else
            strThinblockNode = strAddrNode.substr(0, pos);
        BOOST_FOREACH(std::string strAddr, vNodesIP) {
            if (strAddr == strThinblockNode) {
                nConnectionsOpen++;
                if (!nNotCrossConnected.count(strAddr))
                    nNotCrossConnected.insert(strAddr);
                else
                    nNotCrossConnected.erase(strAddr);
            }
        }
    }
    if (nNotCrossConnected.size() > 0)
        return true;
    else if (nConnectionsOpen > 0)
        LogPrint("thin", "You have a cross connected thinblock node - we may download regular blocks until you resolve the issue\n");
    return false; // Connections are either not open or they are cross connected.
} 


bool HaveThinblockNodes()
{
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH (CNode* pnode, vNodes)
            if (pnode->ThinBlockCapable())
                return true;
    }
    return false;
}

bool CheckThinblockTimer(uint256 hash)
{
    if (!mapThinBlockTimer.count(hash)) {
        mapThinBlockTimer[hash] = GetTimeMillis();
        LogPrint("thin", "Starting Preferential Thinblock timer\n");
    }
    else {
        // Check that we have not exceeded the 10 second limit.
        // If we have then we want to return false so that we can
        // proceed to download a regular block instead.
        uint64_t elapsed = GetTimeMillis() - mapThinBlockTimer[hash];
        if (elapsed > 10000) {
            LogPrint("thin", "Preferential Thinblock timer exceeded - downloading regular block instead\n");
            return false;
        }
    }
    return true;
}

void ClearThinBlockTimer(uint256 hash)
{
    if (mapThinBlockTimer.count(hash)) {
        mapThinBlockTimer.erase(hash);
        LogPrint("thin", "Clearing Preferential Thinblock timer\n");
    }
}

bool IsThinBlocksEnabled() 
{
    return GetBoolArg("-use-thinblocks", true);
}

bool CanThinBlockBeDownloaded(CNode* pto)
{
    if (pto->ThinBlockCapable() && !GetBoolArg("-connect-thinblock-force", false))
        return true;
    else if (pto->ThinBlockCapable() && GetBoolArg("-connect-thinblock-force", false)) {
        // If connect-thinblock-force is true then we have to check that this node is in fact a connect-thinblock node.

        // When -connect-thinblock-force is true we will only download thinblocks from a peer or peers that
        // are using -connect-thinblock=<ip>.  This is an undocumented setting used for setting up performance testing
        // of thinblocks, such as, going over the GFC and needing to have thinblocks always come from the same peer or 
        // group of peers.  Also, this is a one way street.  Thinblocks will flow ONLY from the remote peer to the peer
        // that has invoked -connect-thinblock.

        // Check if this node is also a connect-thinblock node
        BOOST_FOREACH(const std::string& strAddrNode, mapMultiArgs["-connect-thinblock"])
            if (pto->addrName == strAddrNode)
                return true;
    }
    return false;
}

// fIsChainNearlySyncd is updated only during startup and whenever we receive a header.
// This way we avoid having to lock cs_main so often which tends to be a bottleneck.
void IsChainNearlySyncdInit() 
{
    LOCK2(cs_main, cs_ischainnearlysyncd);
    if (!pindexBestHeader) fIsChainNearlySyncd = false;  // Not nearly synced if we don't have any blocks!
    else
      {
      if(chainActive.Height() < pindexBestHeader->nHeight - 2)
        fIsChainNearlySyncd = false;
      else
        fIsChainNearlySyncd = true;
      }
}
bool IsChainNearlySyncd()
{
    LOCK(cs_ischainnearlysyncd);
    return fIsChainNearlySyncd;
}

void BuildSeededBloomFilter(CBloomFilter& filterMemPool, std::vector<uint256>& vOrphanHashes, uint256 hash)
{
    int64_t nStartTimer = GetTimeMillis();
    seed_insecure_rand();
    std::set<uint256> setHighScoreMemPoolHashes;
    std::set<uint256> setPriorityMemPoolHashes;

    // How much of the block should be dedicated to high-priority transactions.
    // Logically this should be the same size as the DEFAULT_BLOCK_PRIORITY_SIZE however,
    // we can't be sure that a miner won't decide to mine more high priority txs and therefore
    // by including a full blocks worth of high priority tx's we cover every scenario.  And when we
    // go on to add the high fee tx's there will be an intersection between the two which then makes 
    // the total number of tx's that go into the bloom filter smaller than just the sum of the two.  
    uint64_t nBlockPrioritySize = nLargestBlockSeen * 1.5;

    // Largest projected block size used to add the high fee transactions.  We multiply it by an
    // additional factor to take into account that miners may have slighty different policies when selecting
    // high fee tx's from the pool.
    uint64_t nBlockMaxProjectedSize = nLargestBlockSeen * 1.5;

    vector<TxCoinAgePriority> vPriority;
    TxCoinAgePriorityCompare pricomparer;
    {
        LOCK2(cs_main, mempool.cs);
        CBlockIndex* pindexPrev = chainActive.Tip();
        const int nHeight = pindexPrev->nHeight + 1;
        const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

        int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                                ? nMedianTimePast
                                : GetAdjustedTime();

        // Create a sorted list of transactions and their updated priorities.  This will be used to fill
        // the mempoolhashes with the expected priority area of the next block.  We will multiply this by
        // a factor of ? to account for any differences between the "Miners".
        vPriority.reserve(mempool.mapTx.size());
        for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin();
             mi != mempool.mapTx.end(); mi++)
        {
            double dPriority = mi->GetPriority(nHeight);
            CAmount dummy;
            mempool.ApplyDeltas(mi->GetTx().GetHash(), dPriority, dummy);
            vPriority.push_back(TxCoinAgePriority(dPriority, mi));
        }
        std::make_heap(vPriority.begin(), vPriority.end(), pricomparer);

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
            BOOST_FOREACH(CTxMemPool::txiter child, mempool.GetMemPoolChildren(iter))
            {
                uint256 childHash = child->GetTx().GetHash();
                if (!setPriorityMemPoolHashes.count(childHash)) {
                    setPriorityMemPoolHashes.insert(childHash);
                    nPrioritySize += child->GetTxSize();
                    LogPrint("bloom", "add priority child %s with fee %d modified fee %d size %d clearatentry %d priority %f\n", 
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
            CTransaction tx = mi->GetTx();

            if (!IsFinalTx(tx, nHeight, nLockTimeCutoff)) {
                LogPrint("bloom", "tx %s is not final\n", tx.GetHash().ToString());
                mi++;
                continue;
            }

            // If this tx is not accounted for already in the priority set then continue and add
            // it to the high score set if it can be and also add any parents or children.  Also add
            // children and parents to the priority set tx's if they have any.
            iter = mempool.mapTx.project<0>(mi);
            if (!setHighScoreMemPoolHashes.count(tx.GetHash()))
            {
                LogPrint("bloom", "next tx is %s blocksize %d fee %d modified fee %d size %d clearatentry %d priority %f\n", 
                                   mi->GetTx().GetHash().ToString(), nBlockSize, mi->GetFee(), mi->GetModifiedFee(), mi->GetTxSize(), 
                                   mi->WasClearAtEntry(), mi->GetPriority(nHeight));

                // add tx to the set: we don't know if this is a parent or child yet.
                setHighScoreMemPoolHashes.insert(tx.GetHash());

                // Add any parent tx's
                bool fChild = false;
                BOOST_FOREACH(CTxMemPool::txiter parent, mempool.GetMemPoolParents(iter))
                {
                    fChild = true;
                    uint256 parentHash = parent->GetTx().GetHash();
                    if (!setHighScoreMemPoolHashes.count(parentHash)) {
                        setHighScoreMemPoolHashes.insert(parentHash);
                        LogPrint("bloom", "add high score parent %s with blocksize %d fee %d modified fee %d size %d clearatentry %d priority %f\n", 
                                           parent->GetTx().GetHash().ToString(), nBlockSize, parent->GetFee(), parent->GetModifiedFee(), 
                                           parent->GetTxSize(), parent->WasClearAtEntry(), parent->GetPriority(nHeight));
                    }
                }

                // Now add any children tx's.
                bool fHasChildren = false;
                BOOST_FOREACH(CTxMemPool::txiter child, mempool.GetMemPoolChildren(iter))
                {
                    fHasChildren = true;
                    uint256 childHash = child->GetTx().GetHash();
                    if (!setHighScoreMemPoolHashes.count(childHash)) {
                        setHighScoreMemPoolHashes.insert(childHash);
                        LogPrint("bloom", "add high score child %s with blocksize %d fee %d modified fee %d size %d clearatentry %d priority %f\n", 
                                           child->GetTx().GetHash().ToString(), nBlockSize, child->GetFee(), child->GetModifiedFee(), 
                                           child->GetTxSize(), child->WasClearAtEntry(), child->GetPriority(nHeight));
                    }
                }

                // If a tx with no parents and no children, then we increment this block size.  
                // We don't want to add parents and children to the size because for tx's with many children, miners may not mine them
                // as they are not as profitable but we still have to add their hash to the bloom filter in case they do.
                if (!fChild && !fHasChildren)
                    nBlockSize += mi->GetTxSize();
            }

            if (nBlockSize >  nBlockMaxProjectedSize)
                break;

            mi++;
        }
    }
    LogPrint("thin", "Bloom Filter Targeting completed in:%d (ms)\n", GetTimeMillis() - nStartTimer);
    nStartTimer= GetTimeMillis(); // reset the timer

    // We set the beginning of our growth algortithm to the time we request our first xthin.  We do this here
    // rather than setting up a global variable in init.cpp.  This has more to do with potential merge conflicts
    // with BU than any other technical reason.
    static int64_t nStartGrowth = GetTime();

    // Tuning knobs for the false positive growth algorithm
    static uint8_t nHoursToGrow = 72; // number of hours until maximum growth for false positive rate
    //static double nGrowthCoefficient = 0.7676; // use for nMinFalsePositive = 0.0001 and nMaxFalsePositive = 0.01 for 6 hour growth period
    //static double nGrowthCoefficient = 0.8831; // use for nMinFalsePositive = 0.0001 and nMaxFalsePositive = 0.02 for 6 hour growth period
    //static double nGrowthCoefficient = 0.1921; // use for nMinFalsePositive = 0.0001 and nMaxFalsePositive = 0.01 for 24 hour growth period
    static double nGrowthCoefficient = 0.0544; // use for nMinFalsePositive = 0.0001 and nMaxFalsePositive = 0.005 for 72 hour growth period
    static double nMinFalsePositive = 0.0001; // starting value for false positive 
    static double nMaxFalsePositive = 0.005; // maximum false positive rate at end of decay
    // TODO: automatically calculate the nGrowthCoefficient from nHoursToGrow, nMinFalsePositve and nMaxFalsePositive

    // Count up all the transactions that we'll be putting into the filter, removing any duplicates
    BOOST_FOREACH(uint256 txHash, setHighScoreMemPoolHashes)
        if (setPriorityMemPoolHashes.count(txHash))
            setPriorityMemPoolHashes.erase(txHash);

    unsigned int nSelectedTxHashes = setHighScoreMemPoolHashes.size() + vOrphanHashes.size() + setPriorityMemPoolHashes.size();
    unsigned int nElements = std::max(nSelectedTxHashes, (unsigned int)1); // Must make sure nElements is greater than zero or will assert

    // Calculate the new False Positive rate.  
    // We increase the false positive rate as time increases, starting at nMinFalsePositive and with growth governed by nGrowthCoefficient,
    // using the simple exponential growth function as follows:
    // y = (starting or minimum fprate: nMinFalsePositive) * e ^ (time in hours from start * nGrowthCoefficient)
    int64_t nTimePassed = GetTime() - nStartGrowth;
    double nFPRate = nMinFalsePositive * exp (((double)(nTimePassed) / 3600) * nGrowthCoefficient);
    if (nTimePassed > nHoursToGrow * 3600)
        nFPRate = nMaxFalsePositive;

    filterMemPool = CBloomFilter(nElements, nFPRate, insecure_rand(), BLOOM_UPDATE_ALL);
    LogPrint("thin", "FPrate: %f Num elements in bloom filter:%d high priority txs:%d high fee txs:%d orphans:%d total txs in mempool:%d\n", 
              nFPRate, nElements, setPriorityMemPoolHashes.size(), 
              setHighScoreMemPoolHashes.size(), vOrphanHashes.size(), mempool.mapTx.size());

    // Add the selected tx hashes to the bloom filter
    BOOST_FOREACH(uint256 txHash, setPriorityMemPoolHashes)
        filterMemPool.insert(txHash);
    BOOST_FOREACH(uint256 txHash, setHighScoreMemPoolHashes)
        filterMemPool.insert(txHash);
    BOOST_FOREACH(uint256 txHash, vOrphanHashes)
        filterMemPool.insert(txHash);
    uint64_t nSizeFilter = ::GetSerializeSize(filterMemPool, SER_NETWORK, PROTOCOL_VERSION);
    LogPrint("thin", "Created bloom filter: %d bytes for block: %s in:%d (ms)\n", nSizeFilter, hash.ToString(), GetTimeMillis() - nStartTimer);
    CThinBlockStats::UpdateOutBoundBloomFilter(nSizeFilter);
}

void LoadFilter(CNode *pfrom, CBloomFilter *filter)
{
    if (!filter->IsWithinSizeConstraints())
        // There is no excuse for sending a too-large filter
        Misbehaving(pfrom->GetId(), 100);
    else
    {
        LOCK(pfrom->cs_filter);
        delete pfrom->pThinBlockFilter;
        pfrom->pThinBlockFilter = new CBloomFilter(*filter);
        pfrom->pThinBlockFilter->UpdateEmptyFull();
    }
    uint64_t nSizeFilter = ::GetSerializeSize(*pfrom->pThinBlockFilter, SER_NETWORK, PROTOCOL_VERSION);
    LogPrint("thin", "Thinblock Bloom filter size: %d\n", nSizeFilter);
    CThinBlockStats::UpdateInBoundBloomFilter(nSizeFilter);
}

void HandleBlockMessage(CNode *pfrom, const string &strCommand, CBlock &block, const CInv &inv)
{
    int64_t startTime = GetTimeMicros();
    CValidationState state;
    uint64_t nSizeBlock = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);

    // Process all blocks from whitelisted peers, even if not requested,
    // unless we're still syncing with the network.
    // Such an unrequested block may still be processed, subject to the
    // conditions in AcceptBlock().
    bool forceProcessing = pfrom->fWhitelisted && !IsInitialBlockDownload();
    const CChainParams& chainparams = Params();
    pfrom->firstBlock += 1;
    ProcessNewBlock(state, chainparams, pfrom, &block, forceProcessing, NULL);
    int nDoS;
    if (state.IsInvalid(nDoS)) {
        LogPrintf("Invalid block due to %s\n", state.GetRejectReason().c_str());
        if (!strCommand.empty())
	  {
          pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                           state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
          if (nDoS > 0) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), nDoS);
          }
	}
    }
    else {
        nLargestBlockSeen = std::max(nSizeBlock, nLargestBlockSeen);

        double nValidationTime = (double)(GetTimeMicros() - startTime) / 1000000.0;
        if (strCommand != NetMsgType::BLOCK) {
            LogPrint("thin", "Processed ThinBlock %s in %.2f seconds\n", inv.hash.ToString(), (double)(GetTimeMicros() - startTime) / 1000000.0);
            CThinBlockStats::UpdateValidationTime(nValidationTime);
        }
        else
            LogPrint("thin", "Processed Regular Block %s in %.2f seconds\n", inv.hash.ToString(), (double)(GetTimeMicros() - startTime) / 1000000.0);
    }

    // When we request a thinblock we may get back a regular block if it is smaller than a thinblock
    // Therefore we have to remove the thinblock in flight if it exists and we also need to check that 
    // the block didn't arrive from some other peer.  This code ALSO cleans up the thin block that
    // was passed to us (&block), so do not use it after this.
    {
        int nTotalThinBlocksInFlight = 0;
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes) {
                if (pnode->mapThinBlocksInFlight.count(inv.hash)) {
                    pnode->mapThinBlocksInFlight.erase(inv.hash); 
                    pnode->thinBlockWaitingForTxns = -1;
                    pnode->thinBlock.SetNull();
                }
                if (pnode->mapThinBlocksInFlight.size() > 0)
                    nTotalThinBlocksInFlight++;
            }
        }

        // When we no longer have any thinblocks in flight then clear the set
        // just to make sure we don't somehow get growth over time.
        LOCK(cs_xval);
        if (nTotalThinBlocksInFlight == 0) {
            setPreVerifiedTxHash.clear();
            setUnVerifiedOrphanTxHash.clear();
        }
    }

    // Clear the thinblock timer used for preferential download
    ClearThinBlockTimer(inv.hash);
}

void ConnectToThinBlockNodes()
{
    // Connect to specific addresses
    if (mapArgs.count("-connect-thinblock") && mapMultiArgs["-connect-thinblock"].size() > 0)
    {
        BOOST_FOREACH(const std::string& strAddr, mapMultiArgs["-connect-thinblock"])
        {
            CAddress addr;
            //NOTE: Because the only nodes we are connecting to here are the ones the user put in their
            //      bitcoin.conf/commandline args as "-connect-thinblock", we don't use the semaphore to limit outbound connections
            OpenNetworkConnection(addr, NULL, strAddr.c_str());
            MilliSleep(500);
        }
    }
}

bool CheckAndRequestExpeditedBlocks(CNode* pfrom)
{
  if (pfrom->nVersion >= EXPEDITED_VERSION)
    {
      BOOST_FOREACH(string& strAddr, mapMultiArgs["-expeditedblock"]) 
        {
          // Add the peer's listening port if it is empty
          int pos1 = strAddr.rfind(":");
          int pos2 = strAddr.rfind("]:");
          if (pos1 <= 0 && pos2 <= 0)
              strAddr += ':' + boost::lexical_cast<std::string>(pfrom->addrFromPort);

          string strListeningPeerIP;
          string strPeerIP = pfrom->addr.ToString();
          pos1 = strPeerIP.rfind(":");
          pos2 = strPeerIP.rfind("]:");
          // Handle both ipv4 and ipv6 cases
          if (pos1 <= 0 && pos2 <= 0) 
              strListeningPeerIP = strPeerIP + ':' + boost::lexical_cast<std::string>(pfrom->addrFromPort);
          else if (pos1 > 0)
              strListeningPeerIP = strPeerIP.substr(0, pos1) + ':' + boost::lexical_cast<std::string>(pfrom->addrFromPort);
          else
              strListeningPeerIP = strPeerIP.substr(0, pos2) + ':' + boost::lexical_cast<std::string>(pfrom->addrFromPort);

	  if(strAddr == strListeningPeerIP)
            {
              if (!IsThinBlocksEnabled())
                {
                  LogPrintf("You do not have Thinblocks enabled.  You can not request expedited blocks from peer %s (%d).\n", strListeningPeerIP, pfrom->id);
                  return false;
                }
              else if (!pfrom->ThinBlockCapable())
                {
                  LogPrintf("Thinblocks is not enabled on remote peer.  You can not request expedited blocks from peer %s (%d).\n", strListeningPeerIP, pfrom->id);
                  return false;
                }
              else
                {
                  LogPrintf("Requesting expedited blocks from peer %s (%d).\n", strListeningPeerIP, pfrom->id);
                  pfrom->PushMessage(NetMsgType::XPEDITEDREQUEST, ((uint64_t) EXPEDITED_BLOCKS));
                  xpeditedBlkUp.push_back(pfrom);
                  return true;
                }
            }
        }
    }
  return false;
}

void CheckNodeSupportForThinBlocks()
{
    if(IsThinBlocksEnabled()) {
        //BU: Enforce cs_vNodes lock held external to FindNode function calls to prevent use-after-free errors
        LOCK(cs_vNodes);
        // Check that a nodes pointed to with connect-thinblock actually supports thinblocks
        BOOST_FOREACH(string& strAddr, mapMultiArgs["-connect-thinblock"]) {
            if(CNode* pnode = FindNode(strAddr)) {
                if(!pnode->ThinBlockCapable()) {
                    LogPrintf("ERROR: You are trying to use connect-thinblocks but to a node that does not support it - Protocol Version: %d peer=%d\n", 
                               pnode->nVersion, pnode->id);
                }
            }
        }
    }
}

void SendXThinBlock(CBlock &block, CNode* pfrom, const CInv &inv)
{
    if (inv.type == MSG_XTHINBLOCK)
    {
        CXThinBlock xThinBlock(block, pfrom->pThinBlockFilter);
        int nSizeBlock = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
        if (xThinBlock.collision == true) // If there is a cheapHash collision in this block then send a normal thinblock
        {
            CThinBlock thinBlock(block, *pfrom->pThinBlockFilter);
            int nSizeThinBlock = ::GetSerializeSize(xThinBlock, SER_NETWORK, PROTOCOL_VERSION);
            if (nSizeThinBlock < nSizeBlock) {
                pfrom->PushMessage(NetMsgType::THINBLOCK, thinBlock);
                CThinBlockStats::UpdateOutBound(nSizeThinBlock, nSizeBlock);
                LogPrint("thin", "TX HASH COLLISION: Sent thinblock - size: %d vs block size: %d => tx hashes: %d transactions: %d  peer: %s (%d)\n", nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.vMissingTx.size(), pfrom->addrName.c_str(), pfrom->id);
            }
            else {
                pfrom->PushMessage(NetMsgType::BLOCK, block);
                LogPrint("thin", "Sent regular block instead - xthinblock size: %d vs block size: %d => tx hashes: %d transactions: %d  peer: %s (%d)\n", nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.vMissingTx.size(), pfrom->addrName.c_str(), pfrom->id);
            }
        }
        else // Send an xThinblock
        {
            // Only send a thinblock if smaller than a regular block
            int nSizeThinBlock = ::GetSerializeSize(xThinBlock, SER_NETWORK, PROTOCOL_VERSION);
            if (nSizeThinBlock < nSizeBlock) {
                CThinBlockStats::UpdateOutBound(nSizeThinBlock, nSizeBlock);
                pfrom->PushMessage(NetMsgType::XTHINBLOCK, xThinBlock);
                LogPrint("thin", "Sent xthinblock - size: %d vs block size: %d => tx hashes: %d transactions: %d peer: %s (%d)\n", nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.vMissingTx.size(), pfrom->addrName.c_str(), pfrom->id);
            }
            else {
                pfrom->PushMessage(NetMsgType::BLOCK, block);
                LogPrint("thin", "Sent regular block instead - xthinblock size: %d vs block size: %d => tx hashes: %d transactions: %d  peer: %s (%d)\n", nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(), xThinBlock.vMissingTx.size(), pfrom->addrName.c_str(), pfrom->id);
            }
        }
    }
    else if (inv.type == MSG_THINBLOCK)
    {
        CThinBlock thinBlock(block, *pfrom->pThinBlockFilter);
        int nSizeBlock = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
        int nSizeThinBlock = ::GetSerializeSize(thinBlock, SER_NETWORK, PROTOCOL_VERSION);
        if (nSizeThinBlock < nSizeBlock) { // Only send a thinblock if smaller than a regular block
            CThinBlockStats::UpdateOutBound(nSizeThinBlock, nSizeBlock);
            pfrom->PushMessage(NetMsgType::THINBLOCK, thinBlock);
            LogPrint("thin", "Sent thinblock - size: %d vs block size: %d => tx hashes: %d transactions: %d  peer: %s (%d)\n", nSizeThinBlock, nSizeBlock, thinBlock.vTxHashes.size(), thinBlock.vMissingTx.size(), pfrom->addrName.c_str(), pfrom->id);
        }
        else {
            pfrom->PushMessage(NetMsgType::BLOCK, block);
            LogPrint("thin", "Sent regular block instead - thinblock size: %d vs block size: %d => tx hashes: %d transactions: %d  peer: %s (%d)\n", nSizeThinBlock, nSizeBlock, thinBlock.vTxHashes.size(), thinBlock.vMissingTx.size(), pfrom->addrName.c_str(), pfrom->id);
        }
    }
    else
      {
	assert(0);  // inv type is not correct 
      }
    pfrom->blocksSent += 1;
}

// Similar to TestBlockValidity but is very conservative in parameters (used in mining)
bool TestConservativeBlockValidity(CValidationState& state, const CChainParams& chainparams, const CBlock& block, CBlockIndex* pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev && pindexPrev == chainActive.Tip());
    if (fCheckpointsEnabled && !CheckIndexAgainstCheckpoint(pindexPrev, state, chainparams, block.GetHash()))
        return error("%s: CheckIndexAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return false;
    if (!CheckBlock(block, state, fCheckPOW, fCheckMerkleRoot, true))
        return false;
    if (!ContextualCheckBlock(block, state, pindexPrev))
        return false;
    if (!ConnectBlock(block, state, &indexDummy, viewNew, true))
        return false;
    assert(state.IsValid());

    return true;
}

// Statistics:

CStatBase* FindStatistic(const char* name)
{
  CStatMap::iterator item = statistics.find(name);
  if (item != statistics.end())
    return item->second;
  return NULL;
}

UniValue getstatlist(const UniValue& params, bool fHelp)
{
  if (fHelp || (params.size() != 0))
        throw runtime_error(
            "getstatlist"
            "\nReturns a list of all statistics available on this node.\n"
            "\nArguments: None\n"
            "\nResult:\n"
            "  {\n"
            "    \"name\" : (string) name of the statistic\n"
            "    ...\n"
            "  }\n"
            "\nExamples:\n" +
            HelpExampleCli("getstatlist", "") + HelpExampleRpc("getstatlist", ""));

  CStatMap::iterator it;

  UniValue ret(UniValue::VARR);
  for (it = statistics.begin(); it != statistics.end(); ++it)
    {
    ret.push_back(it->first);
    }

  return ret;
}

UniValue getstat(const UniValue& params, bool fHelp)
{
    string specificIssue;

    int count = 0;
    if (params.size() < 3) count = 1;  // if a count is not specified, give the latest sample
    else
      {
	if (!params[2].isNum()) 
	  {
          try
	    {
	      count =  boost::lexical_cast<int>(params[2].get_str());
	    }
          catch (const boost::bad_lexical_cast &)
	    {
            fHelp=true;
            specificIssue = "Invalid argument 3 \"count\" -- not a number";
  	    }
	  }
        else
	  {
	    count = params[2].get_int();
	  }
      }
    if (fHelp || (params.size() < 1))
        throw runtime_error(
            "getstat"
            "\nReturns the current settings for the network send and receive bandwidth and burst in kilobytes per second.\n"
            "\nArguments: \n"
            "1. \"statistic\"     (string, required) Specify what statistic you want\n"
            "2. \"series\"  (string, optional) Specify what data series you want.  Options are \"total\", \"now\",\"all\", \"sec10\", \"min5\", \"hourly\", \"daily\",\"monthly\".  Default is all.\n"
            "3. \"count\"  (string, optional) Specify the number of samples you want.\n"

            "\nResult:\n"
            "  {\n"
            "    \"<statistic name>\"\n"
            "    {\n"
            "    \"<series name>\"\n"
            "      [\n"
            "      <data>, (any type) The data points in the series\n"
            "      ],\n"
            "    ...\n"
            "    },\n"
            "  ...\n"            
            "  }\n"
            "\nExamples:\n" +
            HelpExampleCli("getstat", "") + HelpExampleRpc("getstat", "")
            + "\n" + specificIssue);

    UniValue ret(UniValue::VARR);

    string seriesStr;
    if (params.size() < 2)
      seriesStr = "total";
    else seriesStr = params[1].get_str();
    //uint_t series = 0; 
    //if (series == "now") series |= 1;
    //if (series == "all") series = 0xfffffff;

    CStatBase* base = FindStatistic(params[0].get_str().c_str());
    if (base)
      {
        UniValue ustat(UniValue::VOBJ);
        if (seriesStr == "now")
          {
	    ustat.push_back(Pair("now", base->GetNow()));
	  }
        else if (seriesStr == "total")
          {
	    ustat.push_back(Pair("total", base->GetTotal()));
	  }
        else
	  {
            UniValue series = base->GetSeries(seriesStr,count);
	    ustat.push_back(Pair(seriesStr,series));
	  }

        ret.push_back(ustat);  
      }

    return ret;
}
