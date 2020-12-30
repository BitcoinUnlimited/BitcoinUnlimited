#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Unlimited developers
"""
Tests specific for the electrum call 'blockchain.scripthash.get_history'
"""
import asyncio
from test_framework.util import assert_equal
from test_framework.electrumutil import (
        ElectrumTestFramework,
        ElectrumConnection,
        script_to_scripthash,
        sync_electrum_height)
from test_framework.blocktools import create_transaction, pad_tx
from test_framework.script import CScript, OP_TRUE, OP_DROP, OP_NOP

GET_HISTORY = "blockchain.scripthash.get_history"

class ElectrumScripthashGetHistory(ElectrumTestFramework):

    def run_test(self):
        n = self.nodes[0]
        self.bootstrap_p2p()
        coinbases = self.mine_blocks(n, 100)

        async def async_tests(loop):
            cli = ElectrumConnection(loop)
            await cli.connect()
            await self.test_blockheight_confirmed(n, cli, coinbases.pop(0))

        loop = asyncio.get_event_loop()
        loop.run_until_complete(async_tests(loop))

    async def test_blockheight_confirmed(self, n, cli, unspent):
        # Just a unique anyone-can-spend scriptpubkey
        scriptpubkey = CScript([OP_TRUE, OP_DROP, OP_NOP])
        scripthash = script_to_scripthash(scriptpubkey)

        # There should exist any history for scripthash
        assert_equal(0, len(await cli.call(GET_HISTORY, scripthash)))

        # Send tx to scripthash and confirm it
        tx = create_transaction(unspent,
                n = 0, value = unspent.vout[0].nValue,
                sig = CScript([OP_TRUE]), out = scriptpubkey)
        pad_tx(tx)

        self.mine_blocks(n, 1, txns = [tx])
        sync_electrum_height(n)

        # History should now have 1 entry at current tip height
        res = await cli.call(GET_HISTORY, scripthash)
        assert_equal(1, len(res))
        assert_equal(n.getblockcount(), res[0]['height'])
        assert_equal(tx.hash, res[0]['tx_hash'])


if __name__ == '__main__':
    ElectrumScripthashGetHistory().main()
