#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
# This is a template to make creating new QA tests easy.
# You can also use this template to quickly start and connect a few regtest nodes.

import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class MyTest (BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        # pick this one to start from the cached 4 node 100 blocks mined configuration
        # initialize_chain(self.options.tmpdir, bitcoinConfDict, wallets)
        # pick this one to start at 0 mined blocks
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)
        # Number of nodes to initialize ----------> ^

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        # Nodes to start --------^
        # Note for this template I readied 4 nodes but only started 2

        # Now interconnect the nodes
        connect_nodes_full(self.nodes)
        # Let the framework know if the network is fully connected.
        # If not, the framework assumes this partition: (0,1) and (2,3)
        # For more complex partitions, you can't use the self.sync* member functions
        self.is_network_split=False
        self.sync_blocks()

    def run_test (self):

        logging.info("This is a template for you to use when making new tests")

        # generate enough blocks so that nodes[0] has a balance
        self.sync_blocks()
        self.nodes[0].generate(101)
        self.sync_blocks()

        assert_equal(self.nodes[0].getbalance(), 50)

        # Check that only first and second nodes have UTXOs
        assert_equal(len(self.nodes[0].listunspent()), 1)
        assert_equal(len(self.nodes[1].listunspent()), 0)

        # Send 1 BTC from 0 to 2 using sendtoaddress call.
        try:
            self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        except JSONRPCException as e: # an exception you don't catch is a testing error
            raise

        # example of stopping and restarting the nodes
        stop_nodes(self.nodes)
        wait_bitcoinds()
        # start 4 nodes this time with some different configuration
        self.nodes = start_nodes(4, self.options.tmpdir, [ ["-net.txRetryInterval=5000000"], [], [], []])
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        connect_nodes_bi(self.nodes,2,3)
        self.sync_blocks()


if __name__ == '__main__':
    MyTest ().main ()

# Create a convenient function for an interactive python debugging session
def Test():
    t = MyTest()
    # t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }


    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
