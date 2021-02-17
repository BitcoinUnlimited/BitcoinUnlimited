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

BCH_UNCONF_DEPTH = 50
BCH_UNCONF_SIZE_KB = 101
BCH_UNCONF_SIZE = BCH_UNCONF_SIZE_KB*1000
DELAY_TIME = 120

class MyTest (BitcoinTestFramework):

    def setup_network(self, split=False):
        mempoolConf = [
            ["-blockprioritysize=2000000",
             "-limitancestorsize=%d" % (BCH_UNCONF_SIZE_KB*2),
             "-limitdescendantsize=%d" % (BCH_UNCONF_SIZE_KB*2),
             "-limitancestorcount=%d" % (BCH_UNCONF_DEPTH),
             "-limitdescendantcount=%d" % (BCH_UNCONF_DEPTH),
             "-debug=net", "-debug=mempool"],
            ["-blockprioritysize=2000000",
             "-maxmempool=8080",
             "-limitancestorsize=%d" % (BCH_UNCONF_SIZE_KB*2),
             "-limitdescendantsize=%d" % (BCH_UNCONF_SIZE_KB*2),
             "-limitancestorcount=%d" % (BCH_UNCONF_DEPTH*2),
             "-limitdescendantcount=%d" % (BCH_UNCONF_DEPTH*2),
             "-net.restrictInputs=0"],
            ["-blockprioritysize=2000000", "-limitdescendantcount=1000", "-limitancestorcount=1000",
             "-limitancestorsize=1000", "-limitdescendantsize=1000", "-net.restrictInputs=0"],

            # Launch a peer that simulates a non BU node
            ["-blockprioritysize=2000000",
             "-limitancestorsize=%d" % (BCH_UNCONF_SIZE_KB),
             "-limitdescendantsize=%d" % (BCH_UNCONF_SIZE_KB),
             "-limitancestorcount=%d" % (BCH_UNCONF_DEPTH),
             "-limitdescendantcount=%d" % (BCH_UNCONF_DEPTH),
             "-test.extVersion=0",
             "-net.restrictInputs=0"],
            ]
        self.nodes = start_nodes(4, self.options.tmpdir, mempoolConf)
        connect_nodes_full(self.nodes)
        self.is_network_split=False
        self.sync_blocks()

    def run_test (self):
        #create coins that we can use for creating multi input transactions
        self.relayfee = self.nodes[1].getnetworkinfo()['relayfee']
        utxo_count = BCH_UNCONF_DEPTH * 3 + 1
        startHeight = self.nodes[1].getblockcount()
        logging.info("Starting at %d blocks" % startHeight)
        utxos = create_confirmed_utxos(self.relayfee, self.nodes[1], utxo_count)
        startHeight = self.nodes[1].getblockcount()
        # Sync blocks now, or the node 0 generate below could fork the blockchain, resulting in the a bunch of tx created by create_confirmed_utxo being unconfirmed
        # on node 0 which means they won't be admitted into node 0's mempool breaking this test.
        self.sync_blocks()
        logging.info("Initial sync to %d blocks" % startHeight)

        # kick us out of IBD mode since the cached blocks will be old time so it'll look like our blockchain isn't up to date
        # if we are in IBD mode, we don't request incoming tx.
        self.nodes[0].generate(1)
        self.sync_blocks()
        logging.info("ancestor count test")
        bal = self.nodes[1].getbalance()
        addr = self.nodes[1].getnewaddress()
        #logging.info("Node 0 start:\n %s\n" % getNodeInfo(self.nodes[0]))
        #logging.info("Node 1 start:\n %s\n" % getNodeInfo(self.nodes[1]))

        # create multi input transactions that are chained. This will cause any transactions that are greater
        # than the BCH default chain limit to be prevented from entering the mempool, however they will enter the
        # orphanpool instead.
        txhex = []
        tx_amount = 0
        for i in range(1,BCH_UNCONF_DEPTH*3 + 1):
          try:
              inputs = []
              inputs.append(utxos.pop())
              if (i == 1):
                inputs.append(utxos.pop())
              else:
                inputs.append({ "txid" : txid, "vout" : 0})

              outputs = {}
              if (i == 1):
                tx_amount = inputs[0]["amount"] + inputs[1]["amount"] - self.relayfee
              else:
                tx_amount = inputs[0]["amount"] + tx_amount - self.relayfee
              outputs[self.nodes[1].getnewaddress()] = tx_amount
              rawtx = self.nodes[1].createrawtransaction(inputs, outputs)
              signed_tx = self.nodes[1].signrawtransaction(rawtx)["hex"]
              txid = self.nodes[1].sendrawtransaction(signed_tx)
              self.nodes[0].enqueuerawtransaction(signed_tx)  # Manually jam for speed

              txhex.append(txid)  # enough so that it uses the one 
              logging.info("tx depth %d" % i) # Keep travis from timing out
          except JSONRPCException as e: # an exception you don't catch is a testing error
              print(str(e))
              raise
          if i > BCH_UNCONF_DEPTH-5 and i <= BCH_UNCONF_DEPTH+5:  # Bracket the unconf chain acceptance depth of this node for efficiency
              # Test that every tx beyond the unconf limit is inserted into the orphan pool -- nothing is dropped
              # but it won't be relayed to me from the other node so I need to manually inject
              waitFor(DELAY_TIME, lambda: [print("Node 0: mempool size: %d, orphan pool size: %d" % (self.nodes[0].getmempoolinfo()["size"], self.nodes[0].getorphanpoolinfo()["size"])),self.nodes[0].getmempoolinfo()["size"] + self.nodes[0].getorphanpoolinfo()["size"] >= i][-1], lambda: "Node 0 Info:\n" + getNodeInfo(self.nodes[0]))
        waitFor(DELAY_TIME, lambda: [print("Node 0 connected to: %d mempool (should be %d): %s" % (len(self.nodes[0].getpeerinfo()), BCH_UNCONF_DEPTH, str(self.nodes[0].getmempoolinfo()))), self.nodes[0].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH][-1], lambda: "Node 0 Info:\n" + getNodeInfo(self.nodes[0]))
        waitFor(DELAY_TIME, lambda: self.nodes[1].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH*3)
        waitFor(DELAY_TIME, lambda: self.nodes[3].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH)
        # Set small to commit just a few tx so we can see if the missing ones get pushed
        # self.nodes[0].set("mining.blockSize=6000")

        # disconnect to prove that the orphan queue gets pushed into the mempool when block generated
        disconnect_all(self.nodes[0])
        opSz = self.nodes[0].getorphanpoolinfo()["size"]
        blk = self.nodes[0].generate(1)[0]

        # check that all orphans got pushed into the mempool once the block was mined
        consumed = min(BCH_UNCONF_DEPTH, opSz)
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == min(BCH_UNCONF_DEPTH, opSz), lambda: "Node 0:\n" + getNodeInfo(self.nodes[0]))
        waitFor(DELAY_TIME, lambda: self.nodes[0].getorphanpoolinfo()["size"] == opSz-consumed)

        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[0], 2)
        connect_nodes(self.nodes[0], 3)

        blkhex = self.nodes[0].getblock(blk)
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH)
        # generate the block somewhere else and see if the tx get pushed
        waitFor(DELAY_TIME, lambda: self.nodes[2].getbestblockhash() == blk)  # we don't want a fork
        self.nodes[2].set("mining.blockSize=6000")
        blk2 = self.nodes[2].generate(1)[0]
        self.sync_blocks()

        # after the block propagates, nodes will push 50 tx to these nodes.
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH)
        waitFor(DELAY_TIME, lambda: self.nodes[3].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH)

        waitFor(DELAY_TIME, lambda: self.nodes[1].getbestblockhash() == blk2)  # make sure its settled so we can get a good leftover count for the next test.
        unconfLeftOver = self.nodes[1].getmempoolinfo()["size"]
        assert(unconfLeftOver >= BCH_UNCONF_DEPTH)  # if someone bumps the BCH network unconfirmed depth, you need to build a bigger unconf chain
        # Let's consume all BCH_UNCONF_DEPTH tx
        self.nodes[0].set("mining.blockSize=8000000")

        waitFor(DELAY_TIME, lambda: len(self.nodes[0].getblocktemplate()["transactions"])>=BCH_UNCONF_DEPTH)
        waitFor(DELAY_TIME, lambda: self.nodes[0].getbestblockhash() == blk2)
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

        # Grab existing addresses on all the nodes to create destinations for sendmany
        # Grabbing existing addrs is a lot faster than creating new ones
        addrlist = []
        for node in self.nodes:
            tmpaddrs = node.listaddressgroupings()
            for axx in tmpaddrs:
                addrlist.append(axx[0][0])

        amounts = {}
        for a in addrlist:
            amounts[a] = "0.00001"

        bal = self.nodes[1].getbalance()
        amounts[addr] = bal - Decimal("5.0")

        # Wait for sync before issuing the tx chain so that no txes are rejected as nonfinal
        self.sync_blocks()
        logging.info("Block heights: %s" % str([x.getblockcount() for x in self.nodes]))

        # Create an unconfirmed chain that exceeds what a non BU node would allow (node3)
        cumulativeTxSize = 0
        while cumulativeTxSize < BCH_UNCONF_SIZE:
            txhash = self.nodes[1].sendmany("",amounts,0)
            tx = self.nodes[1].getrawtransaction(txhash)
            txinfo = self.nodes[1].gettransaction(txhash)
            logging.info("fee: %s fee sat/byte: %s" % (str(txinfo["fee"]), str(txinfo["fee"]*100000000/Decimal(len(tx)/2)) ))
            cumulativeTxSize += len(tx)/2  # /2 because tx is a hex representation of the tx
            logging.info("total size: %d" % cumulativeTxSize)
            logging.info("tx: %s" % txhash)

        txCommitted = self.nodes[1].getmempoolinfo()["size"]
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == txCommitted)  # nodes[0] will eliminate 1 tx because ancestor size too big
        waitFor(DELAY_TIME, lambda: self.nodes[3].getmempoolinfo()["size"] == txCommitted-1)  # nodes[3] will eliminate 1 tx because ancestor size too big
        waitFor(DELAY_TIME, lambda: self.nodes[2].getmempoolinfo()["size"] == txCommitted)  # nodes[2] should have gotten everything because its ancestor size conf is large
        first_txCommitted = txCommitted

        #send more txns to violate the BU nodes settings (node0)
        while cumulativeTxSize < BCH_UNCONF_SIZE*2:
            txhash = self.nodes[1].sendmany("",amounts,0)
            tx = self.nodes[1].getrawtransaction(txhash)
            txinfo = self.nodes[1].gettransaction(txhash)
            logging.info("fee: %s fee sat/byte: %s" % (str(txinfo["fee"]), str(txinfo["fee"]*100000000/Decimal(len(tx)/2)) ))
            cumulativeTxSize += len(tx)/2  # /2 because tx is a hex representation of the tx
            logging.info("total size: %d" % cumulativeTxSize)
            logging.info("tx: %s" % txhash)
        txCommitted = self.nodes[1].getmempoolinfo()["size"]
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == txCommitted-1)  # nodes[0] will eliminate 1 tx because ancestor size too big
        waitFor(DELAY_TIME, lambda: self.nodes[2].getmempoolinfo()["size"] == txCommitted)  # nodes[2] should have gotten 


        self.nodes[0].generate(1)
        waitFor(DELAY_TIME, lambda: self.nodes[3].getmempoolinfo()["size"] == first_txCommitted - 1)  # node 1 should push the tx that's now acceptable to node 3
        waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == 1)  # node 1 should push the tx that's now acceptable to node 0
        self.nodes[0].generate(1)  # clean up

        self.sync_blocks() # Wait for sync before issuing the tx chain so that no txes are rejected as nonfinal
        logging.info("Block heights: %s" % str([x.getblockcount() for x in self.nodes]))

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
        for i in range(0,51):
          try:
            txhex.append(self.nodes[2].sendtoaddress(addr, bal-1))  # enough so that it uses all UTXO, but has fee left over
            logging.info("%d: sizes %d, %d, %d" % (i,self.nodes[0].getmempoolinfo()["size"],self.nodes[1].getmempoolinfo()["size"],self.nodes[2].getmempoolinfo()["size"]))
          except JSONRPCException as e: # an exception you don't catch is a testing error
              print(str(e))
              raise

        count = 0
        while self.nodes[2].getmempoolinfo()["size"] != 0:
            logging.info("%d: sizes %d, %d, %d" % (count,self.nodes[0].getmempoolinfo()["size"],self.nodes[1].getmempoolinfo()["size"],self.nodes[2].getmempoolinfo()["size"]))
            # these checks aren't going to work at the end when I run out of tx so check for that
            if self.nodes[2].getmempoolinfo()["size"] >= BCH_UNCONF_DEPTH*2:
                waitFor(DELAY_TIME, lambda: self.nodes[0].getmempoolinfo()["size"] == BCH_UNCONF_DEPTH)
                waitFor(DELAY_TIME, lambda: True if self.nodes[1].getmempoolinfo()["size"] >= BCH_UNCONF_DEPTH*2 else print("%d: sizes %d, %d, %d" % (count,self.nodes[0].getmempoolinfo()["size"],self.nodes[1].getmempoolinfo()["size"],self.nodes[2].getmempoolinfo()["size"])) )

            blk = self.nodes[0].generate(1)[0]
            waitFor(DELAY_TIME, lambda: self.nodes[2].getbestblockhash() == blk)
            waitFor(DELAY_TIME, lambda: self.nodes[1].getbestblockhash() == blk)
            count+=1

if __name__ == '__main__':
    t = MyTest()
    t.main (None, { "blockprioritysize": 2000000, "keypool":5 })

# Create a convenient function for an interactive python debugging session
def Test():
    t = MyTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": [ "net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }
    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
