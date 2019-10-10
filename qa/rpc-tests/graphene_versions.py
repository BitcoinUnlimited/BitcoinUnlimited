#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from grapheneblocks import GrapheneBlockTest
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class GrapheneOptimizedTest(GrapheneBlockTest):
   
    def __init__(self, vL1, vH1, vL2, vH2, test_assertion):
        self.vL1 = vL1
        self.vH1 = vH1
        self.vL2 = vL2
        self.vH2 = vH2

        super(GrapheneOptimizedTest, self).__init__(test_assertion)

    def setup_network(self, split=False):
        type1_opts = [
            "-rpcservertimeout=0",
            "-debug=graphene",
            "-use-grapheneblocks=1",
            "-use-thinblocks=0",
            "-use-compactblocks=0",
            "-net.grapheneMinVersionSupported=" + self.vL1,
            "-net.grapheneMaxVersionSupported=" + self.vH1,
            "-net.grapheneFastFilterCompatibility=0",
            "-excessiveblocksize=6000000",
            "-blockprioritysize=6000000",
            "-blockmaxsize=6000000"]

        type2_opts = [
            "-rpcservertimeout=0",
            "-debug=graphene",
            "-use-grapheneblocks=1",
            "-use-thinblocks=0",
            "-use-compactblocks=0",
            "-net.grapheneMinVersionSupported=" + self.vL2,
            "-net.grapheneMaxVersionSupported=" + self.vH2,
            "-net.grapheneFastFilterCompatibility=0",
            "-excessiveblocksize=6000000",
            "-blockprioritysize=6000000",
            "-blockmaxsize=6000000"]

        self.nodes = [
            start_node(0, self.options.tmpdir, type1_opts),
            start_node(1, self.options.tmpdir, type2_opts),
            start_node(2, self.options.tmpdir, type1_opts)
        ]

        interconnect_nodes(self.nodes)
        self.is_network_split = False
        self.sync_all()

def main(flags=None):
    t = GrapheneOptimizedTest('1', '3', '3', '4', 'success')
    t.main(flags)
    stop_nodes(t.nodes)

    t = GrapheneOptimizedTest('0', '4', '1', '3', 'success')
    t.main(flags)
    stop_nodes(t.nodes)

    t = GrapheneOptimizedTest('1', '2', '3', '4', 'failure')
    t.main(flags)
    stop_nodes(t.nodes)

if __name__ == '__main__':
    sys.exit(main())

# Create a convenient function for an interactive python debugging session
def Test():
    flags = standardFlags()
    main(flags)

