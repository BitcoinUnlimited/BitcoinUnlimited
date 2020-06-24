#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Unlimited developers
"""
Tests to check if basic electrum server integration works
"""
import random
import asyncio
from test_framework.util import waitFor, assert_equal, assert_raises_async, waitForAsync
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging
from test_framework.electrumutil import compare, bitcoind_electrum_args, \
    ElectrumConnection, address_to_scripthash
from test_framework.nodemessages import COIN, CTransaction, ToHex, CTxIn, COutPoint


class ElectrumBasicTests(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [bitcoind_electrum_args()]

    def run_test(self):
        n = self.nodes[0]

        logging.info("Checking that blocks are indexed")
        n.generate(200)

        self.test_mempoolsync(n)
        async def async_tests():
            electrum_client = ElectrumConnection()
            await electrum_client.connect()
            await self.test_unknown_method(electrum_client)
            await self.test_invalid_args(electrum_client)
            await self.test_address_balance(n, electrum_client)
        loop = asyncio.get_event_loop()
        loop.run_until_complete(async_tests())

    def test_mempoolsync(self, n):
        # waitFor throws on timeout, failing the test
        waitFor(10, lambda: compare(n, "index_height", n.getblockcount()))
        waitFor(10, lambda: compare(n, "index_txns", n.getblockcount() + 1, True)) # +1 is genesis tx
        waitFor(10, lambda: compare(n, "mempool_count", 0, True))

        logging.info("Check that mempool is communicated")
        n.sendtoaddress(n.getnewaddress(), 1)
        assert_equal(1, len(n.getrawmempool()))
        waitFor(10, lambda: compare(n, "mempool_count", 1, True))

        n.generate(1)
        assert_equal(0, len(n.getrawmempool()))
        waitFor(10, lambda: compare(n, "index_height", n.getblockcount()))
        waitFor(10, lambda: compare(n, "mempool_count", 0, True))
        waitFor(10, lambda: compare(n, "index_txns", n.getblockcount() + 2, True))

    async def test_unknown_method(self, electrum_client):
        from test_framework.connectrum.exc import ElectrumErrorResponse

        await assert_raises_async(
                ElectrumErrorResponse,
                electrum_client.call,
                "method.that.does.not.exist", 42)

        try:
            await electrum_client.call("method.that.does.not.exist")
        except ElectrumErrorResponse as e:
            error_code = "-32601"
            assert error_code in str(e)

    async def test_invalid_args(self, electrum_client):
        from test_framework.connectrum.exc import ElectrumErrorResponse
        error_code = "-32602"

        hash_param_methods = (
            "blockchain.scripthash.get_balance",
            "blockchain.scripthash.get_history",
            "blockchain.scripthash.listunspent")

        for method in hash_param_methods:
            await assert_raises_async(
                    ElectrumErrorResponse,
                    electrum_client.call,
                    method, "invalidhash")
            try:
                await electrum_client.call(method, "invalidhash")
            except ElectrumErrorResponse as e:
                print("ERROR:" + str(e))
                assert error_code in str(e)

        # invalid tx
        try:
            tx = CTransaction()
            tx.calc_sha256()
            tx.vin = [CTxIn(COutPoint(0xbeef, 1))]
            await electrum_client.call("blockchain.transaction.broadcast", ToHex(tx))
        except Exception as e:
            print("ERROR: " + str(e))
            assert error_code in str(e)




    async def test_address_balance(self, n, electrum_client):
        addr = n.getnewaddress()
        txhash = n.sendtoaddress(addr, 1)

        scripthash = address_to_scripthash(addr)

        async def check_address(address, unconfirmed = 0, confirmed = 0):
            res = await electrum_client.call("blockchain.scripthash.get_balance",
                    address_to_scripthash(addr))

            return res["unconfirmed"] == unconfirmed * COIN \
                and res["confirmed"] == confirmed * COIN

        await waitForAsync(10, lambda: check_address(scripthash, unconfirmed = 1))
        n.generate(1)
        await waitForAsync(10, lambda: check_address(scripthash, confirmed = 1))



    def setup_network(self, dummy = None):
        self.nodes = self.setup_nodes()

if __name__ == '__main__':
    ElectrumBasicTests().main()
