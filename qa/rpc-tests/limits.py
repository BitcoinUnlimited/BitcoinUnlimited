#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
# This is a template to make creating new QA tests easy.
# You can also use this template to quickly start and connect a few regtest nodes.

import os
import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.blocktools import *


class MyTest (BitcoinTestFramework):

    def setup_chain(self, bitcoinConfDict=None, wallets=None):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)
        # Number of nodes to initialize ----------> ^

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir, timewait=60 * 10)
        # Nodes to start --------^
        # Note for this template I readied 4 nodes but only started 2

        # Now interconnect the nodes
        connect_nodes(self.nodes[0], 1)
        # Let the framework know if the network is fully connected.
        # If not, the framework assumes this partition: (0,1) and (2,3)
        # For more complex partitions, you can't use the self.sync* member functions
        self.is_network_split = False
        self.sync_all()

    def generateTx(self, node, txBytes, addrs, data=None):
        wallet = node.listunspent()
        wallet.sort(key=lambda x: x["amount"], reverse=False)
        logging.info("Wallet length is %d" % len(wallet))

        size = 0
        count = 0
        decContext = decimal.getcontext().prec
        decimal.getcontext().prec = 8 + 8  # 8 digits to get to 21million, and each bitcoin is 100 million satoshis
        while size < txBytes:
            count += 1
            utxo = wallet.pop()
            outp = {}
            # Make the tx bigger by adding addtl outputs so it validates faster
            payamt = satoshi_round(utxo["amount"] / decimal.Decimal(8.0))
            for x in range(0, 8):
                # its test code, I don't care if rounding error is folded into the fee
                outp[addrs[(count + x) % len(addrs)]] = payamt
            if data:
                outp["data"] = data
            txn = createrawtransaction([utxo], outp, createWastefulOutput)
            # The python createrawtransaction is meant to have the same API as the node's RPC so you can also do:
            # txn2 = node.createrawtransaction([utxo], outp)
            signedtxn = node.signrawtransaction(txn)
            size += len(binascii.unhexlify(signedtxn["hex"]))
            node.sendrawtransaction(signedtxn["hex"])
        logging.info("%d tx %d length" % (count, size))
        decimal.getcontext().prec = decContext
        return (count, size)

    def makeUtxos(self, node, addrs, NUM_BLOCKS):

        outp = {}
        for a in addrs:
            outp[a] = 0.1

        for x in range(0, NUM_BLOCKS):
            tx = node.sendmany("", outp)

        node.generate(1)
        self.sync_blocks()

    def run_test(self):

        TEST_BLOCK_SIZE = 128000000
        [x.set("net.excessiveBlock=%d" % (TEST_BLOCK_SIZE + 20000)) for x in self.nodes]
        [x.set("mining.blockSize=%d" % TEST_BLOCK_SIZE) for x in self.nodes]
        [x.set("mining.dataCarrierSize=64000") for x in self.nodes]  # allow lots of data
        self.testBigSubmitBlock(TEST_BLOCK_SIZE, 100, 30)

    def testBigSubmitBlock(self, TEST_BLOCK_SIZE, NUM_ADDRS, NUM_BLOCKS):
        # generate enough blocks so that nodes[0] has a balance
        self.sync_blocks()
        self.nodes[0].generate(100 + NUM_BLOCKS)
        self.sync_blocks()
        logging.info("generated initial blocks")

        self.nodes[0].keypoolrefill(NUM_ADDRS)
        addrs = [self.nodes[0].getnewaddress() for _ in range(NUM_ADDRS)]
        logging.info("generated addresses")

        self.makeUtxos(self.nodes[0], addrs, NUM_BLOCKS)
        logging.info("split utxos")

        peers = self.nodes[0].getpeerinfo()
        for p in peers:
            self.nodes[0].disconnectnode(p["addr"])

        legacyAddrs = [self.nodes[0].getaddressforms(x)["legacy"] for x in addrs]
        self.generateTx(self.nodes[0], TEST_BLOCK_SIZE, legacyAddrs, "01" * 64000)
        logging.info("generated enough tx")

        bigBlockHash = self.nodes[0].generate(1)[0]
        logging.info("generated block %s" % str(bigBlockHash))
        bigBlockHex = self.nodes[0].getblock(bigBlockHash, False)
        logging.info("got block hex, length %d" % len(bigBlockHex))
        n1blockcount = self.nodes[1].getblockcount()
        ret = self.nodes[1].submitblock(bigBlockHex)
        assert ret != "duplicate"
        assert_equal(n1blockcount + 1, self.nodes[1].getblockcount())
        assert_equal(self.nodes[0].getblockcount(), self.nodes[1].getblockcount())
        return True


if __name__ == '__main__':
    MyTest().main()

# Create a convenient function for an interactive python debugging session


def Test():
    t = MyTest()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }

    flags = []
    # you may want these additional flags:
    # "--srcdir=<out-of-source-build-dir>/debug/src"
    # "--nocleanup", "--noshutdown"
    if os.path.isdir("/ramdisk/test"):  # execution is much faster if a ramdisk is used
        flags.append("--tmppfx=/ramdisk/test")
    t.main(flags, bitcoinConf, None)
