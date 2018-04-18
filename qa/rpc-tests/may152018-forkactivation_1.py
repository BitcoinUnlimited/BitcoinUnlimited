#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging
logging.basicConfig(format='%(asctime)s.%(levelname)s: %(message)s', level=logging.INFO, stream=sys.stdout)

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
        cur_time = int(time.time())
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

        assert(self.nodes[0].get("mining.dataCarrierSize")["mining.dataCarrierSize"] == 83)

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

        # check that the datacarrier size is updated
        assert(self.nodes[0].get("mining.dataCarrierSize")["mining.dataCarrierSize"] == 223)


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
    # "--tmpdir=/ramdisk/test"
    t.main(["--nocleanup", "--noshutdown"], bitcoinConf, None)
