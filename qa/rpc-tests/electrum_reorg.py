#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Unlimited developers
"""
Tests to check if basic electrum server integration works
"""
import random
from test_framework.util import waitFor, assert_equal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging
from test_framework.electrumutil import compare, bitcoind_electrum_args

class ElectrumReorgTests(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [bitcoind_electrum_args()]

    def run_test(self):
        n = self.nodes[0]

        n.generate(200)

        # waitFor throws on timeout, failing the test

        waitFor(10, lambda: compare(n, "index_height", n.getblockcount()))
        waitFor(10, lambda: compare(n, "mempool_count", 0, True))
        n.sendtoaddress(n.getnewaddress(), 1)
        assert_equal(1, len(n.getrawmempool()))
        waitFor(10, lambda: compare(n, "mempool_count", 1, True))

        blocks = n.generate(50)
        waitFor(10, lambda: compare(n, "index_height", n.getblockcount()))
        waitFor(10, lambda: compare(n, "mempool_count", 0, True))

        logging.info("invalidating %d blocks", len(blocks))
        n.invalidateblock(blocks[0])
        # electrum server should trim its chain as well and see our
        # transaction go back into mempool
        waitFor(10, lambda: compare(n, "index_height", n.getblockcount()))
        waitFor(10, lambda: compare(n, "mempool_count", 1, True))

        n.generate(50)
        waitFor(10, lambda: compare(n, "index_height", n.getblockcount()))
        waitFor(10, lambda: compare(n, "mempool_count", 0, True))


    def setup_network(self, dummy = None):
        self.nodes = self.setup_nodes()

if __name__ == '__main__':
    ElectrumReorgTests().main()
