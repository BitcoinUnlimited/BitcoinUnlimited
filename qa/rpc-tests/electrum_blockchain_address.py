#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Unlimited developers
"""
Tests to check if basic electrum server integration works
"""
import random
from test_framework.util import waitFor, assert_equal, assert_raises
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging
from test_framework.electrumutil import compare, bitcoind_electrum_args, \
    create_electrum_connection, address_to_scripthash, sync_electrum_height
from test_framework.nodemessages import COIN, CTransaction, ToHex, CTxIn, COutPoint


class ElectrumBlockchainAddress(BitcoinTestFramework):
    """
    Basic blockchain.address.* testing, mostly to check that the function
    handle an address correctly. The blockchian.scripthash.* equivalents are
    more thoroughly tested.
    """

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [bitcoind_electrum_args()]

    def run_test(self):
        n = self.nodes[0]

        n.generate(200)

        cli = create_electrum_connection()
        self.test_invalid_args(cli)

        self.test_get_balance(n, cli)
        self.test_get_frist_use(n, cli)
        self.test_get_history(n, cli)
        self.test_list_unspent(n, cli)

    def setup_network(self, dummy = None):
        self.nodes = self.setup_nodes()

    def test_invalid_args(self, cli):
        from test_framework.connectrum.exc import ElectrumErrorResponse
        error_code = "-32602"

        hash_param_methods = (
            "blockchain.address.get_balance",
            "blockchain.address.get_history",
            "blockchain.address.listunspent")

        for method in hash_param_methods:
            assert_raises(
                ElectrumErrorResponse,
                cli.call,
                method, "invalidaddress")

    def test_get_balance(self, n, cli):
        addr = n.getnewaddress()
        balance = 11.42
        txhash = n.sendtoaddress(addr, balance)

        def check_address(address, unconfirmed = 0, confirmed = 0):
            res = cli.call("blockchain.address.get_balance", addr)

            return res["unconfirmed"] == unconfirmed * COIN \
                and res["confirmed"] == confirmed * COIN

        waitFor(10, lambda: check_address(addr, unconfirmed = balance))
        n.generate(1)
        waitFor(10, lambda: check_address(addr, confirmed = balance))

    def test_get_frist_use(self, n, cli):
        addr = n.getnewaddress()
        n.sendtoaddress(addr, 12)

        utxo = [ ]
        def fetch_utxo():
            utxo = cli.call("blockchain.address.listunspent", addr)
            return len(utxo) > 0

        waitFor(10, lambda: fetch_utxo())

        firstuse = cli.call("blockchain.address.get_first_use", addr)

        # NYI. Api not stable.

    def test_list_unspent(self, n, cli):
        addr = n.getnewaddress()
        utxo = cli.call("blockchain.address.listunspent", addr)
        assert_equal(0, len(utxo))

        txid = n.sendtoaddress(addr, 21)
        def fetch_utxo():
            utxo = cli.call("blockchain.address.listunspent", addr)
            if len(utxo) > 0:
                return utxo
            return None

        utxo = waitFor(10, fetch_utxo)
        assert_equal(1, len(utxo))

        assert_equal(0, utxo[0]['height'])
        assert_equal(txid, utxo[0]['tx_hash'])
        assert_equal(21 * COIN, utxo[0]['value'])
        assert(utxo[0]['tx_pos'] in [0, 1])

        n.generate(1)
        utxo = waitFor(10, fetch_utxo)
        waitFor(10, lambda: fetch_utxo()[0]['height'] == n.getblockcount())


    def test_get_history(self, n, cli):
        addr = n.getnewaddress()
        txid = n.sendtoaddress(addr, 11)
        def fetch_history():
            h = cli.call("blockchain.address.get_history", addr)
            if len(h) > 0:
                return h
            return None
        history = waitFor(10, fetch_history)
        assert_equal(1, len(history))

        assert_equal(0, history[0]['height'])
        assert_equal(txid, history[0]['tx_hash'])

        n.generate(1)
        history = waitFor(10, fetch_history)
        assert_equal(1, len(history))
        waitFor(10, lambda: fetch_history()[0]['height'] == n.getblockcount())

if __name__ == '__main__':
    ElectrumBlockchainAddress().main()
