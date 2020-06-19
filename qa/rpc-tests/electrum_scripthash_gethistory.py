#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Unlimited developers
"""
Tests the electrum call 'blockchain.scripthash.get_history'
"""
import asyncio
from test_framework.util import assert_equal, p2p_port
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging
from test_framework.electrumutil import *
from test_framework.nodemessages import COIN
from test_framework.blocktools import create_coinbase, create_block, \
    create_transaction, pad_tx
from test_framework.mininode import (
    P2PDataStore,
    NodeConn,
    NetworkThread,
)
from test_framework.script import CScript, OP_TRUE, OP_DROP, OP_NOP
import time

GET_HISTORY = "blockchain.scripthash.get_history"

class ElectrumScripthashGetHistory(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [bitcoind_electrum_args()]


    def bootstrap_p2p(self):
        """Add a P2P connection to the node.

        Helper to connect and wait for version handshake."""
        self.p2p = P2PDataStore()
        self.connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], self.p2p)
        self.p2p.add_connection(self.connection)
        NetworkThread().start()
        self.p2p.wait_for_verack()
        assert(self.p2p.connection.state == "connected")

    def mine_blocks(self, n, num_blocks, txns = None):
        prev = n.getblockheader(n.getbestblockhash())
        prev_height = prev['height']
        prev_hash = prev['hash']
        prev_time = max(prev['time'] + 1, int(time.time()))
        blocks = [ ]
        for i in range(num_blocks):
            coinbase = create_coinbase(prev_height + 1)
            b = create_block(
                    hashprev = prev_hash,
                    coinbase = coinbase,
                    txns = txns,
                    nTime = prev_time + 1)
            txns = None
            b.solve()
            blocks.append(b)

            prev_time = b.nTime
            prev_height += 1
            prev_hash = b.hash

        self.p2p.send_blocks_and_test(blocks, n)
        assert_equal(blocks[-1].hash, n.getbestblockhash())

        # Return coinbases for spending later
        return [b.vtx[0] for b in blocks]


    def run_test(self):
        n = self.nodes[0]
        self.bootstrap_p2p()

        coinbases = self.mine_blocks(n, 100)

        async def async_tests():
            cli = ElectrumConnection()
            await cli.connect()
            await self.test_blockheight_confirmed(n, cli, coinbases.pop(0))
            await self.test_blockheight_unconfirmed(n, cli, coinbases.pop(0))
            await self.test_chain_tofrom_one_scripthash(n, cli, coinbases.pop(0))
        loop = asyncio.get_event_loop()
        loop.run_until_complete(async_tests())

    def create_tx_chain(self, unspent, scriptpubkey, chain_len, num_outputs):
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

    async def test_chain_tofrom_one_scripthash(self, n, cli, unspent):
        """
        Creates a tx chain where the same scripthash is both funder and spender
        """
        scriptpubkey = CScript([OP_TRUE, OP_TRUE, OP_DROP, OP_DROP])
        scripthash = script_to_scripthash(scriptpubkey)
        assert_equal(0, len(await cli.call(GET_HISTORY, scripthash)))

        def has_tx(res, txhash):
            for tx in res:
                if tx['tx_hash'] == txhash:
                    return True
            return False

        CHAIN_LENGTH = 25
        NUM_OUTPUTS = 1
        tx_chain = self.create_tx_chain(unspent, scriptpubkey,
            CHAIN_LENGTH, NUM_OUTPUTS)

        # Check mempool
        assert(len(n.getrawmempool()) == 0)
        self.p2p.send_txs_and_test(tx_chain, n)
        wait_for_electrum_mempool(n, count = len(tx_chain),
            timeout = 20)

        res = await cli.call(GET_HISTORY, scripthash)
        assert_equal(len(tx_chain), len(res))
        assert(all([ has_tx(res, tx.hash) for tx in tx_chain ]))

        # Check when confirmed in a block
        self.mine_blocks(n, 1, tx_chain)
        sync_electrum_height(n)
        res = await cli.call(GET_HISTORY, scripthash)
        assert_equal(len(tx_chain), len(res))
        assert(all([ has_tx(res, tx.hash) for tx in tx_chain ]))

    async def test_blockheight_confirmed(self, n, cli, unspent):
        # Just unique anyone-can-spend scriptpubkey
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

    async def test_blockheight_unconfirmed(self, n, cli, unspent):
        # Another unique anyone-can-spend scriptpubkey
        scriptpubkey = CScript([OP_FALSE, OP_DROP, OP_NOP])
        scripthash = script_to_scripthash(scriptpubkey)

        # There should exist any history for scripthash
        assert_equal(0, len(await cli.call(GET_HISTORY, scripthash)))

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
        res = await cli.call(GET_HISTORY, scripthash)

        assert_equal(n.getblockcount(), get_tx(tx1.hash)['height'])
        assert_equal(0, get_tx(tx2.hash)['height'])
        assert_equal(-1, get_tx(tx3.hash)['height'])

        # cleanup mempool for next test
        self.mine_blocks(n, 1, [tx2, tx3])
        sync_electrum_height(n)
        assert(len(n.getrawmempool()) == 0)

if __name__ == '__main__':
    ElectrumScripthashGetHistory().main()
