#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Unlimited developers
"""
Tests the electrum call 'blockchain.transaction.get_merkle'
"""
import asyncio
from test_framework.util import assert_equal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.electrumutil import (
        ERROR_CODE_INVALID_PARAMS,
        ElectrumConnection,
        assert_response_error,
        bitcoind_electrum_args,
        wait_for_electrum_mempool,
)

class ElectrumGetMerkle(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [bitcoind_electrum_args()]

    def run_test(self):
        n = self.nodes[0]

        n.generate(110)

        async def async_tests(loop):
            cli = ElectrumConnection(loop)
            await cli.connect()

            await self.test_basic(n, cli)

            #await cli.disconnect();
            # TODO: Test the merkle proof

        loop = asyncio.get_event_loop()
        loop.run_until_complete(async_tests(loop))

    async def test_basic(self, n, cli):
        txid = n.sendtoaddress(n.getnewaddress(), 1)
        wait_for_electrum_mempool(n, count = 1)

        # Invalid request, should throw "not confirmed" error
        await assert_response_error(
                lambda: cli.call("blockchain.transaction.get_merkle", txid),
                ERROR_CODE_INVALID_PARAMS,
                "is not confirmed in a block")

        n.generate(1)
        wait_for_electrum_mempool(n, count = 0)

        # Test valid request
        height = n.getblockcount()
        res1 = await cli.call("blockchain.transaction.get_merkle", txid, height)

        # rostrum allows height to be optional (outside of specification)
        res2 = await cli.call("blockchain.transaction.get_merkle", txid)
        assert_equal(res1, res2)

        assert_equal(height, res1['block_height'])
        assert 'merkle' in res1
        assert_equal(1, res1['pos'])

if __name__ == '__main__':
    ElectrumGetMerkle().main()
