#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Unlimited developers
"""
Tests to check if basic electrum server integration works
"""
from test_framework.util import assert_equal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging
from test_framework.nodemessages import CBlock, FromHex


class ElectrumBasicTests(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        n = self.nodes[0]
        n.set("consensus.enableCanonicalTxOrder=1")
        n.generate(200)

        for i in range(1, 100):
            n.sendtoaddress(n.getnewaddress(), 1)

        block = n.generate(1)
        block = FromHex(CBlock(), n.getblock(block[0], False))

        for i in range(0, len(block.vtx)):
            res = n.gettxpos(block.vtx[i].calc_sha256(),
                    block.calc_sha256())
            assert_equal(i, res['position'])


    def setup_network(self, dummy = None):
        self.nodes = self.setup_nodes()

if __name__ == '__main__':
    ElectrumBasicTests().main()
