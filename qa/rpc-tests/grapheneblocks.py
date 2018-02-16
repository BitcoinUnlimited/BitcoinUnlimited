#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class GrapheneBlockTest(BitcoinTestFramework):
    def __init__(self):
        self.rep = False
        BitcoinTestFramework.__init__(self)

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

    def run_test(self):
        # Generate blocks so we can send a few transactions.  We need some transactions in a block
        # before a graphene block can be sent and created, otherwise we'll just end up sending a regular
        # block.
        self.nodes[0].generate(105)
        self.sync_all()

        # Generate and propagate blocks from a node that has graphene turned on.
        # This should work.
        send_to = {}
        self.nodes[0].keypoolrefill(20)
        for i in range(20):
            send_to[self.nodes[1].getnewaddress()] = Decimal("0.01")
        for i in range(20):
            self.nodes[0].sendmany("", send_to)
        self.sync_all()

        self.nodes[1].generate(1)
        self.sync_all()

        # Generate and propagate blocks from a node that does not have graphene turned on.
        # This should work.
        send_to = {}
        self.nodes[0].keypoolrefill(20)
        for i in range(20):
            send_to[self.nodes[1].getnewaddress()] = Decimal("0.01")
        for i in range(20):
            self.nodes[0].sendmany("", send_to)
        self.sync_all()

        self.nodes[2].generate(1)
        self.sync_all()


        # Check graphene block stats
        gni = self.nodes[0].getnetworkinfo()
        assert "grapheneblockstats" in gni

        tbs = gni["grapheneblockstats"]
        assert "enabled" in tbs and tbs["enabled"]

        assert set(tbs) == {"enabled",
                            "summary",
                            "mempool_limiter",
                            "inbound_percent",
                            "outbound_percent",
                            "response_time",
                            "validation_time",
                            "outbound_bloom_filters",
                            "inbound_bloom_filters",
                            "rerequested"}


if __name__ == '__main__':
    GrapheneBlockTest().main()
