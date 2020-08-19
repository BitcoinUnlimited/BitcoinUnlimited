#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin ABC developers
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test that mining RPC continues to supply correct transaction metadata after
the Nov 2018 protocol upgrade which engages canonical transaction ordering
"""
import test_framework.loginit
import time
import random
import decimal

from test_framework.blocktools import create_coinbase
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class CTORMiningTest(BitcoinTestFramework):
    def __init__(self):
        super().__init__()
        self.num_nodes = 2
        self.setup_clean_chain = False
        self.mocktime = int(time.time()) - 600 * 100

    def setup_network(self):
        # Setup two nodes so we can getblocktemplate
        # it errors out if it is not connected to other nodes
        self.nodes = []

        opts = ['-spendzeroconfchange=0', '-debug', '-whitelist=127.0.0.1', '-consensus.enableCanonicalTxOrder=1']
        for i in range(2):
            self.nodes.append(start_node(i, self.options.tmpdir, opts))

        connect_nodes(self.nodes[0], 1)
        self.is_network_split = False

    def run_test(self):
        mining_node = self.nodes[0]

        mining_node.getnewaddress()

        # Generate some unspent utxos
        for x in range(150):
            mining_node.generate(1)

        unspent = mining_node.listunspent()

        assert len(unspent) == 100

        transactions = {}

        # Spend all our coinbases
        while len(unspent):
            inputs = []
            # Grab a random number of inputs
            for _ in range(random.randrange(1, 5)):
                txin = unspent.pop()

                inputs.append({
                    'txid': txin['txid'],
                    'vout': 0,  # This is a coinbase

                    # keep track of coinbase value in extra field that should
                    # otherwise be ignored
                    'input_amount' : txin["amount"]
                })
                if len(unspent) == 0:
                    break

            outputs = {}
            # Calculate a unique fee for this transaction
            fee = decimal.Decimal(random.randint(
                1000, 2000)) / decimal.Decimal(1e8)
            # Spend to the same number of outputs as inputs, so we can leave
            # the amounts unchanged and avoid rounding errors.
            #
            # NOTE: There will be 1 sigop per output (which equals the number
            # of inputs now).  We need this randomization to ensure the
            # numbers are properly following the transactions in the block
            # template metadata
            addr = ""
            for inp in inputs:
                addr = mining_node.getnewaddress()
                output = {
                    # 50 BCH per coinbase
                    addr: inp["input_amount"]
                }
                outputs.update(output)

            # Take the fee off the last output to avoid rounding errors we
            # need the exact fee later for assertions
            outputs[addr] -= fee

            rawtx = mining_node.createrawtransaction(inputs, outputs)
            signedtx = mining_node.signrawtransaction(rawtx)
            txid = mining_node.sendrawtransaction(signedtx['hex'])
            # number of outputs is the same as the number of sigchecks in this
            # case
            transactions.update({txid: {'fee': fee, 'sigchecks': len(outputs)}})

        tmpl = mining_node.getblocktemplate()
        assert 'proposal' in tmpl['capabilities']
        assert 'coinbasetxn' not in tmpl

        # Check the template transaction metadata and ordering
        last_txid = 0
        for txn in tmpl['transactions'][1:]:
            txid = txn['hash']
            txnMetadata = transactions[txid]
            expectedFeeSats = int(txnMetadata['fee'] * 10**8)
            expectedSigChecks = txnMetadata['sigchecks']

            txid_decoded = int(txid, 16)

            # Assert we got the expected metadata
            assert(expectedFeeSats == txn['fee'])
            assert(expectedSigChecks == txn['sigchecks'])

            # Assert transaction ids are in order
            assert(last_txid == 0 or last_txid < txid_decoded)
            last_txid = txid_decoded


if __name__ == '__main__':
    CTORMiningTest().main()
