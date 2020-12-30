#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Unlimited developers
"""
Tests various aspects of chained transactions, primarily `get_history` and
`get_mempool`.

`get_history` and `get_mempool` are basically the same RPC call, except that
get_mempool ignores confirmed transactions.
"""
from test_framework.electrumutil import (
        ElectrumTestFramework,
        ElectrumConnection,
        script_to_scripthash,
        sync_electrum_height,
        wait_for_electrum_mempool)
from test_framework.script import CScript, OP_TRUE, OP_DROP, OP_NOP, OP_FALSE
from test_framework.util import assert_equal
from test_framework.blocktools import create_transaction, pad_tx

import asyncio

GET_HISTORY = "blockchain.scripthash.get_history"
GET_MEMPOOL = "blockchain.scripthash.get_mempool"

class ElectrumMempoolChain(ElectrumTestFramework):

    def run_test(self):
        n = self.nodes[0]
        self.bootstrap_p2p()

        coinbases = self.mine_blocks(n, 100)

        async def async_tests(loop):
            cli = ElectrumConnection(loop)
            await cli.connect()
            await self.test_blockheight_unconfirmed(n, cli, coinbases.pop(0))
            await self.test_chain_to_from_one_scripthash(n, cli, coinbases.pop(0))

        loop = asyncio.get_event_loop()
        loop.run_until_complete(async_tests(loop))

    async def test_blockheight_unconfirmed(self, n, cli, unspent):
        """
        Check that unconfirmed transactions are correctly provided.

        Note that height is different for unconfirmed with confirmed parents
        and unconfirmed with unconfirmed parents.
        """
        # Another unique anyone-can-spend scriptpubkey
        scriptpubkey = CScript([OP_FALSE, OP_DROP, OP_NOP])
        scripthash = script_to_scripthash(scriptpubkey)

        # There should exist any history for scripthash
        assert_equal(0, len(await cli.call(GET_HISTORY, scripthash)))
        assert_equal(0, len(await cli.call(GET_MEMPOOL, scripthash)))

        # Send a chain of three txs. We expect that:
        # tx1: height 0,  signaling unconfirmed with confirmed parents
        # tx2: height -1, signaling unconfirmed with unconfirmed parents
        # tx3: height -1, signaling unconfirmed with unconfirmed parents
        tx1 = create_transaction(unspent,
                n = 0, value = unspent.vout[0].nValue,
                sig = CScript([OP_TRUE]), out = scriptpubkey)
        pad_tx(tx1)
        big_fee = 1
        tx2 = create_transaction(tx1,
                n = 0, value = unspent.vout[0].nValue - big_fee,
                sig = CScript([OP_TRUE]), out = scriptpubkey)
        pad_tx(tx2)
        tx3 = create_transaction(tx2,
                n = 0, value = unspent.vout[0].nValue - big_fee,
                sig = CScript([OP_TRUE]), out = scriptpubkey)
        pad_tx(tx3)

        self.p2p.send_txs_and_test([tx1, tx2, tx3], n)
        wait_for_electrum_mempool(n, count = 3)

        res = await cli.call(GET_HISTORY, scripthash)
        assert_equal(3, len(res))
        assert_equal(res, await cli.call(GET_MEMPOOL, scripthash))
        def get_tx(txhash):
            for tx in res:
                if tx['tx_hash'] == txhash:
                    return tx
            assert(not "tx not in result")

        assert_equal(0, get_tx(tx1.hash)['height'])
        assert_equal(-1, get_tx(tx2.hash)['height'])
        assert_equal(-1, get_tx(tx3.hash)['height'])

        # Confirm tx1, see that
        # tx1: gets tipheight
        # tx2: gets height 0, tx3 keeps height -1
        self.mine_blocks(n, 1, [tx1])
        sync_electrum_height(n)
        for call in [GET_HISTORY, GET_MEMPOOL]:
            res = await cli.call(call, scripthash)

            if call == GET_HISTORY:
                assert_equal(n.getblockcount(), get_tx(tx1.hash)['height'])
            else:
                assert(tx['tx_hash'] != tx1.hash for tx in res)
            assert_equal(0, get_tx(tx2.hash)['height'])
            assert_equal(-1, get_tx(tx3.hash)['height'])

        # cleanup mempool for next test
        self.mine_blocks(n, 1, [tx2, tx3])
        sync_electrum_height(n)
        assert(len(n.getrawmempool()) == 0)

    async def test_chain_to_from_one_scripthash(self, n, cli, unspent):
        """
        Creates a tx chain where the same scripthash is both funder and spender
        """
        scriptpubkey = CScript([OP_TRUE, OP_TRUE, OP_DROP, OP_DROP])
        scripthash = script_to_scripthash(scriptpubkey)
        assert_equal(0, len(await cli.call(GET_HISTORY, scripthash)))
        assert_equal(0, len(await cli.call(GET_MEMPOOL, scripthash)))

        def has_tx(res, txhash):
            for tx in res:
                if tx['tx_hash'] == txhash:
                    return True
            return False

        CHAIN_LENGTH = 25
        NUM_OUTPUTS = 1
        tx_chain = self._create_tx_chain(unspent, scriptpubkey,
            CHAIN_LENGTH, NUM_OUTPUTS)

        # Check mempool
        assert(len(n.getrawmempool()) == 0)
        self.p2p.send_txs_and_test(tx_chain, n)
        wait_for_electrum_mempool(n, count = len(tx_chain), timeout = 20)

        res = await cli.call(GET_HISTORY, scripthash)
        assert_equal(len(tx_chain), len(res))
        assert(all([ has_tx(res, tx.hash) for tx in tx_chain ]))

        res_mempool = await cli.call(GET_MEMPOOL, scripthash)
        assert_equal(res, res_mempool)

        # Check when confirmed in a block
        self.mine_blocks(n, 1, tx_chain)
        sync_electrum_height(n)
        res = await cli.call(GET_HISTORY, scripthash)
        assert_equal(len(tx_chain), len(res))
        assert(all([ has_tx(res, tx.hash) for tx in tx_chain ]))
        assert_equal(0, len(await cli.call(GET_MEMPOOL, scripthash)))

    def _create_tx_chain(self, unspent, scriptpubkey, chain_len, num_outputs):
        assert(num_outputs > 0)

        tx_chain = [unspent]
        for i in range(chain_len):
            prev_tx = tx_chain[-1]
            prev_n = i % num_outputs

            amount = prev_tx.vout[prev_n].nValue // num_outputs

            tx = create_transaction(prev_tx,
                    n = prev_n, value = [amount] * num_outputs,
                    sig = CScript([OP_TRUE]), out = scriptpubkey)
            pad_tx(tx)
            tx_chain.append(tx)

        tx_chain.pop(0) # the initial unspent is not part of the chain
        return tx_chain


if __name__ == '__main__':
    ElectrumMempoolChain().main()
