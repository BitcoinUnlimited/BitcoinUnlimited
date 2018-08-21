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
        initialize_chain(self.options.tmpdir)

    def setup_network(self, split=False):
        self.nodes = start_nodes(1, self.options.tmpdir)
        self.is_network_split=False

    def run_test (self):
        # note that these tests rely on tweaks that may be changed or removed.

        node = self.nodes[0]

        # check basic set/get access
        node.set("mining.blockSize=100000")
        assert node.get("mining.blockSize")["mining.blockSize"] == 100000

        # check double set and then double get
        node.set("mining.blockSize=200000","mining.comment=slartibartfast dug here")
        data = node.get("mining.blockSize", "mining.comment")
        assert data["mining.blockSize"] == 200000
        assert data["mining.comment"] == "slartibartfast dug here"

        # check incompatible double set
        try:
            node.set("mining.blockSize=300000","net.excessiveBlock=10000")
            assert 0 # the 2nd param is inconsistent with the current state of mining.blockSize
        except JSONRPCException as e:
            # if one set fails, no changes should be made (set is atomic)
            assert node.get("mining.blockSize")["mining.blockSize"] == 200000

        # check wildcard
        netTweaks = node.get("net.*")
        for n,val in netTweaks.items():
            assert n.startswith("net.")

        # check equivalence of no args and *
        data = node.get()
        data1 = node.get("*")
        assert data == data1



if __name__ == '__main__':
    MyTest ().main ()

# Create a convenient function for an interactive python debugging session
def Test():
    t = MyTest()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }

    flags = []
    # you may want these additional flags:
    # flags.append("--nocleanup")
    # flags.append("--noshutdown")

    # Execution is much faster if a ramdisk is used, so use it if one exists in a typical location
    if os.path.isdir("/ramdisk/test"):
        flags.append("--tmppfx=/ramdisk/test")

    # Out-of-source builds are awkward to start because they need an additional flag
    # automatically add this flag during testing for common out-of-source locations
    here = os.path.dirname(os.path.abspath(__file__))
    if not os.path.exists(os.path.abspath(here + "/../../src/bitcoind")):
        dbg = os.path.abspath(here + "/../../debug/src/bitcoind")
        rel = os.path.abspath(here + "/../../release/src/bitcoind")
        if os.path.exists(dbg):
            print("Running from the debug directory (%s)" % dbg)
            flags.append("--srcdir=%s" % os.path.dirname(dbg))
        elif os.path.exists(rel):
            print("Running from the release directory (%s)" % rel)
            flags.append("--srcdir=%s" % os.path.dirname(rel))

    t.main(flags, bitcoinConf, None)
