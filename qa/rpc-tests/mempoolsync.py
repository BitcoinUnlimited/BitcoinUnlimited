#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import time


class MempoolSyncTest(BitcoinTestFramework):
    def __init__(self, test_assertion='success'):
        self.rep = False

    def set_test_params(self):
        self.num_nodes = 3

    def setup_network(self, split=False):
        node_opts = [
            "-rpcservertimeout=0",
            "-debug=mempoolsync",
            "-net.syncMempoolWithPeers=1",
            "-net.randomlyDontInv=100",
            "-excessiveblocksize=6000000",
            "-blockprioritysize=6000000",
            "-blockmaxsize=6000000"]

        self.nodes = [
            start_node(0, self.options.tmpdir, node_opts),
            start_node(1, self.options.tmpdir, node_opts),
            start_node(2, self.options.tmpdir, node_opts)
        ]

        interconnect_nodes(self.nodes)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        chain_height = self.nodes[0].getblockcount()
        assert_equal(chain_height, 200)

        logging.info("Mine a single block to get out of IBD")
        self.nodes[0].generate(1)
        self.sync_all()

        logging.info("Send 10 transactions from node0 (to its own address)")
        addr = self.nodes[0].getnewaddress()
        for i in range(10):
            self.nodes[0].sendtoaddress(addr, Decimal("10"))

        waitFor(180, lambda: len(self.nodes[1].getrawmempool()) == 10)
        waitFor(180, lambda: len(self.nodes[2].getrawmempool()) == 10)

if __name__ == '__main__':
    MempoolSyncTest().main()
