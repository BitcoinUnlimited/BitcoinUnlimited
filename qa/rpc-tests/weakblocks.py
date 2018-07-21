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

class WeakblocksTest(BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(4, self.options.tmpdir)
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,2,0)
        connect_nodes_bi(self.nodes,2,3)

        # If not, the framework assumes this partition: (0,1) and (2,3)
        # For more complex partitions, you can't use the self.sync* member functions
        self.is_network_split=False
        self.sync_all()

    def run_test (self):
        logging.info("Running weak blocks test")
        n0, n1, n2, n3 = self.nodes

        # generate enough blocks so that nodes[0] has a balance
        self.sync_blocks()
        n0.generate(101)
        self.sync_blocks()

        assert_equal(n0.getbalance(), 50)

        ### SINGLE NODE WEAK BLOCK SANITY CHECKS
        # check there's no weak blocks around for the freshly started nodes
        wbstats = n0.weakstats()
        wct = n0.weakchaintips()
        assert_equal(wct, [])
        assert_equal(wbstats,
                     {"weakblocksknown" : 0,
                      "weakchaintips" : 0,
                      "weakchainheight" : -1 })

        # generate some weak-only blocks and make sure that they are properly showing
        # up in the various weak blocks calls.
        old_blockcount = n0.getblockcount()

        n0.generate(1, 1000000, "weak-only")
        new_blockcount = n0.getblockcount()
        assert_equal(old_blockcount, new_blockcount) # no new strong block might appear

        wbstats = n0.weakstats()
        wct = n0.weakchaintips()
        assert_equal(len(wct), 1)
        assert_equal(wct[0][1], 0) # weak chain height equals zero (one weak block)
        assert_equal(wbstats["weakblocksknown"], 1)
        assert_equal(wbstats["weakchaintips"], 1)
        assert_equal(wbstats["weakchainheight"], 0)
        assert_equal(wbstats["weakchaintipnumtx"], 1) # only CB

        # and some more weak blocks
        n0.generate(10, 1000000, "weak-only")
        wbstats = n0.weakstats()
        wct = n0.weakchaintips()
        assert_equal(len(wct), 1)
        assert_equal(wct[0][1], 10)
        assert_equal(wbstats["weakblocksknown"], 11)
        assert_equal(wbstats["weakchaintips"], 1)
        assert_equal(wbstats["weakchainheight"], 10)
        assert_equal(wbstats["weakchaintipnumtx"], 1) # again, only CB

        # now, generate one strong block and make sure that all weak blocks are
        # set to 'expired' state
        n0.generate(1, 1000000, "strong-only")
        wbstats = n0.weakstats()
        wct = n0.weakchaintips()
        assert_equal(len(wct), 1) # only one weak chain
        assert_equal(wct[0][1], -1) # that is in expired state (height == -1)

        # at this stage,
        # the weak blocks are still known after receival of a strong block
        # so that they can still be referred to for block transmission etc...
        assert_equal(wbstats["weakblocksknown"], 11)
        assert_equal(wbstats["weakchaintips"], 1)
        assert_equal(wbstats["weakchainheight"], -1)
        assert_equal(len(wbstats.keys()), 3)

        # but with another strong block coming in, all weak blocks should be cleared:
        n0.generate(1, 1000000, "strong-only")
        wbstats = n0.weakstats()
        wct = n0.weakchaintips()
        assert_equal(len(wct), 0) # only one weak chain
        assert_equal(wbstats,
                     {"weakblocksknown": 0, "weakchaintips": 0, "weakchainheight": -1})

        ### WEAK BLOCK TRANSMISSION CHECKS
        # FIXME
if __name__ == '__main__':
    WeakblocksTest().main ()
