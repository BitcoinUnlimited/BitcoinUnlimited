#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
This test checks that blocks containing segwit recovery transactions will be accepted,
that segwit recovery transactions are rejected from mempool acceptance, but that are
accepted with -acceptnonstdtxn=1, and that segwit recovery transactions don't result in bans.
"""

import time

from test_framework.blocktools import (
    create_block,
    create_coinbase,
    hash160,
    make_conform_to_ctor,
)
from test_framework.nodemessages import (
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    ToHex,
)
from test_framework.mininode import (
    P2PDataStore,
    NodeConn,
    NetworkThread,
)
from test_framework.script import (
    CScript,
    OP_EQUAL,
    OP_HASH160,
    OP_TRUE,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    sync_blocks,
    p2p_port,
)

TEST_TIME = int(time.time())

# Error due to non clean stack
CLEANSTACK_ERROR = 'non-mandatory-script-verify-flag (P2SH script evaluation of script does not result in a clean stack)'
EVAL_FALSE_ERROR = 'non-mandatory-script-verify-flag (Script evaluated without error but finished with a false/empty top stack element)'


class P2PNode(P2PDataStore):
    pass


class PreviousSpendableOutput(object):

    def __init__(self, tx=CTransaction(), n=-1):
        self.tx = tx
        self.n = n


class SegwitRecoveryTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.set_test_params()

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.block_heights = {}
        self.tip = None
        self.blocks = {}
        # We have 2 nodes:
        # 1) node_nonstd (nodes[0]) accepts non-standard txns. It does
        #    accept Segwit recovery transactions
        # 2) node_std (nodes[1]) doesn't accept non-standard txns and
        #    doesn't have us whitelisted. It's used to test for bans, as we
        #    connect directly to it via mininode and send a segwit spending
        #    txn. This transaction is non-standard. We check that sending
        #    this transaction doesn't result in a ban.
        # Nodes are connected to each other, so node_std receives blocks and
        # transactions that node_nonstd has accepted. Since we are checking
        # that segwit spending txn are not resulting in bans, node_nonstd
        # doesn't get banned when forwarding this kind of transactions to
        # node_std.
        # NB debug categories need to be specified otherwise test will fail
        # because it will look for error message in debug.log
        self.extra_args = [['-acceptnonstdtxn=1',
                            '-whitelist=127.0.0.1'],
                           ['-acceptnonstdtxn=0',
                            '-debug=mempool']]

    def bootstrap_p2p(self):
        """Add a P2P connection to the node.

        Helper to connect and wait for version handshake."""
        self.p2p = P2PNode()
        self.connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], self.p2p)
        self.connection2 = NodeConn('127.0.0.1', p2p_port(1), self.nodes[1], self.p2p)
        self.p2p.add_connection(self.connection)
        self.p2p.add_connection(self.connection2)
        NetworkThread().start()
        self.p2p.wait_for_verack()
        assert(self.p2p.connection.state == "connected")

    def next_block(self, number):
        if self.tip is None:
            base_block_hash = self.genesis_hash
            block_time = TEST_TIME
        else:
            base_block_hash = self.tip.sha256
            block_time = self.tip.nTime + 1
        # First create the coinbase
        height = self.block_heights[base_block_hash] + 1
        coinbase = create_coinbase(height, scriptPubKey = CScript([OP_TRUE]))
        coinbase.rehash()
        block = create_block(base_block_hash, coinbase, block_time)

        # Do PoW, which is cheap on regnet
        block.solve()
        self.tip = block
        self.block_heights[block.sha256] = height
        assert number not in self.blocks
        self.blocks[number] = block
        return block

    def run_test(self):
        self.bootstrap_p2p()
        self.genesis_hash = int(self.nodes[0].getbestblockhash(), 16)
        self.block_heights[self.genesis_hash] = 0
        spendable_outputs = []

        # shorthand
        block = self.next_block
        node_nonstd = self.nodes[0]
        node_std = self.nodes[1]

        # save the current tip so it can be spent by a later block
        def save_spendable_output():
            spendable_outputs.append(self.tip)

        # get an output that we previously marked as spendable
        def get_spendable_output():
            return PreviousSpendableOutput(spendable_outputs.pop(0).vtx[0], 0)

        # submit current tip and check it was accepted
        def accepted(self, node):
            self.p2p.send_blocks_and_test([self.tip], node)

        # move the tip back to a previous block
        def tip(number):
            self.tip = self.blocks[number]

        # adds transactions to the block and updates state
        def update_block(block_number, new_transactions):
            block = self.blocks[block_number]
            block.vtx.extend(new_transactions)
            old_sha256 = block.sha256
            make_conform_to_ctor(block)
            block.hashMerkleRoot = block.calc_merkle_root()
            block.solve()
            # Update the internal state just like in next_block
            self.tip = block
            if block.sha256 != old_sha256:
                self.block_heights[
                    block.sha256] = self.block_heights[old_sha256]
                del self.block_heights[old_sha256]
            self.blocks[block_number] = block
            return block

        # checks the mempool has exactly the same txns as in the provided list
        def check_mempool_equal(node, txns):
            assert set(node.getrawmempool()) == set(tx.hash for tx in txns)

        # Returns 2 transactions:
        # 1) txfund: create outputs in segwit addresses
        # 2) txspend: spends outputs from segwit addresses
        def create_segwit_fund_and_spend_tx(spend, case0=False):
            if not case0:
                # Spending from a P2SH-P2WPKH coin,
                #   txhash:a45698363249312f8d3d93676aa714be59b0bd758e62fa054fb1ea6218480691
                redeem_script0 = bytearray.fromhex(
                    '0014fcf9969ce1c98a135ed293719721fb69f0b686cb')
                # Spending from a P2SH-P2WSH coin,
                #   txhash:6b536caf727ccd02c395a1d00b752098ec96e8ec46c96bee8582be6b5060fa2f
                redeem_script1 = bytearray.fromhex(
                    '0020fc8b08ed636cb23afcb425ff260b3abd03380a2333b54cfa5d51ac52d803baf4')
            else:
                redeem_script0 = bytearray.fromhex('51020000')
                redeem_script1 = bytearray.fromhex('53020080')
            redeem_scripts = [redeem_script0, redeem_script1]

            # Fund transaction to segwit addresses
            txfund = CTransaction()
            txfund.vin = [CTxIn(COutPoint(spend.tx.sha256, spend.n))]
            amount = (50 * COIN - 1000) // len(redeem_scripts)
            for redeem_script in redeem_scripts:
                txfund.vout.append(
                    CTxOut(amount, CScript([OP_HASH160, hash160(redeem_script), OP_EQUAL])))
            txfund.rehash()

            # Segwit spending transaction
            # We'll test if a node that checks for standardness accepts this
            # txn. It should fail exclusively because of the restriction in
            # the scriptSig (non clean stack..), so all other characteristcs
            # must pass standardness checks. For this reason, we create
            # standard P2SH outputs.
            txspend = CTransaction()
            for i in range(len(redeem_scripts)):
                txspend.vin.append(
                    CTxIn(COutPoint(txfund.sha256, i), CScript([redeem_scripts[i]])))
            txspend.vout = [CTxOut(50 * COIN - 2000,
                                   CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]))]
            txspend.rehash()

            return txfund, txspend

        # Check we are not banned when sending a txn that is rejected.
        def check_for_no_ban_on_rejected_tx(self, node, tx, reject_reason):
            self.p2p.send_txs_and_test(
                [tx], node, success=False, reject_reason=reject_reason)
        def check_for_accepted_tx(self, node, tx):
            self.p2p.send_txs_and_test([tx], node, success=True)

        # Create a new block
        block(0)
        save_spendable_output()
        accepted(self, node_nonstd)

        # Now we need that block to mature so we can spend the coinbase.
        matureblocks = []
        for i in range(199):
            block(5000 + i)
            matureblocks.append(self.tip)
            save_spendable_output()
        self.p2p.send_blocks_and_test(matureblocks, node_nonstd)

        # collect spendable outputs now to avoid cluttering the code later on
        out = []
        for i in range(100):
            out.append(get_spendable_output())

        # Create segwit funding and spending transactions
        txfund, txspend = create_segwit_fund_and_spend_tx(out[0])
        txfund_case0, txspend_case0 = create_segwit_fund_and_spend_tx(
            out[1], True)

        # Mine txfund, as it can't go into node_std mempool because it's
        # nonstandard.
        block(5555)
        update_block(5555, [txfund, txfund_case0])
        # check that current tip is accepted by node0 (nonstd)
        accepted(self, node_nonstd)

        # Check both nodes are synchronized before continuing.
        sync_blocks(self.nodes)

        # Check that upgraded nodes checking for standardness are not banning
        # nodes sending segwit spending txns.
        check_for_no_ban_on_rejected_tx(
            self, node_std, txspend, CLEANSTACK_ERROR)
        check_for_no_ban_on_rejected_tx(
            self, node_std, txspend_case0, EVAL_FALSE_ERROR)

        txspend_id = node_nonstd.decoderawtransaction(ToHex(txspend))["txid"]
        txspend_case0_id = node_nonstd.decoderawtransaction(ToHex(txspend_case0))["txid"]

        # Segwit recovery txns are accept from node that accept not standard txs
        assert_equal(node_nonstd.sendrawtransaction(ToHex(txspend)), txspend_id)
        assert_equal(node_nonstd.sendrawtransaction(ToHex(txspend_case0)), txspend_case0_id)

        # Segwit recovery txs are reject if node does not accept stansard txs
        assert_raises_rpc_error(-26, CLEANSTACK_ERROR,
                                node_std.sendrawtransaction, ToHex(txspend))
        assert_raises_rpc_error(-26, EVAL_FALSE_ERROR,
                                node_std.sendrawtransaction, ToHex(txspend_case0))

        # Blocks containing segwit spending txns are accepted in both nodes.
        self.next_block(5)
        update_block(5, [txspend, txspend_case0])
        accepted(self, node_nonstd)
        sync_blocks(self.nodes)


if __name__ == '__main__':
    SegwitRecoveryTest().main()

def Test():
    from test_framework.util import standardFlags
    t = SegwitRecoveryTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }
    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
