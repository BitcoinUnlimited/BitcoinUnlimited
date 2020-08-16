#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

logging.getLogger().setLevel(logging.INFO)

class GrapheneStage2Test(BitcoinTestFramework):
    expected_stats = {'enabled', 
                      'filter', 
                      'graphene_additional_tx_size', 
                      'graphene_block_size', 
                      'iblt', 
                      'inbound_percent', 
                      'outbound_percent', 
                      'rank', 
                      'rerequested', 
                      'response_time', 
                      'summary', 
                      'validation_time'}
    def __init__(self, test_assertion='success'):
        self.rep = False
        BitcoinTestFramework.__init__(self)

        if test_assertion == 'success':
            self.test_assertion = self.assert_success
        else:
            self.test_assertion = self.assert_failure

    def setup_chain(self, bitcoinConfDict=None, wallets=None):
        print ("Initializing test directory " + self.options.tmpdir)
        # initialize_chain_clean(self.options.tmpdir, 2)
        initialize_chain(self.options.tmpdir, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        node_opts = [
            "-rpcservertimeout=0",
            "-debug=graphene",
            "-use-grapheneblocks=1",
            "-use-thinblocks=0",
            "-net.grapheneIbltSizeOverride=1",
            "-net.grapheneBloomFprOverride=0.0",
            "-net.randomlyDontInv=100",
            "-excessiveblocksize=6000000",
            "-blockprioritysize=6000000",
            "-blockmaxsize=6000000"]

        self.nodes = [
            start_node(0, self.options.tmpdir, node_opts),
            start_node(1, self.options.tmpdir, node_opts)
        ]

        self.is_network_split = False
        interconnect_nodes(self.nodes)
        self.sync_all()

    def extract_stats_fields(self, node):
        gni = node.getnetworkinfo()
        assert "grapheneblockstats" in gni
        tbs = gni["grapheneblockstats"]
        assert "enabled" in tbs and tbs["enabled"]
        assert set(tbs) == self.expected_stats

        return tbs

    def assert_success(self):
        # Nodes 1 should have received one block from node 0 and have experienced on decode failure.
        tmp = self.extract_stats_fields(self.nodes[1])['summary']
        logging.info("graphene summary: %s" % tmp)
        assert '1 inbound and 0 outbound graphene blocks' in tmp
        assert 'bandwidth with 1 local decode failure' in tmp
        # Node 1 should have experienced only one decode failure
        assert 'bandwidth with 2 local decode failure' not in tmp

    def run_test(self):
        # Generate blocks so we can send a few transactions.  We need some transactions in a block
        # before a graphene block can be sent and created, otherwise we'll just end up sending a regular
        # block.
        self.nodes[0].generate(1)
        self.sync_blocks()

        logging.info("Send 5 transactions from node0 (to its own address)")
        addr = self.nodes[0].getnewaddress()
        for i in range(5):
            self.nodes[0].sendtoaddress(addr, Decimal("10"))

        self.nodes[0].generate(1)

        self.sync_blocks()

        self.assert_success()

if __name__ == '__main__':
    GrapheneStage2Test().main()
