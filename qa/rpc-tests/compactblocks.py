#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2016 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class ThinBlockTest(BitcoinTestFramework):
    def __init__(self):
        self.rep = False
        BitcoinTestFramework.__init__(self)

    def setup_chain(self):
        print ("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def setup_network(self, split=False):
        node_opts1 = [
            "-rpcservertimeout=0",
            "-debug=cmpctblocks",
            "-debug=mempool",
            "-use-grapheneblocks=0",
            "-use-thinblocks=0",
            "-use-compactblocks=1",
            "-use-bloom-filter-targeting=0",
            "-excessiveblocksize=6000000",
            "-blockprioritysize=6000000",
            "-blockmaxsize=6000000",
            "-peerbloomfilters=1"]

        # These options have peerbloomfiters turned off.  Xthin's should still work with this option turned off.
        node_opts2 = [
            "-rpcservertimeout=0",
            "-debug=cmpctblocks",
            "-debug=mempool",
            "-use-grapheneblocks=0",
            "-use-thinblocks=0",
            "-use-compactblocks=1",
            "-use-bloom-filter-targeting=0",
            "-excessiveblocksize=6000000",
            "-blockprioritysize=6000000",
            "-blockmaxsize=6000000",
            "-peerbloomfilters=0"]

        # This node has bloom filter targeting enabled.
        node_opts3 = [
            "-rpcservertimeout=0",
            "-debug=cmpctblocks",
            "-debug=mempool",
            "-use-grapheneblocks=0",
            "-use-thinblocks=0",
            "-use-compactblocks=1",
            "-use-bloom-filter-targeting=1",
            "-excessiveblocksize=6000000",
            "-blockprioritysize=6000000",
            "-blockmaxsize=6000000",
            "-peerbloomfilters=1"]

        self.nodes = [
            start_node(0, self.options.tmpdir, node_opts1),
            start_node(1, self.options.tmpdir, node_opts1),
            start_node(2, self.options.tmpdir, node_opts2),
            start_node(3, self.options.tmpdir, node_opts3)
        ]
        interconnect_nodes(self.nodes)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        # Generate blocks so we can send a few transactions.  We need some transactions in a block
        # before an xthin can be send and created, otherwise we'll just end up sending a regular
        # block.
        self.nodes[0].generate(100)
        self.sync_all()
        self.nodes[0].generate(5)
        self.sync_all()

        # Generate and propagate blocks from a node that has bloomfiltering turned on.
        # This should work.
        send_to = {}
        self.nodes[0].keypoolrefill(20)
        for i in range(20):
            send_to[self.nodes[1].getnewaddress()] = Decimal("0.01")
        self.nodes[0].sendmany("", send_to)
        self.sync_all()

        self.nodes[1].generate(1)
        self.sync_all()

        # Generate and propagate blocks from a node that does not have bloomfiltering turned on.
        # This should work.
        send_to = {}
        self.nodes[0].keypoolrefill(20)
        for i in range(20):
            send_to[self.nodes[1].getnewaddress()] = Decimal("0.01")
        self.nodes[0].sendmany("", send_to)
        self.sync_all()

        self.nodes[2].generate(1)
        self.sync_all()


        # Check thinblock stats
        gni = self.nodes[0].getnetworkinfo()
        assert "compactblockstats" in gni

        tbs = gni["compactblockstats"]
        assert "enabled" in tbs and tbs["enabled"]

        assert set(tbs) == {"enabled",
                            "summary",
                            "mempool_limiter",
                            "inbound_percent",
                            "outbound_percent",
                            "response_time",
                            "validation_time",
                            "compact_block_size",
                            "compact_full_tx",
                            "rerequested"}

        # test clear block stats function
        self.nodes[0].clearblockstats()
        gni = self.nodes[0].getnetworkinfo()
        tbs = gni["compactblockstats"]

        # TODO: this currently not working due to #1326
        #assert tbs['summary'] == '0 inbound and 0 outbound compact blocks have saved 0.00B of bandwidth'

if __name__ == '__main__':
    ThinBlockTest().main()
