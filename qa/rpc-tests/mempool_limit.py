#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
# Test mempool limiting together/eviction with the wallet

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class MempoolLimitTest(BitcoinTestFramework):

    def __init__(self):
        self.txouts = gen_return_txouts()

    def setup_network(self):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir,
        ["-maxmempool=5",
         "-spendzeroconfchange=0",
         "-minlimitertxfee=2",
         "-limitdescendantcount=25",
         "-limitancestorcount=25",
         "-limitancestorsize=101",
         "-limitdescendantsize=101",
         "-debug"]))
        self.is_network_split = False
        self.sync_all()
        self.relayfee = self.nodes[0].getnetworkinfo()['relayfee']

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2)

    def run_test(self):
        txids = []
        utxos = create_confirmed_utxos(self.relayfee, self.nodes[0], 91)

        # create a lot of txns up to but not exceeding the maxmempool
        relayfee = self.nodes[0].getnetworkinfo()['relayfee']
        base_fee = relayfee*100
        for i in range (2):
            txids.append([])
            txids[i] = create_lots_of_big_transactions(self.nodes[0], self.txouts, utxos[33*i:33*i+33], 33, (i+1)*base_fee)
            print(str(self.nodes[0].getmempoolinfo()))

        num_txns_in_mempool = self.nodes[0].getmempoolinfo()["size"]

        # create another txn that will exceed the maxmempool which should evict some random transaction.
        all_txns = self.nodes[0].getrawmempool()

        i = 2
        new_txn = create_lots_of_big_transactions(self.nodes[0], self.txouts, utxos[33*i:33*i+33], 1, (i+1)*base_fee)
        print("newtxns " + str(new_txn[0]))
        assert(self.nodes[0].getmempoolinfo()["usage"] < self.nodes[0].getmempoolinfo()["maxmempool"])

        # make sure the mempool count did not change
        assert(num_txns_in_mempool == self.nodes[0].getmempoolinfo()["size"])

        # make sure new tx is in the mempool
        assert(new_txn[0] in self.nodes[0].getrawmempool())

if __name__ == '__main__':
    MempoolLimitTest().main()

def Test():
    t = MempoolLimitTest()
    # t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["blk", "mempool", "net", "req"],
        "logtimemicros": 1
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
