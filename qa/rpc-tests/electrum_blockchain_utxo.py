#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Unlimited developers
import asyncio
from test_framework.script import CScript, OP_TRUE, OP_DROP, OP_NOP, OP_FALSE
from test_framework.util import assert_equal
from test_framework.blocktools import create_transaction, pad_tx
from test_framework.util import assert_equal
from test_framework.loginit import logging
from test_framework.electrumutil import (
        ERROR_CODE_INVALID_PARAMS,
        bitcoind_electrum_args,
        ElectrumConnection,
        ElectrumTestFramework,
        assert_response_error,
        script_to_scripthash,
)

GET_UTXO = "blockchain.utxo.get"
TX_BROADCAST = "blockchain.transaction.broadcast"

class ElectrumUtxoTests(ElectrumTestFramework):

    def run_test(self):
        self.bootstrap_p2p()

        coinbases = self.mine_blocks(self.nodes[0], 101)

        async def async_tests(loop):
            self.cli = ElectrumConnection(loop)
            await self.cli.connect()

            await self.test_invalid_txid(),
            await self.test_invalid_output(coinbases.pop(0))
            await self.test_two_tx_chain(coinbases.pop(0))

            self.cli.disconnect()

        loop = asyncio.get_event_loop()
        loop.run_until_complete(async_tests(loop))

    async def test_invalid_txid(self):
        txid = "0000000000000000000000000000000000000000000000000000000000000042"
        await assert_response_error(
                lambda: self.cli.call(GET_UTXO, txid, 0),
                ERROR_CODE_INVALID_PARAMS,
                "No such mempool transaction")

    async def test_invalid_output(self, unspent):
        await assert_response_error(
                lambda: self.cli.call(GET_UTXO, unspent.rehash(), 420),
                ERROR_CODE_INVALID_PARAMS,
                "out_n 420 does not exist on tx")

    async def test_two_tx_chain(self, unspent):
        res = await self.cli.call(GET_UTXO, unspent.rehash(), 0)
        assert_equal(res["status"], "unspent")

        original_amount = unspent.vout[0].nValue

        # Use custom locking script for testing scripthash later.
        scriptpubkey1 = CScript([OP_NOP])
        scriptpubkey2 = CScript([OP_TRUE, OP_DROP])

        # Create two transactions, where the second has the first one as parent.
        tx1 = create_transaction(unspent, n = 0,
                sig = CScript([OP_TRUE]),
                out = scriptpubkey1,
                value = original_amount - 1000)
        pad_tx(tx1)
        tx2 = create_transaction(tx1, n = 0,
                sig = CScript([OP_TRUE]),
                out = scriptpubkey2,
                value = original_amount - 2000)
        pad_tx(tx2)

        tx1_id = await self.cli.call(TX_BROADCAST, tx1.toHex())
        tx2_id = await self.cli.call(TX_BROADCAST, tx2.toHex())
        self.wait_for_mempool_count(count = 2)

        async def check_utxo(confirmation_height):
            tx1_utxo = await self.cli.call(GET_UTXO, tx1_id, 0)
            tx2_utxo = await self.cli.call(GET_UTXO, tx2_id, 0)

            assert_equal(tx1_utxo['status'], 'spent')
            assert_equal(tx1_utxo['amount'], original_amount - 1000)
            assert_equal(tx1_utxo['scripthash'], script_to_scripthash(scriptpubkey1))
            assert_equal(tx1_utxo['spent']['tx_hash'], tx2_id)
            assert_equal(tx1_utxo['spent']['tx_pos'], 0)
            if confirmation_height is not None:
                assert_equal(tx1_utxo['height'], confirmation_height)
                assert_equal(tx1_utxo['spent']['height'], confirmation_height)
            else:
                # 0 == in mempool
                assert_equal(tx1_utxo['height'], 0)
                assert_equal(tx1_utxo['spent']['height'], 0)

            assert_equal(tx2_utxo['status'], 'unspent')
            assert_equal(tx2_utxo['amount'], original_amount - 2000)
            assert_equal(tx2_utxo['scripthash'], script_to_scripthash(scriptpubkey2))
            assert_equal(tx2_utxo['spent']['height'], None)
            assert_equal(tx2_utxo['spent']['tx_hash'], None)
            assert_equal(tx2_utxo['spent']['tx_pos'], None)
            if confirmation_height is not None:
                assert_equal(tx2_utxo['height'], confirmation_height)
            else:
                assert_equal(tx2_utxo['height'], -1) # in mempool, with parent in mempool

        # Check result when unconfirmed
        await check_utxo(confirmation_height = None)

        # Check result when confirmed
        self.mine_blocks(self.nodes[0], 1, [tx1, tx2])
        self.wait_for_mempool_count(count = 0)
        await check_utxo(confirmation_height = self.nodes[0].getblockcount())

if __name__ == '__main__':
    ElectrumUtxoTests().main()
