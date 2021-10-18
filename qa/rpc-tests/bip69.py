#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
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
import binascii
from test_framework.script import *
from test_framework.nodemessages import *

class BIP69Test (BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        logging.info("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.node_args = [['-usehd=0'], ['-usehd=0'], ['-usehd=0']]
        self.nodes = start_nodes(3, self.options.tmpdir, self.node_args)
        connect_nodes_full(self.nodes)
        self.is_network_split=False
        self.sync_blocks()

    def validate_inputs(self, inputs):
        last_hash = ""
        last_n = 0;
        first = False
        for tx_input in inputs:
            if (first == False):
                first = True
                last_hash = tx_input["txid"]
                last_n = tx_input["vout"]
                continue
            if last_hash > tx_input["txid"]:
                return False
            if last_hash == tx_input["txid"]:
                if last_n > tx_input["vout"]:
                    return False
            last_hash = tx_input["txid"]
            last_n = tx_input["vout"]
        return True

    def validate_outputs(self, outputs):
        last_value = 0
        last_pubkey = "";
        first = False
        for tx_output in outputs:
            if (first == False):
                first = True
                last_value = tx_output["value"]
                last_pubkey = tx_output["scriptPubKey"]["hex"]
                continue
            if last_value > tx_output["value"]:
                return False
            if last_value == tx_output["value"]:
                if last_pubkey > tx_output["scriptPubKey"]["hex"]:
                    return False
            last_value = tx_output["value"]
            last_pubkey = tx_output["scriptPubKey"]["hex"]
        return True

    def run_test (self):

        # Check that there's no UTXO on none of the nodes
        assert_equal(len(self.nodes[0].listunspent()), 0)
        assert_equal(len(self.nodes[1].listunspent()), 0)
        assert_equal(len(self.nodes[2].listunspent()), 0)

        logging.info("Mining blocks...")

        self.nodes[0].generate(200)

        # get some addresses
        addr1 = self.nodes[1].getnewaddress()
        addr2 = self.nodes[1].getnewaddress()
        addr3 = self.nodes[1].getnewaddress()
        addr4 = self.nodes[1].getnewaddress()
        addr5 = self.nodes[1].getnewaddress()
        addr6 = self.nodes[1].getnewaddress()
        # make a large transaction with multiple inputs and outputs
        txid1 = self.nodes[0].sendmany("", {addr1:2.345, addr2:1.23, addr3:55, addr4:23.478, addr5:60, addr6:55})

        # check that the transaction is BIP69 sorted
        tx1 = self.nodes[0].getrawtransaction(txid1, True)
        assert_equal(self.validate_inputs(tx1["vin"]), True)
        assert_equal(self.validate_outputs(tx1["vout"]), True)



if __name__ == '__main__':
    BIP69Test ().main ()

def Test():
    t = BIP69Test()
    bitcoinConf = {
        "debug": ["selectcoins", "rpc","net", "blk", "thin", "mempool", "req", "bench", "evict"]
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
