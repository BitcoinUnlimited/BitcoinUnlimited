#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit

# Wallet performance benchmarks

import time
import sys
import random
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.nodemessages import *
from test_framework.util import *

SATPERBCH = Decimal(100000000)

class MyTest (BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        # pick this one to start from the cached 4 node 100 blocks mined configuration
        initialize_chain(self.options.tmpdir, bitcoinConfDict, wallets)
        # pick this one to start at 0 mined blocks
        # initialize_chain_clean(self.options.tmpdir, 2, bitcoinConfDict, wallets)
        # Number of nodes to initialize ----------> ^

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        # Nodes to start --------^
        # Note for this template I readied 4 nodes but only started 2

        # Now interconnect the nodes
        connect_nodes_full(self.nodes)
        # Let the framework know if the network is fully connected.
        # If not, the framework assumes this partition: (0,1) and (2,3)
        # For more complex partitions, you can't use the self.sync* member functions
        self.is_network_split=False
        self.sync_blocks()

    def run_test (self):
        # generate enough blocks so that nodes[0] has a balance
        self.sync_blocks()
        self.nodes[1].generate(50)
        self.sync_blocks()

        start = time.time()
        NUM_ADDRS = 500
        addrs1=[]
        for i in range(0,NUM_ADDRS):
            addrs1.append(self.nodes[1].getnewaddress())
        getAddr1Done = time.time()
        print("sequential getnewaddress/sec: %4.2f" % (float(NUM_ADDRS)/float(getAddr1Done-start)))

        start = time.time()
        addrs0=[]
        for i in range(0,NUM_ADDRS):
            addrs0.append(self.nodes[0].getnewaddress())
        getAddr0Done = time.time()
        print("sequential getnewaddress/sec: %4.2f" % (float(NUM_ADDRS)/float(getAddr0Done-start)))

        # Doesn't matter: self.nodes[0].set("wallet.coinSelSearchTime=1")

        # Split so we can spend
        logging.info("split")
        for i in range(0,NUM_ADDRS-20,2):
          try:
            dests = {}
            for j in range(i,i+20):
                dests[addrs0[j]] = "0.01"
            txid = self.nodes[1].sendmany('', dests, 0, "", [])
          except JSONRPCException as e:  # Expecting to run out of UTXOs
            self.nodes[1].generate(1)

        self.nodes[1].generate(1)
        time.sleep(1)

        logging.info("Begin small wallet sendtoaddress test")
        unspentSize = len(self.nodes[0].listunspent())
        start = time.time()
        for i in range(0,NUM_ADDRS):
            self.nodes[0].sendtoaddress(addrs1[i], round(0.001 + (i*0.0001),8))
        sendtoaddressDone = time.time()

        print("sequential sendtoaddress/sec at %d utxos: %4.2f" % (unspentSize, float(NUM_ADDRS)/float(sendtoaddressDone - start)))

        if 1:
          logging.info("split into large wallet")
        # Split so we can spend
          for i in range(0,int(NUM_ADDRS/2-30)):
            try:
              dests = {}
              for j in range(i,i+30):
                dests[addrs0[j]] = "0.01"
              txid = self.nodes[1].sendmany('', dests, 0, "", [])
            except JSONRPCException as e:  # Expecting to run out of UTXOs
              print(i, ": Generate block, utxos: ", len(self.nodes[0].listunspent()))
              self.nodes[1].generate(1)

        self.nodes[1].generate(1)
        time.sleep(1)

        LOOP = 200 # int(NUM_ADDRS/4)
        logging.info("Begin sequential sendtoaddress test")
        unspentSize = len(self.nodes[0].listunspent())
        start = time.time()
        for i in range(0,LOOP):
            self.nodes[0].sendtoaddress(addrs1[i], round(0.001 + (i*0.0001),8))
        sendtoaddressDone = time.time()
        print("sequential sendtoaddress/sec at %d utxos: %4.2f" % (unspentSize, float(LOOP)/float(sendtoaddressDone - start)))

        self.nodes[0].generate(1)
        time.sleep(0.25)

        # Gather statistics on the transactions
        fees = Decimal("0")
        nInputs = 0
        nOutputs = 0
        txsize = 0
        print("sendtoaddress tx analysis")
        random.seed(1)
        for i in range(0,LOOP):
            # txid = self.nodes[0].sendtoaddress(addrs1[i], round(0.01 + (i*0.001),8))
            txid = self.nodes[0].sendtoaddress(addrs1[i], round(random.random()/10.0,8))
            try:
                txhex = self.nodes[0].getrawtransaction(txid)
            except JSONRPCException as e:
                time.sleep(0.25)
                txhex = self.nodes[0].getrawtransaction(txid)
            txinfo = self.nodes[0].gettransaction(txid)
            txinfo2 = self.nodes[0].decoderawtransaction(txhex)
            tx = CTransaction().deserialize(txhex)
            txsize += len(txhex)/2
            fees += txinfo["fee"]
            nInputs += len(tx.vin)
            nOutputs += len(tx.vout)

        print("%d tx, size %d.  Total Fees: %s. Fees SAT/byte: %s  Inputs spent: %d.  Outputs sent: %d." % (LOOP, txsize, str(fees), str(fees*SATPERBCH/Decimal(txsize)), nInputs, nOutputs))
        
        self.nodes[0].generate(1)
        time.sleep(0.25)

        self.nodes[0].set("wallet.coinSelSearchTime=1")
        unspentSize = len(self.nodes[0].listunspent())
        logging.info("Begin sendfrom test")
        start = time.time()
        for i in range(0,LOOP):
            self.nodes[0].sendfrom("",addrs1[i], round(0.001 + (i*0.0001),8))
        sendfromDone = time.time()
        print("sequential sendfrom/sec at %d utxos: %4.2f" % (unspentSize, float(LOOP)/float(sendfromDone - start)))

        self.nodes[0].generate(1)
        time.sleep(0.25)

        unspentSize = len(self.nodes[0].listunspent())
        logging.info("Begin sequential sendmany test with 1ms search time")
        start = time.time()
        for i in range(0,LOOP-1):
            self.nodes[0].sendmany("", { addrs0[i]: str(round(0.001 + (i*0.0001),8)), addrs0[i+1]: str(round(0.001 + (i*0.0001),8)) }, 0, "", [])
        sendtoaddressDone = time.time()

        print("sequential sendmany/sec at %d utxos: %4.2f" % (unspentSize, float(LOOP-1)/float(sendtoaddressDone - start)))


if __name__ == '__main__':
    MyTest ().main ()

# Create a convenient function for an interactive python debugging session
def Test():
    t = MyTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["selectcoins"], # ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000,  # we don't want any transactions rejected due to insufficient fees...
        "logtimemicros":1,
        "checkmempool":0,
        # "par":1  # Reduce the # of threads in bitcoind for easier debugging
    }


    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
