#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the finalizeblock RPC calls."""
import os

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import logging
logging.getLogger().setLevel(logging.INFO)

RPC_FINALIZE_INVALID_BLOCK_ERROR = 'finalize-invalid-block'


class FinalizeBlockTest(BitcoinTestFramework):
    # There should only be one chaintip, which is expected_tip
    def only_valid_tip(self, expected_tip, other_tip_status=None):
        node = self.nodes[0]
        assert_equal(node.getbestblockhash(), expected_tip)
        for tip in node.getchaintips():
            if tip["hash"] == expected_tip:
                assert_equal(tip["status"], "active")
            else:
                assert_equal(tip["status"], other_tip_status)

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        logging.info("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=net"]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug=net"]))

    def run_test(self):
        node = self.nodes[0]

        logging.info("Test block finalization...")
        node.generate(10)
        waitFor(10, lambda: node.getblockcount() == 10)
        tip = node.getbestblockhash()
        node.finalizeblock(tip)
        assert_equal(node.getbestblockhash(), tip)

        alt_node = self.nodes[1]
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes[0:1])
        waitFor(10, lambda: alt_node.getblockcount() == 10)

        # Disconnect the alt_node first before mining and then reconnecting. In other
        # single threaded nodes we wouldn't need to do this however in BU we if we
        # didn't disonnect here we could end up with multiple getdata's from the other
        # peer which would result in a first set of headers which are accepted (and then invalidated)
        # followed by a second set of headers which get rejected; if this happens then this python
        # test will fail, hence the need here to disconnect/reconnect which then results in only one
        # getdata and one set of headers and thus we end up with the correct chaintip.
        disconnect_all(alt_node)
        alt_node.invalidateblock(tip)
        alt_node.generate(10)
        waitFor(10, lambda: alt_node.getblockcount() == 19)
        connect_nodes_bi(self.nodes, 0, 1)

        # Wait for node 0 to invalidate the chain.
        status = "False"
        block = alt_node.getbestblockhash()
        time_loop = 0
        while (status is "False"):
            for tip1 in node.getchaintips():
                if tip1["hash"] == block:
                    assert(tip1["status"] != "active")
                    status = tip1["status"] == "invalid"
            time.sleep(1)
            time_loop += 1
            if (time_loop > 10):
                raise Exception("chaintip check failed")

        logging.info("Test that an invalid block cannot be finalized...")
        assert_raises_rpc_error(-20, RPC_FINALIZE_INVALID_BLOCK_ERROR,
                                node.finalizeblock, alt_node.getbestblockhash())

        logging.info(
            "Test that invalidating a finalized block moves the finalization backward...")

        print(str(node.getblockcount()))
        node.invalidateblock(tip)

        node.invalidateblock(node.getbestblockhash())
        node.reconsiderblock(tip)
        assert_equal(node.getbestblockhash(), tip)

        # The node will now accept that chain as the finalized block moved back.
        node.reconsiderblock(alt_node.getbestblockhash())
        assert_equal(node.getbestblockhash(), alt_node.getbestblockhash())

        logging.info("Trigger reorg via block finalization...")
        node.finalizeblock(tip)
        assert_equal(node.getbestblockhash(), tip)

        logging.info("Try to finalized a block on a competiting fork...")
        assert_raises_rpc_error(-20, RPC_FINALIZE_INVALID_BLOCK_ERROR,
                                node.finalizeblock, alt_node.getbestblockhash())


if __name__ == '__main__':
    FinalizeBlockTest().main()
