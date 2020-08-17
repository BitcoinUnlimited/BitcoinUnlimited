#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
from test_framework.nodemessages import *
import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging
logging.getLogger().setLevel(logging.INFO)
logf = open("ctorout.txt","a")
logging.getLogger().addHandler(logging.StreamHandler(logf))

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

def activeTipFromChainTips(node):
    ct = node.getchaintips()
    tip = next(x for x in ct if x["status"] == "active")
    return tip

def thereExists(lst, fn):
    for l in lst:
        if fn(l):
            return True
    return False

def dumpNodeInfo(nodes):
    ret = []
    ret.append(str(["_" if x == None else x.getblockcount() for x in nodes]))
    ret.append(str(["_" if x == None else x.getbestblockhash() for x in nodes]))
    count = 0
    for n in nodes:
        if n != None:  # in case a node is off, you can leave a gap in the list so the idx will be correct
            ret.append("\n")
            ret.append("Node %d:" % count)
            ret.append("%d: NETWORK INFO:" % count)
            ret.append(str(n.getnetworkinfo()))
            ret.append("%d: PEER INFO:" % count)
            ret.append(str(n.getpeerinfo()))
            ret.append("%d: CHAIN TIPS:" % count)
            ret.append(str(n.getchaintips()))
            ret.append("%d: MEM POOL:" % count)
            ret.append(str(n.getmempoolinfo()))
            ret.append(str(n.getrawmempool()))
            ret.append("%d: ORPHAN POOL:" % count)
            ret.append(str(n.getorphanpoolinfo()))
            ret.append(str(n.getraworphanpool()))
            ret.append("%d: INFO:" % count)
            ret.append(str(n.getinfo()))
            ret.append("%d: BANNED:" % count)
            ret.append(str(n.listbanned()))
        count+=1
    return "\n".join(ret)

class CtorTest (BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        logging.info("Initializing test directory "+self.options.tmpdir)
        # we don't want the cache because we want to definitely have dtor blocks initially
        initialize_chain_clean(self.options.tmpdir, 7, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(4, self.options.tmpdir, binary=[self.bitcoindBin]*4)
        setup_connection_tracking(self.nodes)
        # Now interconnect the nodes
        connect_nodes_full(self.nodes[:3])
        connect_nodes_bi(self.nodes,2,3)
        self.is_network_split=False

    def run_test (self):
        decimal.getcontext().prec = 16  # 8 digits to get to 21million, and each bitcoin is 100 million satoshis

        for i in range(0,10):
            logging.info("generate 10 blocks")
            self.nodes[0].generate(10)
            self.sync_blocks()
        self.nodes[0].generate(1)
        self.sync_blocks()
        logging.info("initial blocks generated")


        assert_equal(self.nodes[0].getbalance(), 50, lambda: logging.error(dumpNodeInfo(self.nodes)))

        # create an alternate node that we'll use to test rollback across the fork point
        rollbackNode = start_node(6, self.options.tmpdir)
        connect_nodes(rollbackNode,0)
        rollbackAddr = rollbackNode.getnewaddress()
        waitFor(20, lambda: rollbackNode.getblockcount() == 101)

        # make a chain of dependent transactions
        addr = [ x.getnewaddress() for x in self.nodes]

        # get rid of most of my coins
        tx = self.nodes[0].sendtoaddress(rollbackAddr, 9)
        logging.info("Node 0 sent tx: %s" % tx)
        for a in addr: # give all nodes some balance to work with
            tx = self.nodes[0].sendtoaddress(a, 5)
            logging.info("Node 0 sent tx: %s" % tx)

        # now send tx to myself that must reuse coins because I don't have enough
        for i in range(1, 20):
            try:
                tx = self.nodes[0].sendtoaddress(addr[0], 21-i)
                logging.info("Node 0 sent tx: %s" % tx)
            except JSONRPCException as e: # an exception you don't catch is a testing error
                raise

        # rollback node will have a bunch of tx and the one that give it coins
        waitFor(30, lambda: rollbackNode.getmempoolinfo()["size"] >= 20)
        # now isolate it for the rollback test at the end
        disconnect_all(rollbackNode)

        # force CTOR on node 2,3
        self.nodes[2].set("consensus.enableCanonicalTxOrder=1")
        self.nodes[3].set("consensus.enableCanonicalTxOrder=1")
        waitFor(5, lambda: "True" in str(self.nodes[2].get("consensus.enableCanonicalTxOrder")))
        waitFor(5, lambda: "True" in str(self.nodes[3].get("consensus.enableCanonicalTxOrder")))

        waitFor(30, lambda: self.nodes[2].getmempoolinfo()["size"] >= 20)

        preForkHash = self.nodes[0].getbestblockhash()
        # generate a non-ctor block.  The chance that the 40 generated tx happen to be in order is 1/40! (factorial not bang)
        dtorBlock = self.nodes[0].generate(1)[0]
        logging.info("Node 0 generated dtor block %s" % dtorBlock)
        sync_blocks(self.nodes[0:2])
        # verify that n1 which is in DTOR mode accept the just generated block
        logging.info("Check block heights")
        waitFor(30, lambda: True if self.nodes[1].getblockcount() == 102 else logging.info(str([x.getblockcount() for x in self.nodes])))
        assert_equal(self.nodes[1].getblockcount(), 102, lambda x: logging.info(dumpNodeInfo(self.nodes)))

        # check that a CTOR node rejected the block
        waitFor(30, lambda: "invalid" in str(self.nodes[2].getchaintips()))
        ct = self.nodes[2].getchaintips()
        tip = next(x for x in ct if x["status"] == "active")
        assert_equal(tip["height"], 101)
        try:
            invalid = next(x for x in ct if x["status"] == "invalid")
        except Exception as e:
            print(str(ct))
            AssertionError("Incorrect chain status: " + str(e))
        assert_equal(invalid["height"], 102, lambda: logging.error(dumpNodeInfo(self.nodes)))

        # logging.error(dumpNodeInfo(self.nodes + [None, None, rollbackNode]))
        # Now generate a CTOR block
        ctorBlock = self.nodes[2].generate(1)[0]
        logging.info("Node 2 generated ctor block %s" % ctorBlock)
        sync_blocks(self.nodes[2:])
        # did the other CTOR node accept it?
        assert_equal(self.nodes[3].getblockcount(), 102, lambda: logging.error(dumpNodeInfo(self.nodes + [None, None, rollbackNode])))
        assert_equal(self.nodes[3].getbestblockhash(), ctorBlock)
        logging.info("blk count")
        waitFor(10, lambda: self.nodes[0].getblockcount() == 102)
        # did the original nodes reject it?
        # we need to generate another block so the CTOR chain exceeds the DTOR
        blkhash = self.nodes[2].generate(1)[0]
        logging.info("Node 2 generated ctorblock+1 %s" % blkhash)
        sync_blocks_to(103, self.nodes[2:])
        waitFor(10, lambda: thereExists(self.nodes[0].getchaintips(), lambda x: x["height"] == 103 and x["status"] != "headers-only"))
        logging.info("blk count 2")
        waitFor(10, lambda: self.nodes[0].getblockcount() == 102)
        logging.info("blk count 2 passed")
        waitFor(20, lambda: True if activeTipFromChainTips(self.nodes[0])['height'] == 102 else logging.info( self.nodes[0].getchaintips()))
        # assert_equal(tip["height"], 102, lambda: logging.error(dumpNodeInfo(self.nodes + [None, None, rollbackNode])))
        logging.info("chaintips ok")
        ct = self.nodes[0].getchaintips()
        invalid = next(x for x in ct if x["status"] == "invalid")
        assert_equal(invalid["height"], 103, lambda: logging.error(dumpNodeInfo(self.nodes + [None, None, rollbackNode])))

        # Let's look at the CTOR block and verify the order of the TX in it
        blkdata = self.nodes[2].getblock(ctorBlock)
        txhashes = []
        last = 0
        for tx in blkdata["tx"][1:]: # skip coinbase
            val = uint256_from_bigendian(tx)
            txhashes.append(val)
            assert(last < val)
            last = val

        # At this point we have 2 chains. Heights: common: 101, dtor:102, ctor: 103

        # dtor rollback test
        disconnect_all(self.nodes[1])
        disconnect_all(self.nodes[0])

        for j in range(5):
            for i in range(3):
                tx = self.nodes[1].sendtoaddress(addr[1], 4-i)
                logging.info("Node 1 sent tx: %s" % tx)
            blk = self.nodes[1].generate(1)[0]
            logging.info("Node 1 generated %s" % blk)

        connect_nodes_bi(self.nodes,0,1)
        sync_blocks(self.nodes[0:2])
        logging.info("Nodes 0,1 at block: %d " % self.nodes[2].getblockcount())
        # make n0 send coin to itself 4*3 times
        for j in range(4):
            for i in range(3):
                tx = self.nodes[0].sendtoaddress(addr[0], 4-i)
                logging.info("Node 0 sent tx: %s" % tx)
            blk = self.nodes[0].generate(1)[0]
            logging.info("Node 0 generated %s" % blk)
        connect_nodes_bi(self.nodes,0,1)
        sync_blocks(self.nodes[0:2])
        logging.info("Nodes 0,1 at block: %d " % self.nodes[2].getblockcount())
        waitFor(10, lambda: self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash())

        # ctor rollback test
        disconnect_all(self.nodes[3])
        for j in range(5):
            for i in range(3):
                self.nodes[3].sendtoaddress(addr[3], 4-i)
            blk = self.nodes[3].generate(1)[0]
            logging.info("Node 3 generated %s" % blk)

        connect_nodes_bi(self.nodes,2,3)
        sync_blocks(self.nodes[2:])
        logging.info("Nodes 2,3 at block: %d " % self.nodes[2].getblockcount())

        for j in range(4):
            for i in range(3):
                self.nodes[2].sendtoaddress(addr[2], 4-i)
            blk = self.nodes[2].generate(1)[0]
            logging.info("Node 2 generated %s" % blk)
        
        connect_nodes_bi(self.nodes,2,3)
        sync_blocks(self.nodes[2:])
        logging.info("Nodes 2,3 at block: %d " % self.nodes[2].getblockcount())
        waitFor(10, lambda: self.nodes[2].getbestblockhash() == self.nodes[3].getbestblockhash())

        # push the dtor chain beyond ctor by 29 blocks
        for i in range(0,30):
            self.nodes[0].sendtoaddress(addr[0], 1)
            blk = self.nodes[0].generate(1)[0]
            logging.info("(DTOR) Node 0 generated %s" % blk)
        sync_blocks(self.nodes[0:2])
        logging.info("Nodes 0,1 at block: %d " % self.nodes[0].getblockcount())

        # Test that two new nodes that come up IBD and follow the appropriate chains
        ctorTip = self.nodes[3].getbestblockhash()
        ctorTipHeight = self.nodes[3].getblockcount()
        dtorTip = self.nodes[0].getbestblockhash()
        dtorTipHeight = self.nodes[0].getblockcount()

        self.nodes.append(start_node(4, self.options.tmpdir))
        self.nodes[4].set("consensus.enableCanonicalTxOrder=1")
        waitFor(5, lambda: "True" in str(self.nodes[4].get("consensus.enableCanonicalTxOrder")))
        for i in range(5):
            connect_nodes_bi(self.nodes,4,i)

        self.nodes.append(start_node(5, self.options.tmpdir, ["-debug"]))
        self.nodes[5].set("consensus.enableCanonicalTxOrder=0")
        waitFor(5, lambda: "False" in str(self.nodes[5].get("consensus.enableCanonicalTxOrder")))

        # node 5 is non-ctor
        for i in range(5):
            connect_nodes_bi(self.nodes,5,i)

        logging.info("CTOR tip height is: %d,  DTOR is: %d" % (ctorTipHeight, dtorTipHeight))

        waitFor(120, lambda: True if self.nodes[4].getblockcount() == ctorTipHeight else logging.info("Syncing %d of %d (%s)" % (self.nodes[4].getblockcount(), ctorTipHeight, self.nodes[4].getbestblockhash())))
        assert_equal(self.nodes[4].getbestblockhash(), ctorTip)
        waitFor(120, lambda: True if self.nodes[5].getblockcount() == dtorTipHeight else logging.info("Syncing %d of %d (%s)" % (self.nodes[5].getblockcount(), ctorTipHeight, self.nodes[5].getbestblockhash())))
        assert_equal(self.nodes[5].getbestblockhash(), dtorTip)

        # Now run the rollback across fork test
        logging.info("Rollback across fork test")

        # first generate a competing ctor fork on our isolated node that is longer than the current fork
        rollbackNode.set("consensus.enableCanonicalTxOrder=1")
        waitFor(5, lambda: "True" in str(rollbackNode.get("consensus.enableCanonicalTxOrder")))

        # make the new fork longer than current
        for n in range(10):
            time.sleep(.1)
            blk = rollbackNode.generate(1)[0]
            logging.info("Rollback node generated block %s" % blk)

        preFork = rollbackNode.generate(1)[0] # will be a dtor block
        preForkBlock = rollbackNode.getblock(preFork)

        # now send tx to myself that will reuse coins.  This creates an block incompatible with dtor
        bal = rollbackNode.getbalance()
        for i in range(1,20):
            txh = rollbackNode.sendtoaddress(rollbackAddr, bal-Decimal(i*.01)) # a little less each time to account for fees
            logging.info("Rollback node generated tx %s" % txh)
        
        ctorForkHash = rollbackNode.generate(1)[0]
        blks = rollbackNode.generate(5)
        logging.info("(CTOR) generated: %s" % str(blks))
        time.sleep(3)
        ctorForkTipHash = rollbackNode.getbestblockhash()
        ctorForkTipCount = rollbackNode.getblockcount()

        # enable ctor
        # now when 2 and 3 see the other fork, they should switch to it.
        disconnect_all(self.nodes[2])
        disconnect_all(self.nodes[3])
        self.nodes[2].set("consensus.enableCanonicalTxOrder=1")
        self.nodes[3].set("consensus.enableCanonicalTxOrder=1")
        waitFor(5, lambda: "True" in str(self.nodes[2].get("consensus.enableCanonicalTxOrder")))
        waitFor(5, lambda: "True" in str(self.nodes[3].get("consensus.enableCanonicalTxOrder")))
        logging.info("enable ctor")

        connect_nodes(rollbackNode,2)
        connect_nodes(rollbackNode,3)
        waitFor(120, lambda: True if self.nodes[2].getblockcount() == ctorForkTipCount else logging.info("node 2 syncing %d of %d" % (self.nodes[2].getblockcount(), ctorForkTipCount )))
        waitFor(120, lambda: True if self.nodes[3].getblockcount() == ctorForkTipCount else logging.info("node 3 syncing %d of %d" % (self.nodes[3].getblockcount(), ctorForkTipCount )))

        logging.info("done!  Stopping nodes")
        # done, clean up
        stop_nodes([rollbackNode])


if __name__ == '__main__':
    result = CtorTest().main (bitcoinConfDict={"keypool": 5, "limitdescendantsize": 50,
                                     "use-thinblocks": 1,
                                     "use-grapheneblocks": 0,
                                     "consensus.enableCanonicalTxOrder" : 0})
    print("CTOR test complete")
    sys.exit(result)

# Create a convenient function for an interactive python debugging session
def Test():
    t = CtorTest()
    t.drop_to_pdb=True
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "limitdescendantsize": 50, # allow lots of child tx so we can tease apart ctor vs dependent order
        "use-thinblocks": 1,
        "use-grapheneblocks": 0,
        "consensus.enableCanonicalTxOrder" : 0,
        "keypool": 5
    }

    flags = standardFlags()
    # flags[0] = "--tmpdir=/tmp/test/t"
    t.main(flags, bitcoinConf, None)
