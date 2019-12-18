#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Unlimited developers
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

def GetLow64(strhash):
    return "0x" + strhash[48:]

class RandomPruning (BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        # pick this one to start from the cached 4 node 100 blocks mined configuration
        # initialize_chain(self.options.tmpdir, bitcoinConfDict, wallets)
        # pick this one to start at 0 mined blocks
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)
        # Number of nodes to initialize ----------> ^

    def setup_network(self, split=False):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=0", "-useblockdb=1"]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug=prune", "-useblockdb=1", "-prunewithmask=1", "-prune.hashMaskThreshold=10"]))

        # Now interconnect the nodes
        connect_nodes_full(self.nodes)
        # Let the framework know if the network is fully connected.
        # If not, the framework assumes this partition: (0,1) and (2,3)
        # For more complex partitions, you can't use the self.sync* member functions
        self.is_network_split=False
        self.sync_blocks()

    def run_test (self):
        # generate enough blocks so that nodes[0] has a balance
        # mine some number of blocks higher than the minimum we would be pruning
        self.sync_blocks()
        blocks_mined = []
        for i in range(101):
            blockhash = self.nodes[0].generate(1)[0]
            # dont add blocks above the min blocks to keep line
            if (i < 80):
                blocks_mined.append(blockhash)
        self.sync_blocks()

        #get the max possible value of our hashmask
        uint64_t_max = (2**64) - 1
        # calculate 1% which
        threshold_percent = uint64_t_max / 100
        blockchaininfo = self.nodes[1].getblockchaininfo()
        hashMask64 = blockchaininfo["pruneHashMask"]
        threshold = blockchaininfo["hashMaskThreshold"]
        # calculate the normalize threshold for the node
        normalized_threshold = threshold * threshold_percent

        # our threshold should be 10
        assert_equal({'prune.hashMaskThreshold': 10}, self.nodes[1].get("prune.hashMaskThreshold"))

        # check the blocks mined prior to determine if we are keeping the blocks we expect to have
        # and pruning the ones we expect to prune

        # kept does nothing in the actual test but it was/is useful for debugging if the test fails
        kept = []
        for block in blocks_mined:
            assert_equal(len(block), 64)
            low64block = GetLow64(block)
            # if the 64LSB of the blockhash is equal to or above our threshold we should have pruned it
            # assert this is the case
            valxmask = int(low64block, 16) ^ hashMask64
            if valxmask >= normalized_threshold:
                assert_raises_rpc_error(-1, "Block not available (pruned data)", self.nodes[1].getblock, block)
            else:
                kept.append(self.nodes[1].getblock(block)['height'])

        print("checking if we can raise the threshold higher than the current one... (should not be able to)")
        # we should get an error raising the threshold higher than 10% since that was its last value
        assert_raises_rpc_error(-1, "current hashMaskThreshold (10) is smaller than your proposed new threshold (70)",
        self.nodes[1].set, "prune.hashMaskThreshold=70")
        # our threshold should still be 10
        print("checking to make sure nothing has changed...")
        assert_equal({'prune.hashMaskThreshold': 10}, self.nodes[1].get("prune.hashMaskThreshold"))
        # we should be able to lower it, for example to 5
        print("attempting to lower threshold from 10 to 5... (should be able to)")
        self.nodes[1].set("prune.hashMaskThreshold=5")
        # our threshold should now be 5
        print("checking to make sure threshold was lowered...")
        assert_equal({'prune.hashMaskThreshold': 5}, self.nodes[1].get("prune.hashMaskThreshold"))
        print("stopping nodes...")
        # stop the nodes
        stop_nodes(self.nodes)
        wait_bitcoinds()
        print("starting nodes...")
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=prune", "-useblockdb=1"]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug=prune", "-useblockdb=1"]))
        connect_nodes_full(self.nodes)
        # our prune threshold should be persistant across restarts
        print("checking that threshold is persistant across node restarts...")
        assert_equal({'prune.hashMaskThreshold': 5}, self.nodes[1].get("prune.hashMaskThreshold"))


        blocks_mined2 = []
        for i in range(200):
            blockhash = self.nodes[0].generate(1)[0]
            # dont add blocks above the min blocks to keep line
            if (i + 100 < 380):
                blocks_mined.append(blockhash)
            if (i % 100 == 0):
                self.sync_blocks()
        self.sync_blocks()

        blockchaininfo = self.nodes[1].getblockchaininfo()
        fullhashMask = blockchaininfo["pruneHashMask"]
        threshold = blockchaininfo["hashMaskThreshold"]
        normalized_threshold = threshold * threshold_percent

        kept2 = []
        for block in blocks_mined2:
            assert_equal(len(block), 64)
            low64block = GetLow64(block)
            # if the 64LSB of the blockhash is equal to or above our threshold we should have pruned it
            # assert this is the case
            valxmask = (int(low64block, 16) ^ int(hashMask64, 16))
            if valxmask >= normalized_threshold:
                assert_raises_rpc_error(-1, "Block not available (pruned data)", self.nodes[1].getblock, block)
            else:
                kept2.append(self.nodes[1].getblock(block)['height'])



if __name__ == '__main__':
    RandomPruning ().main ()

# Create a convenient function for an interactive python debugging session
def Test():
    t = RandomPruning()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }


    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
