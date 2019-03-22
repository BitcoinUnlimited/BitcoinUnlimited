#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class ForkTest (BitcoinTestFramework):

    def setup_chain(self, bitcoinConfDict=None, wallets=None):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)

        # Now interconnect the nodes
        connect_nodes_bi(self.nodes, 0, 1)
        # Let the framework know if the network is fully connected.
        # If not, the framework assumes this partition: (0,1) and (2,3)
        # For more complex partitions, you can't use the self.sync* member functions
        self.is_network_split = False
        self.sync_all()

    def run_test(self):

        # Advance the time beyond the timeout value
        #cur_time = int(time.time())
        cur_time = 1526400000 - 10
        self.nodes[0].setmocktime(cur_time)
        self.nodes[1].setmocktime(cur_time)

        self.nodes[0].set("mining.forkMay2018Time=%d" % (cur_time + 10))
        self.nodes[1].set("mining.forkMay2018Time=%d" % (cur_time + 10))

        ebVal = self.nodes[0].get("net.excessiveBlock")

        ret = self.nodes[1].generate(6)
        self.sync_blocks()
        self.nodes[0].generate(6)
        self.sync_blocks()

        # Not forking yet

        # since we change the default for OP_RETURN after the fork happened,
        # the mining.dataCarrierSize should be equal to 223.
        assert(self.nodes[0].get("mining.dataCarrierSize")["mining.dataCarrierSize"] == 223)

        assert_equal(ebVal, self.nodes[0].get("net.excessiveBlock"))
        assert_equal(ebVal, self.nodes[1].get("net.excessiveBlock"))

        self.nodes[0].setmocktime(cur_time + 20)
        self.nodes[1].setmocktime(cur_time + 20)

        self.nodes[0].generate(5)
        self.sync_blocks()
        assert_equal(ebVal, self.nodes[0].get("net.excessiveBlock"))
        assert_equal(ebVal, self.nodes[1].get("net.excessiveBlock"))

        self.nodes[0].generate(1)
        self.sync_blocks()

        # Forked, check that the EB is updated
        assert(ebVal != self.nodes[0].get("net.excessiveBlock"))
        assert(ebVal != self.nodes[1].get("net.excessiveBlock"))
        assert(self.nodes[0].get("net.excessiveBlock")["net.excessiveBlock"] == 32000000)
        assert(self.nodes[1].get("net.excessiveBlock")["net.excessiveBlock"] == 32000000)

        # check that the block size is updated
        d = self.nodes[0].get("mining.*lockSize")
        assert(d["mining.blockSize"] >= d["mining.forkBlockSize"])
        d = self.nodes[1].get("mining.*lockSize")
        assert(d["mining.blockSize"] >= d["mining.forkBlockSize"])

        self.nodes[0].set("net.excessiveBlock=20000000")
        self.nodes[0].set("mining.blockSize=2000000")
        self.nodes[0].set("mining.dataCarrierSize=400")

        self.nodes[1].generate(1)
        self.sync_blocks()

        # check that in subsequent blocks, some stuff is not changed, and some is held to a min
        d = self.nodes[0].get("*")
        assert d["mining.blockSize"] == 2000000  # shouldn't be changed because not consensus
        assert d["net.excessiveBlock"] == 32000000  # Should be changed because the setting we made was < the min
        assert d["mining.dataCarrierSize"] == 400  # Shouldn't be changed because > the min

        self.nodes[0].set("net.excessiveBlock=64000000")
        expectException(lambda: self.nodes[0].set("mining.dataCarrierSize=100"),
                        JSONRPCException, "Invalid Value. Data Carrier minimum size has to be greater of equal to 223 bytes")

        self.nodes[1].generate(1)
        self.sync_blocks()

        d = self.nodes[0].get("*")
        assert d["net.excessiveBlock"] == 64000000  # Shouldn't be changed because the setting we made was > the min

        ###############################################################
        # Stop nodes and restart with the forktime in the past
        # - check that the EB/MG setting are now the new fork settings.

        stop_nodes(self.nodes)
        wait_bitcoinds()

        # Start nodes and set the mocktime to the fork activation time.
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, ["-mocktime=%d" % (cur_time + 10)]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-mocktime=%d" % (cur_time + 10)]))

        # Now interconnect the nodes
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

        # Forked, check that the EB is still updated
        assert(self.nodes[0].get("net.excessiveBlock")["net.excessiveBlock"] == 32000000)
        assert(self.nodes[1].get("net.excessiveBlock")["net.excessiveBlock"] == 32000000)

        # check that the block size did NOT get updated.
        d = self.nodes[0].get("mining.*lockSize")
        assert(d["mining.blockSize"] == 2000000)
        d = self.nodes[1].get("mining.*lockSize")
        assert(d["mining.blockSize"] == 2000000)

if __name__ == '__main__':
    ForkTest().main()


# Create a convenient function for an interactive python debugging session
def Test():
    t = ForkTest()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }

    # you may want these additional flags:
    # "--srcdir=<out-of-source-build-dir>/debug/src"
    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
