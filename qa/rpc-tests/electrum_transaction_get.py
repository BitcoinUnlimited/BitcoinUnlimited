#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Unlimited developers
"""
Tests the electrum call 'blockchain.transaction.get'
"""
import asyncio
from test_framework.util import assert_equal, p2p_port
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging
from test_framework.electrumutil import *
from test_framework.nodemessages import COIN, ToHex
from test_framework.blocktools import create_coinbase, create_block, \
    create_transaction
from test_framework.mininode import (
    P2PDataStore,
    NodeConn,
    NetworkThread,
)
from test_framework.script import CScript, OP_TRUE, OP_DROP, OP_NOP
import time

TX_GET = "blockchain.transaction.get"

class ElectrumTransactionGet(BitcoinTestFramework):

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
        print(prev)
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

        coinbases = self.mine_blocks(n, 5)

        async def async_tests():
            cli = ElectrumConnection()
            await cli.connect()

            # Test raw
            for tx in coinbases:
                assert_equal(ToHex(tx), await cli.call(TX_GET, tx.hash))

            # Test verbose.
            # The spec is unclear. It states:
            #
            # "whatever the coin daemon returns when asked for a
            #  verbose form of the raw transaction"
            #
            # Just check the basics.
            for tx in coinbases:
                electrum = await cli.call(TX_GET, tx.hash, True)
                bitcoind = n.getrawtransaction(tx.hash, True)
                assert_equal(bitcoind['txid'], electrum['txid'])
                assert_equal(bitcoind['locktime'], electrum['locktime'])
                assert_equal(bitcoind['size'], electrum['size'])
                assert_equal(bitcoind['hex'], electrum['hex'])
                assert_equal(len(bitcoind['vin']), len(bitcoind['vin']))
                assert_equal(len(bitcoind['vout']), len(bitcoind['vout']))

        loop = asyncio.get_event_loop()
        loop.run_until_complete(async_tests())
if __name__ == '__main__':
    ElectrumTransactionGet().main()
