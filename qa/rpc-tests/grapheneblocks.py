#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class GrapheneBlockTest(BitcoinTestFramework):
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

    def setup_chain(self):
        print ("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        node_opts = [
            "-rpcservertimeout=0",
            "-debug=graphene",
            "-use-grapheneblocks=1",
            "-use-thinblocks=0",
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

    def extract_stats_fields(self, node):
        gni = node.getnetworkinfo()
        assert "grapheneblockstats" in gni
        tbs = gni["grapheneblockstats"]
        assert "enabled" in tbs and tbs["enabled"]
        assert set(tbs) == self.expected_stats

        return tbs

    def assert_success(self):
        # Nodes 0 and 1 should have received one block from node 2.
        assert '1 inbound and 0 outbound graphene blocks' in self.extract_stats_fields(self.nodes[0])['summary']
        assert '1 inbound and 0 outbound graphene blocks' in self.extract_stats_fields(self.nodes[1])['summary']

        # Node 2 should have sent a block to the two other nodes
        assert '0 inbound and 2 outbound graphene blocks' in self.extract_stats_fields(self.nodes[2])['summary']

    def assert_failure(self):
        try:
            self.assert_success()
        except AssertionError:
            return

        raise AssertionError('graphene block failure was expected but not encountered')

    def run_test(self):
        # Generate blocks so we can send a few transactions.  We need some transactions in a block
        # before a graphene block can be sent and created, otherwise we'll just end up sending a regular
        # block.
        self.nodes[0].generate(105)
        self.sync_all()

        # Node 1 generates and propagates a graphene block.
        send_to = {}
        self.nodes[0].keypoolrefill(2)
        for i in range(10):
            send_to[self.nodes[1].getnewaddress()] = Decimal("0.01")
        self.nodes[0].sendmany("", send_to)
        self.sync_all()

        self.nodes[1].generate(1)
        self.sync_all()

        # Testing the clear stats function here
        for node in self.nodes:
            node.clearblockstats()

        # Node 2 generates and propagates a graphene block.
        send_to = {}
        self.nodes[0].keypoolrefill(2)
        for i in range(10):
            send_to[self.nodes[2].getnewaddress()] = Decimal("0.01")
        self.nodes[0].sendmany("", send_to)
        self.sync_all()

        self.nodes[2].generate(1)
        self.sync_all()

        self.test_assertion()

if __name__ == '__main__':
    GrapheneBlockTest().main()
