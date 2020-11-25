#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Unlimited developers

import asyncio
import time
from test_framework.util import assert_raises_async
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging
from test_framework.electrumutil import (ElectrumConnection,
    address_to_scripthash, bitcoind_electrum_args)
from test_framework.connectrum.exc import ElectrumErrorResponse

MAX_SCRIPTHASH_SUBSCRIPTIONS = 5
SCRIPTHASH_ALIAS_BYTES_LIMIT = 54 * 2 # two bitcoin cash addresses

class ElectrumDoSLimitTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1

        max_args = [
                "-electrum.rawarg=--scripthash-subscription-limit={}".format(MAX_SCRIPTHASH_SUBSCRIPTIONS),
                "-electrum.rawarg=--scripthash-alias-bytes-limit={}".format(SCRIPTHASH_ALIAS_BYTES_LIMIT)
        ]

        self.extra_args = [bitcoind_electrum_args() + max_args]

    def run_test(self):
        n = self.nodes[0]
        n.generate(1)

        async def async_tests():
            await self.test_subscribe_limit(n)
            await self.test_scripthash_alias_limit(n)

        loop = asyncio.get_event_loop()
        loop.run_until_complete(async_tests())

    async def test_subscribe_limit(self, n):
        cli = ElectrumConnection()
        await cli.connect()
        logging.info("Testing scripthash subscription limit.")


        # Subscribe up to limit
        scripthashes = []
        for i in range(0, MAX_SCRIPTHASH_SUBSCRIPTIONS):
            s = address_to_scripthash(n.getnewaddress())
            await cli.subscribe('blockchain.scripthash.subscribe', s)
            scripthashes.append(s)

        # Next subscription should fail
        s = address_to_scripthash(n.getnewaddress())

        await assert_raises_async(
                ElectrumErrorResponse,
                cli.call,
                "blockchain.scripthash.subscribe", s)

        try:
            await cli.call("blockchain.scripthash.subscribe", s)
        except ElectrumErrorResponse as e:
            error_code = "-32600"
            assert error_code in str(e)
            assert "subscriptions limit reached" in str(e)

        # Subscribing to an existing subscription should not affect the limit.
        await cli.subscribe('blockchain.scripthash.subscribe', scripthashes[0])

        # Unsubscribing should allow for a new subscription
        ok = await cli.call('blockchain.scripthash.unsubscribe', scripthashes[0])
        assert(ok)
        await cli.subscribe('blockchain.scripthash.subscribe', s)

        # ... and also enforce the limit again
        await assert_raises_async(ElectrumErrorResponse, cli.call,
                'blockchain.scripthash.subscribe',
                address_to_scripthash(n.getnewaddress()))

    async def test_scripthash_alias_limit(self, n):
        cli = ElectrumConnection()
        await cli.connect()
        addresses = ["bitcoincash:ppwk8u8cg8cthr3jg0czzays6hsnysykes9amw07kv",
                     "bitcoincash:qrsrvtc95gg8rrag7dge3jlnfs4j9pe0ugrmeml950"]

        # Alias limit allows to subscribe to two addresses.
        for a in addresses:
            await cli.subscribe('blockchain.address.subscribe', a)

        # Third address should fail
        third = n.getnewaddress()

        await assert_raises_async(
                ElectrumErrorResponse,
                cli.call,
                "blockchain.address.subscribe", third)

        try:
            await cli.call("blockchain.address.subscribe", third)
        except ElectrumErrorResponse as e:
            error_code = "-32600"
            assert error_code in str(e)
            assert "alias subscriptions limit reached" in str(e)

        # Unsubscribing should allow for a new subscription
        ok = await cli.call('blockchain.address.unsubscribe', addresses[0])
        assert(ok)
        await cli.subscribe('blockchain.address.subscribe', third)

        # ... and also enforce the limit again
        await assert_raises_async(ElectrumErrorResponse, cli.call,
                'blockchain.address.subscribe', n.getnewaddress())


if __name__ == '__main__':
    ElectrumDoSLimitTest().main()
