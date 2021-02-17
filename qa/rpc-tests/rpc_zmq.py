#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test for the ZMQ RPC methods."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class RPCZMQTest(BitcoinTestFramework):

    address = "tcp://127.0.0.1:28341" # ZMQ ports of these test must be unique so multiple tests can be run simultaneously

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        self._test_getzmqnotifications()

    def _test_getzmqnotifications(self):
        self.restart_node(0, extra_args=[])
        assert_equal(self.nodes[0].getzmqnotifications(), [])
        self.restart_node(
            0, extra_args=["-zmqpubhashtx={}".format(self.address)])
        assert_equal(self.nodes[0].getzmqnotifications(), [
            {"type": "pubhashtx", "address": self.address},
        ])


if __name__ == '__main__':
    RPCZMQTest().main()

def Test():
    flags = standardFlags()
    t = RPCZMQTest()
    t.drop_to_pdb = True
    t.main(flags)
