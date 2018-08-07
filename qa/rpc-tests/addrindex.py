#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class AddrIndexTest (BitcoinTestFramework):
    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        connect_nodes_bi(self.nodes,0,1)
        self.is_network_split=False
        self.sync_all()

    def run_test (self):
        # generate enough blocks so that nodes[0] has a balance
        self.sync_blocks()
        self.nodes[0].generate(101)
        self.sync_blocks()

        assert_equal(self.nodes[0].getbalance(), 50)

        n0, n1 = self.nodes

        addr = n1.getnewaddress()

        # address not yet used -> not in index
        assert n0.searchrawtransactions(addr) == []
        assert n1.searchrawtransactions(addr) == []

        txid = n0.sendtoaddress(addr, 0.1)
        # not yet mined -> not in index
        assert n0.searchrawtransactions(addr) == []
        assert n1.searchrawtransactions(addr) == []

        # mine it
        self.nodes[0].generate(1)
        self.sync_blocks()

        sresult0 = n0.searchrawtransactions(addr)
        sresult1 = n1.searchrawtransactions(addr)

        assert len(sresult0) == 1
        assert sresult0[0]["txid"] == txid
        assert len(sresult1) == 1
        assert sresult1[0]["txid"] == txid

        for i in range(10):
            n0.sendtoaddress(addr, 0.01)

        self.nodes[0].generate(1)
        self.sync_blocks()

        sresult0 = n0.searchrawtransactions(addr)
        sresult1 = n1.searchrawtransactions(addr)

        assert len(sresult0) == 11
        assert sresult0[0]["txid"] == txid

        assert len(sresult1) == 11
        assert sresult1[0]["txid"] == txid

        sresult0 = n0.searchrawtransactions(addr, False)
        assert "txid" not in sresult0[0]

        sresult0 = n0.searchrawtransactions(addr, True, 5) # skip=5
        assert len(sresult0) == 6

        sresult0 = n0.searchrawtransactions(addr, True, -3) # skip=-3
        assert len(sresult0) == 3

        sresult0 = n0.searchrawtransactions(addr, True, 5, 4) # skip=5, count=4
        assert len(sresult0) == 4




if __name__ == '__main__':
    t = AddrIndexTest()
    flags = []
    bitcoinConf = { "addrindex": 1 }
    t.main(flags, bitcoinConf, None)
