#!/usr/bin/env python3
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
"""
Tests to check if basic electrum server integration works
"""
import random
from test_framework.util import waitFor, assert_equal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging

def compare(node, key, expected, is_debug_data = False):
    info = node.getelectruminfo()
    logging.debug("expecting %s == %s from %s", key, expected, info)
    if is_debug_data:
        info = info['debuginfo']
    if key not in info:
        return False
    return info[key] == expected

class ElectrumBasicTests(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.electrum_port = random.randint(40000, 60000)
        self.monitoring_port = random.randint(40000, 60000)

        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-debug=electrum", "-electrum=1", "-debug=rpc",
            "-electrum.port=" + str(self.electrum_port),
            "-electrum.monitoring.port=" + str(self.monitoring_port)]]

    def run_test(self):
        n = self.nodes[0]

        logging.info("Checking that blocks are indexed")
        n.generate(200)

        # waitFor throws on timeout, failing the test

        waitFor(10, lambda: compare(n, "index_height", n.getblockcount()))
        waitFor(10, lambda: compare(n, "index_txns", n.getblockcount() + 1, True)) # +1 is genesis tx
        waitFor(10, lambda: compare(n, "mempool_count", 0, True))

        logging.info("Check that mempool is communicated")
        n.sendtoaddress(n.getnewaddress(), 1)
        assert_equal(1, len(n.getrawmempool()))
        waitFor(10, lambda: compare(n, "mempool_count", 1, True))

        n.generate(1)
        assert_equal(0, len(n.getrawmempool()))
        waitFor(10, lambda: compare(n, "index_height", n.getblockcount()))
        waitFor(10, lambda: compare(n, "mempool_count", 0, True))
        waitFor(10, lambda: compare(n, "index_txns", n.getblockcount() + 2, True))


    def setup_network(self, dummy = None):
        self.nodes = self.setup_nodes()

if __name__ == '__main__':
    ElectrumBasicTests().main()
