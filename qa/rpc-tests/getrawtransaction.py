#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Unlimited developers
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
import binascii
import json

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class GetRawTransactionTest (BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        connect_nodes_full(self.nodes)
        self.is_network_split=False
        self.sync_blocks()

    def run_test (self):

        # generate enough blocks so that nodes[0] has a balance
        self.sync_blocks()
        self.nodes[0].generate(105)
        self.sync_blocks()

        startinghash = self.nodes[0].generate(1)

        blockTxids = {}
        blocks = 0
        while blocks < 7:
            txids = []
            if blocks == 0:
                self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.001)
            oneTxHash = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
            txids.append(oneTxHash)
            timeIssued = time.time()
            txids.append(self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1))
            txids.append(self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1))

            # Test some fields of rawtransaction get getrawtransaction while the tx is unconfirmed
            grt = self.nodes[0].getrawtransaction(oneTxHash, 1);
            assert_equal(grt["confirmations"], 0)  # An unconfirmed tx should have 0 confirms
            assert grt["time"] > 0
            assert grt["time"] <= timeIssued  # Make sure the reported time is near the time issued, not now.
            gt = self.nodes[0].gettransaction(oneTxHash)
            assert_equal(gt["confirmations"], 0)  # An unconfirmed tx should have 0 confirms

            blockhash = self.nodes[0].generate(1)[0]
            blockTxids[blockhash] = txids
            blocks = blocks + 1

        self.sync_blocks()

        # intentionally ask for more blocks than can be returned for testing
        rawtransactionssince = self.nodes[1].getrawtransactionssince(startinghash[0], 10)
        rawtransactionssince2 = self.nodes[1].getrawtransactionssince(startinghash[0], "10")
        assert_equal(rawtransactionssince, rawtransactionssince2)
        for hash ,txs in blockTxids.items():
            assert_not_equal(rawtransactionssince.get(hash, False), False)
            for txid in txs:
                assert_not_equal(rawtransactionssince[hash].get(txid, False), False)
        assert_equal(rawtransactionssince.get("notarealhash", False), False)

        for hash ,txs in blockTxids.items():
            assert_equal(self.nodes[0].getrawblocktransactions(hash), rawtransactionssince[hash])

        unspents = self.nodes[1].listunspent()
        index = 0
        while index < len(unspents):
            if unspents[index]['satoshi'] == 100000:
                break
            index = index + 1
        data = "TestData............................7894561230"
        tx = self.nodes[1].createrawtransaction([{"txid": unspents[index]['txid'], "vout": unspents[index]['vout']}], {"data": data.encode().hex()})
        signedtx = self.nodes[1].signrawtransaction(tx)['hex']
        self.nodes[1].sendrawtransaction(signedtx)
        opblockhash = self.nodes[1].generate(1)[0]
        self.sync_blocks()
        k = 0
        while k < 5:
            self.nodes[0].generate(1)
            k = k + 1
        self.sync_blocks()
        #print(self.nodes[0].getrawblocktransactions("-v", opblockhash))
        # we should get at least 1 entry when checking for any data because of our op return tx.
        assert_not_equal(self.nodes[0].getrawblocktransactions(opblockhash , "*"), {})
        assert_not_equal(self.nodes[0].getrawtransactionssince(opblockhash , 1, "*"), {})
        assert_equal(self.nodes[0].getrawblocktransactions(opblockhash , "*"), self.nodes[0].getrawtransactionssince(opblockhash , 1, "*")[opblockhash])




if __name__ == '__main__':
    GetRawTransactionTest ().main ()

# Create a convenient function for an interactive python debugging session
def Test():
    t = GetRawTransactionTest()
    t.drop_to_pdb=True
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }


    flags = standardFlags()
    t.main(flags, bitcoinConf, None)

