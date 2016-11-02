// Copyright (c) 2016 Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// All global variables that have construction/destruction dependencies
// must be placed in this file so that the ctor/dtor order is correct.

// Independent global variables may be placed here for organizational
// purposes.

#include "alert.h"
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

proxyType proxyInfo[NET_MAX];
proxyType nameProxy;
CCriticalSection cs_proxyInfos;

map<uint256, CAlert> mapAlerts;
CCriticalSection cs_mapAlerts;

set<uint256> setPreVerifiedTxHash;
set<uint256> setUnVerifiedOrphanTxHash;
CCriticalSection cs_xval;

bool fIsChainNearlySyncd;
CCriticalSection cs_ischainnearlysyncd;

uint64_t maxGeneratedBlock = DEFAULT_MAX_GENERATED_BLOCK_SIZE;
unsigned int excessiveBlockSize = DEFAULT_EXCESSIVE_BLOCK_SIZE;
unsigned int excessiveAcceptDepth = DEFAULT_EXCESSIVE_ACCEPT_DEPTH;
unsigned int maxMessageSizeMultiplier = DEFAULT_MAX_MESSAGE_SIZE_MULTIPLIER;
int nMaxOutConnections = DEFAULT_MAX_OUTBOUND_CONNECTIONS;

uint32_t blockVersion = 0;  // Overrides the mined block version if non-zero

vector<string> BUComments = vector<string>();
string minerComment;

// Variables for traffic shaping
CLeakyBucket receiveShaper(DEFAULT_MAX_RECV_BURST, DEFAULT_AVE_RECV);
CLeakyBucket sendShaper(DEFAULT_MAX_SEND_BURST, DEFAULT_AVE_SEND);
boost::chrono::steady_clock CLeakyBucket::clock;

// Variables for statistics tracking, must be before the "requester" singleton instantiation
const char* sampleNames[] = { "sec10", "min5", "hourly", "daily","monthly"};
int operateSampleCount[] = { 30,       12,   24,  30 };
int interruptIntervals[] = { 30,       30*12,   30*12*24,   30*12*24*30 };

CTxMemPool mempool(::minRelayTxFee);

boost::posix_time::milliseconds statMinInterval(10000);
boost::asio::io_service stat_io_service __attribute__((init_priority(101)));

CStatMap statistics __attribute__((init_priority(102)));
CTweakMap tweaks;

// The following are from net.cpp
CCriticalSection cs_mapLocalHost;
map<CNetAddr, LocalServiceInfo> mapLocalHost;
map<CInv, CDataStream> mapRelay;
deque<pair<int64_t, CInv> > vRelayExpiration;
CCriticalSection cs_mapRelay;

vector<CNode*> vNodes;
CCriticalSection cs_vNodes;
list<CNode*> vNodesDisconnected;
CSemaphore*  semOutbound = NULL;
CSemaphore*  semOutboundAddNode = NULL; // BU: separate semaphore for -addnodes
CNodeSignals g_signals;

deque<string> vOneShots;
CCriticalSection cs_vOneShots;

set<CNetAddr> setservAddNodeAddresses;
CCriticalSection cs_setservAddNodeAddresses;

vector<std::string> vAddedNodes;
CCriticalSection cs_vAddedNodes;

vector<std::string> vUseDNSSeeds;
CCriticalSection cs_vUseDNSSeeds;

CCriticalSection cs_nLastNodeId;

map<CNetAddr, ConnectionHistory> mapInboundConnectionTracker;
CCriticalSection cs_mapInboundConnectionTracker;

// BU: change locking of orphan map from using cs_main to cs_orphancache.  There is too much dependance on cs_main locks which
//     are generally too broad in scope.
CCriticalSection cs_orphancache;
map<uint256, COrphanTx> mapOrphanTransactions GUARDED_BY(cs_orphancache);
map<uint256, set<uint256> > mapOrphanTransactionsByPrev GUARDED_BY(cs_orphancache);

CTweakRef<unsigned int> ebTweak("net.excessiveBlock","Excessive block size in bytes", &excessiveBlockSize,&ExcessiveBlockValidator);
CTweakRef<unsigned int> eadTweak("net.excessiveAcceptDepth","Excessive block chain acceptance depth in blocks", &excessiveAcceptDepth);
CTweakRef<int> maxOutConnectionsTweak("net.maxOutboundConnections","Maximum number of outbound connections", &nMaxOutConnections,&OutboundConnectionValidator);
CTweakRef<int> maxConnectionsTweak("net.maxConnections","Maximum number of connections connections",&nMaxConnections);
CTweakRef<unsigned int> triTweak("net.txRetryInterval","How long to wait in microseconds before requesting a transaction from another source", &MIN_TX_REQUEST_RETRY_INTERVAL);  // When should I request a tx from someone else (in microseconds). cmdline/bitcoin.conf: -txretryinterval
CTweakRef<unsigned int> briTweak("net.blockRetryInterval","How long to wait in microseconds before requesting a block from another source", &MIN_BLK_REQUEST_RETRY_INTERVAL); // When should I request a block from someone else (in microseconds). cmdline/bitcoin.conf: -blkretryinterval

CTweakRef<std::string> subverOverrideTweak("net.subversionOveride","If set, this field will override the normal subversion field.  This is useful if you need to hide your node.",&subverOverride,&SubverValidator);

CStatHistory<unsigned int, MinValMax<unsigned int> > txAdded; //"memPool/txAdded");
CStatHistory<uint64_t, MinValMax<uint64_t> > poolSize; // "memPool/size",STAT_OP_AVE);
CStatHistory<uint64_t > recvAmt; 
CStatHistory<uint64_t > sendAmt; 

// Thin block statistics need to be located here to ensure that the "statistics" global variable is constructed first
CStatHistory<uint64_t> CThinBlockStats::nOriginalSize("thin/blockSize", STAT_OP_SUM | STAT_KEEP);
CStatHistory<uint64_t> CThinBlockStats::nThinSize("thin/thinSize", STAT_OP_SUM | STAT_KEEP);
CStatHistory<uint64_t> CThinBlockStats::nBlocks("thin/numBlocks", STAT_OP_SUM | STAT_KEEP);
CStatHistory<uint64_t> CThinBlockStats::nMempoolLimiterBytesSaved("nSize", STAT_OP_SUM | STAT_KEEP);
CStatHistory<uint64_t> CThinBlockStats::nTotalBloomFilterBytes("nSizeBloom", STAT_OP_SUM | STAT_KEEP);
CCriticalSection cs_thinblockstats;
map<int64_t, pair<uint64_t, uint64_t> > CThinBlockStats::mapThinBlocksInBound;
map<int64_t, int> CThinBlockStats::mapThinBlocksInBoundReRequestedTx;
map<int64_t, pair<uint64_t, uint64_t> > CThinBlockStats::mapThinBlocksOutBound;
map<int64_t, uint64_t> CThinBlockStats::mapBloomFiltersInBound;
map<int64_t, uint64_t> CThinBlockStats::mapBloomFiltersOutBound;
map<int64_t, double> CThinBlockStats::mapThinBlockResponseTime;
map<int64_t, double> CThinBlockStats::mapThinBlockValidationTime;



// Expedited blocks
vector<CNode*> xpeditedBlk; // (256,(CNode*)NULL);    // Who requested expedited blocks from us
vector<CNode*> xpeditedBlkUp; //(256,(CNode*)NULL);  // Who we requested expedited blocks from
vector<CNode*> xpeditedTxn; // (256,(CNode*)NULL);  

// Request management
CRequestManager requester;

// BUIP010 Xtreme Thinblocks Variables
map<uint256, uint64_t> mapThinBlockTimer;

//! The largest block size that we have seen since startup
uint64_t nLargestBlockSeen=BLOCKSTREAM_CORE_MAX_BLOCK_SIZE; // BU - Xtreme Thinblocks

CMainCleanup instance_of_cmaincleanup;

CNetCleanup cnet_instance_cleanup;  // Must construct after statistics, because CNodes use statistics.  In particular, seg fault on osx during exit because constructor/destructor order is not guaranteed between modules in clang.
