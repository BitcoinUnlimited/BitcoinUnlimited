#!/usr/bin/env python3
# Copyright (c) 2015-2019 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import test_framework.loginit
# This is a template to make creating new QA tests easy.
# You can also use this template to quickly start and connect a few regtest nodes.

import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

BCH_UNCONF_DEPTH = 25
BCH_UNCONF_SIZE_KB = 101
BCH_UNCONF_SIZE = BCH_UNCONF_SIZE_KB*1000
DELAY_TIME = 45

class MyTest (BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        # pick this one to start from the cached 4 node 100 blocks mined configuration

        initialize_chain(self.options.tmpdir, bitcoinConfDict, wallets)
        # pick this one to start at 0 mined blocks
        # initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)
        # Number of nodes to initialize ----------> ^

    def setup_network(self, split=False):
        mempoolConf = [
            ["-blockprioritysize=2000000"],
            ["-blockprioritysize=2000000",
             "-maxmempool=8080",
             "-limitancestorsize=%d" % (BCH_UNCONF_SIZE_KB*2),
             "-limitdescendantsize=%d" % (BCH_UNCONF_SIZE_KB*2),
             "-limitancestorcount=%d" % (BCH_UNCONF_DEPTH*2),
             "-limitdescendantcount=%d" % (BCH_UNCONF_DEPTH*2),
             "-net.unconfChainResendAction=2",
             "-net.restrictInputs=0"],
            ["-blockprioritysize=2000000", "-limitdescendantcount=1000", "-limitancestorcount=1000",
             "-limitancestorsize=1000", "-limitdescendantsize=1000", "-net.unconfChainResendAction=2",
             "-net.restrictInputs=0"],
            ["-blockprioritysize=2000000", "-limitancestorsize=150","-net.unconfChainResendAction=2"]
            ]
        self.nodes = start_nodes(4, self.options.tmpdir, mempoolConf)
        connect_nodes_full(self.nodes)
        self.is_network_split=False
        self.sync_blocks()

    def run_test (self):
        # kick us out of IBD mode since the cached blocks will be old time so it'll look like our blockchain isn't up to date
        # if we are in IBD mode, we don't request incoming tx.
        self.nodes[0].generate(1)

        logging.info("ancestor count test")
        bal = self.nodes[1].getbalance()
        addr = self.nodes[1].getnewaddress()

        txhex = []
        for i in range(0,BCH_UNCONF_DEPTH*2):
          try:
              txhex.append(self.nodes[1].sendtoaddress(addr, bal-1))  # enough so that it uses all UTXO, but has fee left over
          except JSONRPCException as e: # an exception you don't catch is a testing error
              print(str(e))
              raise
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH)
        waitFor(DELAY_TIME, lambda: self.nodes[1].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH*2)
        # Set small to commit just a few tx so we can see if the missing ones get pushed
        self.nodes[0].set("mining.blockSize=6000")
        blk = self.nodes[0].generate(1)[0]
        blkhex = self.nodes[0].getblock(blk)
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH)
        # generate the block somewhere else and see if the tx get pushed
        self.nodes[2].set("mining.blockSize=4000")
        blk2 = self.nodes[2].generate(1)[0]
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH)

        waitFor(DELAY_TIME, lambda: self.nodes[1].getbestblockhash() == blk2)  # make sure its settled so we can get a good leftover count for the next test.
        unconfLeftOver = self.nodes[1].getmempoolinfo()["size"]
        assert(unconfLeftOver >= BCH_UNCONF_DEPTH)  # if someone bumps the BCH network unconfirmed depth, you need to build a bigger unconf chain
        # Let's consume all BCH_UNCONF_DEPTH tx
        self.nodes[0].set("mining.blockSize=8000000")

        waitFor(DELAY_TIME, lambda: len(self.nodes[0].getblocktemplate()["transactions"])>=BCH_UNCONF_DEPTH)
        blk3 = self.nodes[0].generate(1)[0]
        blk3data = self.nodes[0].getblock(blk3)
        # this would be ideal, but a particular block is not guaranteed to contain all tx in the mempool 
        # assert_equal(len(blk3data["tx"]), BCH_UNCONF_DEPTH + 1)  # chain of BCH_UNCONF_DEPTH unconfirmed + coinbase
        committedTxCount = len(blk3data["tx"])-1  # -1 to remove coinbase
        waitFor(DELAY_TIME, lambda: self.nodes[1].getbestblockhash() == blk3)
        waitFor(DELAY_TIME, lambda: self.nodes[1].getmempoolinfo()["size"] == unconfLeftOver - committedTxCount)
        # make sure that everything that can be pushed is pushed
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == min(unconfLeftOver - committedTxCount, BCH_UNCONF_DEPTH))

        # clean up: confirm all the left over tx from the prior test
        self.nodes[1].generate(1)

        logging.info("ancestor size test")
        # Now test ancestor size
        addrlist = [self.nodes[3].getnewaddress() for i in range(0,200)]
        amounts = {}
        for a in addrlist:
            amounts[a] = "0.00001"

        bal = self.nodes[1].getbalance()
        amounts[addr] = bal - Decimal("5.0")

        # Create an unconfirmed chain that exceeds what node 0 allows
        cumulativeTxSize = 0
        while cumulativeTxSize < BCH_UNCONF_SIZE:
            txhash = self.nodes[1].sendmany("",amounts,0)
            tx = self.nodes[1].getrawtransaction(txhash)
            cumulativeTxSize += len(tx)/2  # /2 because tx is a hex representation of the tx

        txCommitted = self.nodes[1].getmempoolinfo()["size"]
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == txCommitted-1)  # nodes[0] will eliminate 1 tx because ancestor size too big
        waitFor(DELAY_TIME, lambda: self.nodes[2].getmempoolinfo()["size"] == txCommitted)  # nodes[2] should have gotten everything because its ancestor size conf is large
        self.nodes[0].generate(1)
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == 1)  # node 1 should push the tx that's now acceptable to node 0
        self.nodes[0].generate(1)  # clean up

        # Now let's run a more realistic test with 2 mining nodes of varying mempool depth, and one application node with a huge depth
        logging.info("deep unconfirmed chain test")

        # Because the TX push races the block, connect the network in a special way to avoid this race.
        # This is undesirable for a test, but in the real network will likely result in a faster dispersal of the TX because the miners are interconnected
        for n in self.nodes:
            disconnect_all(n)
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[1], 2)

        bal = self.nodes[2].getbalance()
        addr = self.nodes[2].getnewaddress()

        txhex = []
        for i in range(0,100):
          try:
            txhex.append(self.nodes[2].sendtoaddress(addr, bal-1))  # enough so that it uses all UTXO, but has fee left over
          except JSONRPCException as e: # an exception you don't catch is a testing error
              print(str(e))
              raise

        count = 0
        while self.nodes[2].getmempoolinfo()["size"] != 0:
            # these checks aren't going to work at the end when I run out of tx so check for that
            if self.nodes[2].getmempoolinfo()["size"] >= BCH_UNCONF_DEPTH*2:
                waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH)
                waitFor(DELAY_TIME, lambda: self.nodes[1].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH*2)
            logging.info("%d: sizes %d, %d, %d" % (count,self.nodes[0].getmempoolinfo()["size"],self.nodes[1].getmempoolinfo()["size"],self.nodes[2].getmempoolinfo()["size"]))
            blk = self.nodes[0].generate(1)[0]
            waitFor(DELAY_TIME, lambda: self.nodes[2].getbestblockhash() == blk)
            count+=1

if __name__ == '__main__':
    MyTest ().main ()

# Create a convenient function for an interactive python debugging session
def Test():
    t = MyTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }
    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
