#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
This test checks activation of may152018 opcodes
"""
import test_framework.loginit
from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import satoshi_round, assert_equal, assert_raises_rpc_error
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
from test_framework.script import *

# far into the future
MAY152018_START_TIME = 1526400000

# Error due to invalid opcodes
DISABLED_OPCODE_ERROR = b'non-mandatory-script-verify-flag (Attempted to use a disabled opcode)'
RPC_DISABLED_OPCODE_ERROR = "64: " + \
    DISABLED_OPCODE_ERROR.decode("utf-8")


class PreviousSpendableOutput():

    def __init__(self, tx=CTransaction(), n=-1):
        self.tx = tx
        self.n = n  # the output we're spending


class May152018ActivationTest(ComparisonTestFramework):

    def __init__(self):
        self.num_nodes = 1

    def set_test_params(self):
        self.setup_clean_chain = True
        self.extra_args = [['-whitelist=127.0.0.1']]

    def create_and_tx(self, count):
        node = self.nodes[0]
        utxos = node.listunspent()
        assert(len(utxos) > 0)
        utxo = utxos[0]
        tx = CTransaction()
        value = int(satoshi_round(
            utxo["amount"] - self.relayfee) * COIN) // count
        tx.vin = [CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]))]
        tx.vout = []
        for _ in range(count):
            tx.vout.append(CTxOut(value, CScript([OP_1, OP_1, OP_AND])))
        tx_signed = node.signrawtransaction(ToHex(tx),None,None,"ALL|FORKID")["hex"]
        return tx_signed

    def run_test(self):
        self.test = TestManager(self, self.options.tmpdir)
        self.test.add_all_connections(self.nodes)
        # Start up network handling in another thread
        NetworkThread().start()
        self.test.run()

    def get_tests(self):
        node = self.nodes[0]
        self.relayfee = self.nodes[0].getnetworkinfo()["relayfee"]

        # First, we generate some coins to spend.
        node.setmocktime(MAY152018_START_TIME - 1000)
        node.generate(125)

        # Create various outputs using the OP_AND to check for activation.
        tx_hex = self.create_and_tx(25)
        txid = node.sendrawtransaction(tx_hex)
        assert(txid in set(node.getrawmempool()))

        node.generate(1)
        assert(txid not in set(node.getrawmempool()))

        # register the spendable outputs.
        tx = FromHex(CTransaction(), tx_hex)
        tx.rehash()
        spendable_ands = [PreviousSpendableOutput(
            tx, i) for i in range(len(tx.vout))]

        def spend_and():
            outpoint = spendable_ands.pop()
            out = outpoint.tx.vout[outpoint.n]
            value = int(out.nValue - (self.relayfee * COIN))
            tx = CTransaction()
            tx.vin = [CTxIn(COutPoint(outpoint.tx.sha256, outpoint.n))]
            tx.vout = [CTxOut(value, CScript([]))]
            tx.rehash()
            return tx

        # Check that large opreturn are not accepted yet.
        logging.info("Try to use the may152018 opcodes before activation")

        tx0 = spend_and()
        tx0_hex = ToHex(tx0)
        assert_raises_rpc_error(-26, RPC_DISABLED_OPCODE_ERROR,
                                node.sendrawtransaction, tx0_hex)

        # Push MTP forward just before activation.
        logging.info("Pushing MTP just before the activation and check again")
        node.setmocktime(MAY152018_START_TIME)

        # returns a test case that asserts that the current tip was accepted
        def accepted(tip):
            return TestInstance([[tip, True]])

        # returns a test case that asserts that the current tip was rejected
        def rejected(tip, reject=None):
            if reject is None:
                return TestInstance([[tip, False]])
            else:
                return TestInstance([[tip, reject]])

        def next_block(block_time):
            # get block height
            blockchaininfo = node.getblockchaininfo()
            height = int(blockchaininfo['blocks'])

            # create the block
            coinbase = create_coinbase(height)
            coinbase.rehash()
            block = create_block(
                int(node.getbestblockhash(), 16), coinbase, block_time)

            # Do PoW, which is cheap on regnet
            block.solve()
            return block

        for i in range(6):
            b = next_block(MAY152018_START_TIME + i - 1)
            yield accepted(b)

        # Check again just before the activation time
        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     MAY152018_START_TIME - 1)
        assert_raises_rpc_error(-26, RPC_DISABLED_OPCODE_ERROR,
                                node.sendrawtransaction, tx0_hex)

        def add_tx(block, tx):
            block.vtx.append(tx)
            block.hashMerkleRoot = block.calc_merkle_root()
            block.solve()

        b = next_block(MAY152018_START_TIME + 6)
        add_tx(b, tx0)
        # In this next step we don't check the reason code for the expected failure because of timing we
        # can not be sure whether checking the block will fail on signature validation or validation during
        # checkinputs. We can only be certain that we must have a failure.
        yield rejected(b)

        logging.info("Activates the new opcodes")
        fork_block = next_block(MAY152018_START_TIME + 6)
        yield accepted(fork_block)

        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     MAY152018_START_TIME)

        tx_hex = self.create_and_tx(25)
        tx0id = node.sendrawtransaction(tx_hex)

        assert(tx0id in set(node.getrawmempool()))
        # Transactions can also be included in blocks.
        may152018block = next_block(MAY152018_START_TIME + 7)
        tx0 = FromHex(CTransaction(), tx_hex)
        tx0.rehash()
        add_tx(may152018block, tx0)
        yield accepted(may152018block)

        logging.info("Cause a reorg that deactivate the may152018 opcodes")

        # Invalidate the may152018 block, ensure tx0 gets back to the mempool.
        assert(tx0id not in set(node.getrawmempool()))

        node.invalidateblock(format(may152018block.sha256, 'x'))
        assert(tx0id in set(node.getrawmempool()))

        node.invalidateblock(format(fork_block.sha256, 'x'))
        assert(tx0id not in set(node.getrawmempool()))


if __name__ == '__main__':
    May152018ActivationTest().main()
