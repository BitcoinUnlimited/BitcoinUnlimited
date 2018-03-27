#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


import re
import sys
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


# running main will force system exit otherwise
sys.exit = lambda x: None


class BlockTest(BitcoinTestFramework):

    def __init__(self, block_type, n_txs):
        self.block_type = block_type
        self.n_txs = n_txs
        self.result = None
        self.rep = False
        BitcoinTestFramework.__init__(self)

    def setup_chain(self):
        print ("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2)

    def setup_network(self, split=False):
        node_opts = [
            "-rpcservertimeout=0",
            "-excessiveblocksize=6000000",
            "-blockprioritysize=6000000",
            "-blockmaxsize=6000000"]

        if self.block_type == 'graphene':
            node_opts.append("-debug=graphene")
            node_opts.append("-use-grapheneblocks=1")
            node_opts.append("-use-thinblocks=0")
        elif self.block_type == 'thin':
            node_opts.append("-debug=thin")
            node_opts.append("-use-grapheneblocks=0")
            node_opts.append("-use-thinblocks=1")

        self.nodes = [
            start_node(0, self.options.tmpdir, node_opts),
            start_node(1, self.options.tmpdir, node_opts)
        ]

        interconnect_nodes(self.nodes)
        self.is_network_split = False
        self.sync_all()

    def extract_stats(self, node):
        gni = node.getnetworkinfo()

        if self.block_type == 'graphene':
            assert "grapheneblockstats" in gni

            tbs = gni["grapheneblockstats"]
        elif self.block_type == 'thin':
            assert "thinblockstats" in gni

            tbs = gni["thinblockstats"]

        return tbs

    def extract_bytes(self, raw_result):
        unit_byte_map = {'B': 1, 'KB': 2**10, 'MB': 2**20, 'GB': 2**30, 'TB': 2**40}

        m = re.match('.*?: ([\d]+.[\d]+)([A-Z]+)', raw_result)

        if m is None:
            raise RuntimeError('Numerical value could not be extracted from: %s' % raw_result)

        value = float(m.group(1))
        units = m.group(2)

        if units not in unit_byte_map:
            raise KeyError('Unknown unit type: %s' % units)

        return int(value * unit_byte_map[units])

    def run_test(self):
        # Generate blocks so we can send a few transactions.  
        self.nodes[0].generate(min(2000, 10 * self.n_txs))
        self.sync_all()

        # Generate and propagate blocks from a node that has graphene or thin turned on.
        self.nodes[0].keypoolrefill(self.n_txs)
        for i in range(self.n_txs):
            self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), Decimal("0.01"))
        self.sync_all()

        self.nodes[1].generate(1)
        self.sync_all()

        if self.block_type == 'graphene':
            block_size = self.extract_bytes(self.extract_stats(self.nodes[1])['graphene_block_size'])
            mempool_info_size = self.extract_bytes(self.extract_stats(self.nodes[1])['inbound_mempool_info'])

            self.result = block_size + mempool_info_size
        elif self.block_type == 'thin':
            block_size = self.extract_bytes(self.extract_stats(self.nodes[1])['thin_block_size'])
            filter_size = self.extract_bytes(self.extract_stats(self.nodes[1])['inbound_bloom_filters'])
            full_tx_size = self.extract_bytes(self.extract_stats(self.nodes[1])['thin_full_tx'])

            self.result = block_size + filter_size - full_tx_size


if __name__ == '__main__':
    n_txs_list = [100]
    actual_n_txs = []
    graphene_sizes = []
    thin_sizes = []

    for n_txs in n_txs_list:
        try:
            graphene_test = BlockTest('graphene', n_txs)
            graphene_test.main()
            graphene_sizes.append(graphene_test.result)

            thin_test = BlockTest('thin', n_txs)
            thin_test.main()
            thin_sizes.append(thin_test.result)

            actual_n_txs.append(n_txs)
        except:
            continue

    print('n_txs graphene_size xthin_size')
    for i in range(len(actual_n_txs)):
        print(actual_n_txs[i], graphene_sizes[i], thin_sizes[i])
