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

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

FORK_CFG="consensus.forkNov2018Time"

class MyTest (BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        # we don't want the cache because we want to definitely have dtor blocks initially
        initialize_chain_clean(self.options.tmpdir, 7, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(4, self.options.tmpdir)
        # Now interconnect the nodes
        connect_nodes_full(self.nodes[:3])
        connect_nodes_bi(self.nodes,2,3)
        self.is_network_split=False
        self.sync_blocks()

    def run_test (self):
        decimal.getcontext().prec = 16  # 8 digits to get to 21million, and each bitcoin is 100 million satoshis

        # turn off automated forking so we can force CTOR on node 2,3
        self.nodes[0].set(FORK_CFG + "=0", "consensus.enableCanonicalTxOrder=0")
        self.nodes[1].set(FORK_CFG + "=0", "consensus.enableCanonicalTxOrder=0")

        self.nodes[0].generate(101)
        self.sync_blocks()
        assert_equal(self.nodes[0].getbalance(), 50)

        # create an alternate node that we'll use to test rollback across the fork point
        rollbackNode = start_node(6, self.options.tmpdir)
        rollbackNode.set(FORK_CFG + "=0", "consensus.enableCanonicalTxOrder=0")
        connect_nodes(rollbackNode,0)
        rollbackAddr = rollbackNode.getnewaddress()
        waitFor(20, lambda: rollbackNode.getblockcount() >= 101)

        # make a chain of dependent transactions
        addr = [ x.getnewaddress() for x in self.nodes]

        # get rid of most of my coins
        self.nodes[0].sendtoaddress(rollbackAddr, 9)
        for a in addr: # give all nodes some balance to work with
            self.nodes[0].sendtoaddress(a, 5)

        # now send tx to myself that must reuse coins because I don't have enough
        for i in range(1,20):
            try:
                self.nodes[0].sendtoaddress(addr[0], 21-i)
            except JSONRPCException as e: # an exception you don't catch is a testing error
                raise

        # rollback node will have a bunch of tx and the one that give it coins
        waitFor(30, lambda: rollbackNode.getmempoolinfo()["size"] >= 20)
        # now isolate it for the rollback test at the end
        disconnect_all(rollbackNode)

        # turn off automated forking so we can force CTOR on node 2,3
        self.nodes[2].set(FORK_CFG + "=0", "consensus.enableCanonicalTxOrder=1")
        self.nodes[3].set(FORK_CFG + "=0", "consensus.enableCanonicalTxOrder=1")

        waitFor(30, lambda: self.nodes[2].getmempoolinfo()["size"] >= 20)

        preForkHash = self.nodes[0].getbestblockhash()
        # generate a non-ctor block.  The chance that the 40 generated tx happen to be in order is 1/40! (factorial not bang)
        dtorBlock = self.nodes[0].generate(1)[0]
        sync_blocks(self.nodes[0:2])
        assert_equal(self.nodes[1].getblockcount(), 102)

        # check that a CTOR node rejected the block
        ct = self.nodes[2].getchaintips()
        tip = next(x for x in ct if x["status"] == "active")
        assert_equal(tip["height"], 101)
        invalid = next(x for x in ct if x["status"] == "invalid")
        assert_equal(invalid["height"], 102)

        # Now generate a CTOR block
        ctorBlock = self.nodes[2].generate(1)[0]
        sync_blocks(self.nodes[2:])
        # did the other CTOR node accept it?
        assert_equal(self.nodes[3].getblockcount(), 102)
        assert_equal(self.nodes[3].getbestblockhash(), ctorBlock)
        # did the original nodes reject it?
        # we need to generate another block so the CTOR chain exceeds the DTOR
        self.nodes[2].generate(1)
        sync_blocks_to(103, self.nodes[2:])
        waitFor(10, lambda: self.nodes[0].getbestblockhash() == dtorBlock)
        ct = self.nodes[0].getchaintips()
        tip = next(x for x in ct if x["status"] == "active")
        assert_equal(tip["height"], 102)
        invalid = next(x for x in ct if x["status"] == "invalid")
        assert_equal(invalid["height"], 103)

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
                self.nodes[1].sendtoaddress(addr[1], 4-i)
            self.nodes[1].generate(1)

        for j in range(4):
            for i in range(3):
                self.nodes[0].sendtoaddress(addr[0], 4-i)
            self.nodes[0].generate(1)
        connect_nodes_bi(self.nodes,0,1)

        waitFor(10, lambda: self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash())

        # ctor rollback test
        disconnect_all(self.nodes[3])
        for j in range(5):
            for i in range(3):
                self.nodes[3].sendtoaddress(addr[3], 4-i)
            self.nodes[3].generate(1)

        for j in range(4):
            for i in range(3):
                self.nodes[2].sendtoaddress(addr[2], 4-i)
            self.nodes[2].generate(1)
        connect_nodes_bi(self.nodes,2,3)
        #print(self.nodes[2].getbestblockhash())
        #print(self.nodes[3].getbestblockhash())
        #print(self.nodes[2].getchaintips())
        waitFor(10, lambda: self.nodes[2].getbestblockhash() == self.nodes[3].getbestblockhash())

        # push the dtor chain beyond ctor
        for i in range(0,30):
            self.nodes[0].sendtoaddress(addr[0], 1)
            self.nodes[0].generate(1)

        # Test that two new nodes that come up IBD and follow the appropriate chains
        ctorTip = self.nodes[3].getbestblockhash()
        ctorTipHeight = self.nodes[3].getblockcount()
        dtorTip = self.nodes[0].getbestblockhash()
        dtorTipHeight = self.nodes[0].getblockcount()

        self.nodes.append(start_node(4, self.options.tmpdir))
        self.nodes[4].set(FORK_CFG + "=0", "consensus.enableCanonicalTxOrder=1")
        for i in range(5):
            connect_nodes_bi(self.nodes,4,i)

        self.nodes.append(start_node(5, self.options.tmpdir))
        # node 5 is non-ctor
        for i in range(5):
            connect_nodes_bi(self.nodes,5,i)

        waitFor(15, lambda: self.nodes[4].getblockcount() >= ctorTipHeight)
        assert_equal(self.nodes[4].getbestblockhash(), ctorTip)
        waitFor(15, lambda: self.nodes[5].getblockcount() >= dtorTipHeight)
        assert_equal(self.nodes[5].getbestblockhash(), dtorTip)

        # Now run the rollback across fork test

        # first generate a competing ctor fork on our isolated node that is longer than the current fork

        # make the new fork longer than current
        for n in range(10):
            time.sleep(.1)
            rollbackNode.generate(1)

        preFork = rollbackNode.generate(1)[0] # will be a dtor block
        preForkBlock = rollbackNode.getblock(preFork)
        rollbackNode.set("consensus.enableCanonicalTxOrder=1")
        # now send tx to myself that will reuse coins.  This creates an block incompatible with dtor
        bal = rollbackNode.getbalance()
        for i in range(1,20):
            rollbackNode.sendtoaddress(rollbackAddr, bal-Decimal(i*.01)) # a little less each time to account for fees
        ctorForkHash = rollbackNode.generate(1)[0]
        rollbackNode.generate(5)
        ctorForkTipHash = rollbackNode.getbestblockhash()
        ctorForkTipCount = rollbackNode.getblockcount()

        # enable Nov 15 fork time
        self.nodes[2].set("%s=%d" % (FORK_CFG, preForkBlock["mediantime"]-1))
        self.nodes[3].set("%s=%d" % (FORK_CFG, preForkBlock["mediantime"]-1))
        # now when 2 and 3 see the other fork, they should switch to it.
        disconnect_all(self.nodes[2])
        connect_nodes(rollbackNode,2)
        connect_nodes(rollbackNode,3)

        waitFor(15, lambda: self.nodes[2].getblockcount() == ctorForkTipCount)
        waitFor(15, lambda: self.nodes[3].getblockcount() == ctorForkTipCount)

        # done, clean up
        stop_nodes([rollbackNode])


if __name__ == '__main__':
    MyTest ().main (bitcoinConfDict={"limitdescendantsize": 50,
                                     FORK_CFG: 0,
                                     "use-thinblocks": 1, 
                                     "use-grapheneblocks": 0})

# Create a convenient function for an interactive python debugging session
def Test():
    t = MyTest()
    t.drop_to_pdb=True
    bitcoinConf = {
        "debug": ["all", "net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "limitdescendantsize": 50, # allow lots of child tx so we can tease apart ctor vs dependent order
        FORK_CFG: 0 # start with automatic fork off
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
