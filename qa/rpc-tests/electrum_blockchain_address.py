#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Unlimited developers
"""
Tests to check if basic electrum server integration works
"""
import random
import asyncio
from test_framework.util import waitForAsync, assert_equal, assert_raises_async
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging
from test_framework.electrumutil import compare, bitcoind_electrum_args, \
    address_to_scripthash, sync_electrum_height, ElectrumConnection
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

        async def async_tests():
            cli = ElectrumConnection()
            await cli.connect()
            await self.test_invalid_args(cli)
            await self.test_get_balance(n, cli)
            await self.test_get_frist_use(n, cli)
            await self.test_get_history(n, cli)
            await self.test_list_unspent(n, cli)
        loop = asyncio.get_event_loop()
        loop.run_until_complete(async_tests())

    def setup_network(self, dummy = None):
        self.nodes = self.setup_nodes()

    async def test_invalid_args(self, cli):
        from test_framework.connectrum.exc import ElectrumErrorResponse
        error_code = "-32602"

        hash_param_methods = (
            "blockchain.address.get_balance",
            "blockchain.address.get_history",
            "blockchain.address.listunspent")

        for method in hash_param_methods:
            await assert_raises_async(
                ElectrumErrorResponse,
                cli.call,
                method, "invalidaddress")

    async def test_get_balance(self, n, cli):
        addr = n.getnewaddress()
        balance = 11.42
        txhash = n.sendtoaddress(addr, balance)

        async def check_address(address, unconfirmed = 0, confirmed = 0):
            res = await cli.call("blockchain.address.get_balance", addr)

            return res["unconfirmed"] == unconfirmed * COIN \
                and res["confirmed"] == confirmed * COIN

        await waitForAsync(10, lambda: check_address(addr, unconfirmed = balance))
        n.generate(1)
        await waitForAsync(10, lambda: check_address(addr, confirmed = balance))

    async def test_get_frist_use(self, n, cli):
        addr = n.getnewaddress()
        n.sendtoaddress(addr, 12)

        utxo = [ ]
        async def fetch_utxo():
            utxo = await cli.call("blockchain.address.listunspent", addr)
            return len(utxo) > 0

        await waitForAsync(10, fetch_utxo)
        #firstuse = await cli.call("blockchain.address.get_first_use", addr)

        # NYI. Api not stable.

    async def test_list_unspent(self, n, cli):
        addr = n.getnewaddress()
        utxo = await cli.call("blockchain.address.listunspent", addr)
        assert_equal(0, len(utxo))

        txid = n.sendtoaddress(addr, 21)
        async def fetch_utxo():
            utxo = await cli.call("blockchain.address.listunspent", addr)
            if len(utxo) > 0:
                return utxo
            return None

        utxo = await waitForAsync(10, fetch_utxo)
        assert_equal(1, len(utxo))

        assert_equal(0, utxo[0]['height'])
        assert_equal(txid, utxo[0]['tx_hash'])
        assert_equal(21 * COIN, utxo[0]['value'])
        assert(utxo[0]['tx_pos'] in [0, 1])

        n.generate(1)
        async def wait_for_confheight():
            utxo = await cli.call("blockchain.address.listunspent", addr)
            return len(utxo) == 1 and utxo[0]['height'] == n.getblockcount()
        await waitForAsync(10, wait_for_confheight)


    async def test_get_history(self, n, cli):
        addr = n.getnewaddress()
        txid = n.sendtoaddress(addr, 11)
        async def fetch_history():
            h = await cli.call("blockchain.address.get_history", addr)
            if len(h) > 0:
                return h
            return None
        history = await waitForAsync(10, fetch_history)
        assert_equal(1, len(history))

        UNCONFIRMED_HEIGHT = 0
        assert_equal(UNCONFIRMED_HEIGHT, history[0]['height'])
        assert_equal(txid, history[0]['tx_hash'])

        n.generate(1)
        async def wait_for_confheight():
            h = await cli.call("blockchain.address.get_history", addr)
            return len(h) == 1 and h[0]['height'] == n.getblockcount()
        await waitForAsync(10, wait_for_confheight)

if __name__ == '__main__':
    ElectrumBlockchainAddress().main()
