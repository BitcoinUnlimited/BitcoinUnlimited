#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Unlimited developers
"""
Tests the electrum call 'blockchain.transaction.get'
"""
import asyncio
from test_framework.util import assert_equal, p2p_port
from test_framework.electrumutil import ElectrumTestFramework, ElectrumConnection
from test_framework.nodemessages import ToHex
from test_framework.blocktools import create_transaction, pad_tx
from test_framework.script import (
        CScript,
        OP_CHECKSIG,
        OP_DROP,
        OP_DUP,
        OP_EQUAL,
        OP_EQUALVERIFY,
        OP_FALSE,
        OP_HASH160,
        OP_TRUE,
)
from test_framework.nodemessages import COIN

TX_GET = "blockchain.transaction.get"
DUMMY_HASH = 0x1111111111111111111111111111111111111111

class ElectrumTransactionGet(ElectrumTestFramework):

    def run_test(self):
        n = self.nodes[0]
        self.bootstrap_p2p()

        coinbases = self.mine_blocks(n, 103)

        # non-coinbase transactions
        prevtx = coinbases[0]
        nonstandard_tx = create_transaction(
                prevtx = prevtx,
                value = prevtx.vout[0].nValue, n = 0,
                sig = CScript([OP_TRUE]),
                out = CScript([OP_FALSE, OP_DROP]))

        prevtx = coinbases[1]
        p2sh_tx = create_transaction(
                prevtx = prevtx,
                value = prevtx.vout[0].nValue, n = 0,
                sig = CScript([OP_TRUE]),
                out = CScript([OP_HASH160, DUMMY_HASH, OP_EQUAL]))

        prevtx = coinbases[2]
        p2pkh_tx = create_transaction(
                prevtx = prevtx,
                value = prevtx.vout[0].nValue, n = 0,
                sig = CScript([OP_TRUE]),
                out = CScript([OP_DUP, OP_HASH160, DUMMY_HASH, OP_EQUALVERIFY, OP_CHECKSIG]))

        for tx in [nonstandard_tx, p2sh_tx, p2pkh_tx]:
            pad_tx(tx)

        coinbases.extend(self.mine_blocks(n, 1, [nonstandard_tx, p2sh_tx, p2pkh_tx]))
        self.sync_height()


        async def async_tests(loop):
            cli = ElectrumConnection(loop)
            await cli.connect()

            return await asyncio.gather(
                self.test_verbose(n, cli, nonstandard_tx.hash, p2sh_tx.hash, p2pkh_tx.hash),
                self.test_non_verbose(cli, coinbases)
            )


        loop = asyncio.get_event_loop()
        loop.run_until_complete(async_tests(loop))

    async def test_non_verbose(self, cli, coinbases):
        for tx in coinbases:
            assert_equal(ToHex(tx), await cli.call(TX_GET, tx.hash))

    async def test_verbose(self, n, cli, nonstandard_tx, p2sh_tx, p2pkh_tx):
        """
        The spec is unclear. It states:

        "whatever the coin daemon returns when asked for a
         verbose form of the raw transaction"

        We should test for defacto "common denominators" between bitcoind
        implementations.
        """

        # All the transactions are confirmed in the tip
        block = n.getbestblockhash()
        coinbase_tx = n.getblock(block)['tx'][0]

        async def check_tx(txid, check_output_type = False):
            electrum = await cli.call(TX_GET, txid, True)
            bitcoind = n.getrawtransaction(txid, True, block)

            assert_equal(bitcoind['txid'], electrum['txid'])
            assert_equal(bitcoind['locktime'], electrum['locktime'])
            assert_equal(bitcoind['size'], electrum['size'])
            assert_equal(bitcoind['hex'], electrum['hex'])
            assert_equal(bitcoind['version'], electrum['version'])
            assert_equal(bitcoind['confirmations'], electrum['confirmations'])
            assert_equal(bitcoind['time'], electrum['time'])

            # inputs
            assert_equal(len(bitcoind['vin']), len(bitcoind['vin']))
            for i in range(len(bitcoind['vin'])):
                if 'coinbase' in bitcoind['vin'][i]:
                    # bitcoind drops txid and adds 'coinbase' for coinbase
                    # inputs
                    assert_equal(bitcoind['vin'][i]['coinbase'], electrum['vin'][i]['coinbase'])
                    assert_equal(bitcoind['vin'][i]['sequence'], electrum['vin'][i]['sequence'])
                    continue

                assert_equal(
                        bitcoind['vin'][i]['txid'],
                        electrum['vin'][i]['txid'])
                assert_equal(
                        bitcoind['vin'][i]['vout'],
                        electrum['vin'][i]['vout'])
                assert_equal(
                        bitcoind['vin'][i]['sequence'],
                        electrum['vin'][i]['sequence'])
                assert_equal(
                        bitcoind['vin'][i]['scriptSig']['hex'],
                        electrum['vin'][i]['scriptSig']['hex'])

                # There is more than one way to represent script as assembly.
                # For instance '51' can be represented as '1' or 'OP_PUSHNUM_1'.
                # Just check for existance.
                assert('asm' in electrum['vin'][i]['scriptSig'])


            # outputs
            assert_equal(len(bitcoind['vout']), len(bitcoind['vout']))
            for i in range(len(bitcoind['vout'])):
                assert_equal(
                        bitcoind['vout'][i]['n'],
                        electrum['vout'][i]['n'])

                assert_equal(
                        bitcoind['vout'][i]['value'],
                        electrum['vout'][i]['value_coin'])

                assert_equal(
                        bitcoind['vout'][i]['value'] * COIN,
                        electrum['vout'][i]['value_satoshi'])

                assert_equal(
                        bitcoind['vout'][i]['scriptPubKey']['hex'],
                        electrum['vout'][i]['scriptPubKey']['hex'])
                assert('asm' in electrum['vout'][i]['scriptPubKey'])

                if check_output_type:
                    assert_equal(
                        bitcoind['vout'][i]['scriptPubKey']['type'],
                        electrum['vout'][i]['scriptPubKey']['type'])

        await asyncio.gather(
                # ElectrsCash cannot tell if it's nonstandard
                check_tx(nonstandard_tx, check_output_type = False),
                check_tx(p2sh_tx),
                check_tx(p2pkh_tx),
                check_tx(coinbase_tx),
        )


if __name__ == '__main__':
    ElectrumTransactionGet().main()
