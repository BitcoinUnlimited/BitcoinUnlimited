#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
This test checks the activation logic of OP_REVERSEBYTES.
Derived from both abc-schnorrmultisig-activation.py (see https://reviews.bitcoinabc.org/D3736) and
abc-schnorrmultisig.py
"""

import time

from test_framework.blocktools import (
    create_block,
    create_coinbase,
    make_conform_to_ctor,
    create_tx_with_script,
    pad_tx,
)
from test_framework.key import CECKey

from test_framework.nodemessages import (
    CBlock,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    FromHex,
    ToHex,
)
from test_framework.mininode import (
    P2PDataStore,
    NodeConn,
    NetworkThread,
)
from test_framework import schnorr
from test_framework.script import (
    CScript,
    OP_EQUAL,
    OP_REVERSEBYTES,
    OP_RETURN,
    OP_TRUE,
    SIGHASH_ALL,
    SIGHASH_FORKID,
    SignatureHashForkId,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, p2p_port, waitFor
import logging

# Blocks with invalid scripts give this error:
BAD_INPUTS_ERROR = 'bad-blk-signatures'

# Pre-upgrade, we get a BAD_OPCODE error
PRE_UPGRADE_BAD_OPCODE_ERROR = 'upgrade-conditional-script-failure (Opcode missing or not understood)'

class P2PNode(P2PDataStore):
    pass

class OpReversebytesActivationTest(BitcoinTestFramework):
    def __init__(self):
        super().__init__()
        self.set_test_params()

    def set_test_params(self):
        self.num_nodes = 1
        self.block_heights = {}
        self.extra_args = [["-debug=mempoolrej, mempool",]]

    def bootstrap_p2p(self, *, num_connections=1):
        """Add a P2P connection to the node.

        Helper to connect and wait for version handshake."""
        self.p2p = P2PNode()
        self.connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], self.p2p)
        self.p2p.add_connection(self.connection)
        NetworkThread().start()
        self.p2p.wait_for_verack()
        assert(self.p2p.connection.state == "connected")

    def get_best_block(self, node):
        """Get the best block. Register its height so we can use build_block."""
        block_height = node.getblockcount()
        blockhash = node.getblockhash(block_height)
        block = FromHex(CBlock(), node.getblock(blockhash, 0))
        block.calc_sha256()
        self.block_heights[block.sha256] = block_height
        return block

    def build_block(self, parent, transactions=(), n_time=None):
        """Make a new block with an OP_1 coinbase output.

        Requires parent to have its height registered."""
        parent.calc_sha256()
        block_height = self.block_heights[parent.sha256] + 1
        block_time = (parent.nTime + 1) if n_time is None else n_time

        # the script in create_coinbase differs for BU and ABC
        # you need to let coinbase script be CScript([OP_TRUE])
        block = create_block(
            parent.sha256, create_coinbase(block_height, scriptPubKey = CScript([OP_TRUE])), block_time)
        block.vtx.extend(transactions)
        make_conform_to_ctor(block)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        self.block_heights[block.sha256] = block_height
        return block

    def check_for_no_ban_on_rejected_tx(self, tx, reject_reason):
        """Check we are not disconnected when sending a txn that the node rejects."""
        self.p2p.send_txs_and_test(
            [tx], self.nodes[0], success=False, reject_reason=reject_reason)

    def check_for_ban_on_rejected_tx(self, tx, reject_reason=None):
        """Check we are disconnected when sending a txn that the node rejects.

        (Can't actually get banned, since bitcoind won't ban local peers.)"""
        self.p2p.send_txs_and_test(
            [tx], self.nodes[0], success=False, expect_disconnect=True, reject_reason=reject_reason)

    def check_for_ban_on_rejected_block(self, block, reject_reason=None):
        """Check we are disconnected when sending a block that the node rejects.

        (Can't actually get banned, since bitcoind won't ban local peers.)"""
        self.p2p.send_blocks_and_test([block], self.nodes[0], success=False,
                                               reject_reason=reject_reason, expect_ban=True)

    def run_test(self):
        logging.info("Initializing test directory "+self.options.tmpdir)
        node = self.nodes[0]

        self.bootstrap_p2p()

        tip = self.get_best_block(node)

        logging.info("Create some blocks with OP_1 coinbase for spending.")
        blocks = []
        for _ in range(10):
            tip = self.build_block(tip)
            blocks.append(tip)
        self.p2p.send_blocks_and_test(blocks, node, success=True)
        spendable_outputs = [block.vtx[0] for block in blocks]

        logging.info("Mature the blocks and get out of IBD.")
        node.generate(100)

        tip = self.get_best_block(node)

        logging.info(
            "Set up spending transactions to test and mine the funding transactions.")

        # Generate a key pair
        privkeybytes = b"xyzxyzhh" * 4
        private_key = CECKey()
        private_key.set_secretbytes(privkeybytes)
        # get uncompressed public key serialization
        public_key = private_key.get_pubkey()

        # Create funding/spending transaction pair
        spend_from = spendable_outputs.pop()
        value = spend_from.vout[0].nValue

        # Reversed data
        data = bytes.fromhex('0123456789abcdef')
        rev_data = bytes(reversed(data))

        # Lockscript: provide a bytestring that reverses to X
        script = CScript([OP_REVERSEBYTES, rev_data, OP_EQUAL])

        # Fund transaction: REVERSEBYTES <reversed(x)> EQUAL
        tx_reversebytes_fund = create_tx_with_script(spend_from, 0, b'', value, script)
        tx_reversebytes_fund.rehash()

        # Spend transaction: <x>
        tx_reversebytes_spend = CTransaction()
        tx_reversebytes_spend.vout.append(CTxOut(value - 1000, CScript([b'x' * 100, OP_RETURN])))
        tx_reversebytes_spend.vin.append(CTxIn(COutPoint(tx_reversebytes_fund.sha256, 0), b''))
        tx_reversebytes_spend.vin[0].scriptSig = CScript([data])
        tx_reversebytes_spend.rehash()

        # Mine funding transaction into block. Pre-upgrade output scripts can have
        # OP_REVERSEBYTES and still be fully valid, but they cannot spend it.
        tip = self.build_block(tip, [tx_reversebytes_fund])
        self.p2p.send_blocks_and_test([tip], node)

        logging.info(
            "Submitting a new OP_REVERSEBYTES tx via net, and mining it in a block")
        # Send OP_REVERSEBYTES tx
        self.p2p.send_txs_and_test([tx_reversebytes_spend], node)

        # Verify OP_REVERSEBYTES tx is in mempool
        waitFor(10, lambda: set(node.getrawmempool()) == {tx_reversebytes_spend.hash})

        # Mine OP_REVERSEBYTES tx into block
        tip = self.build_block(tip, [tx_reversebytes_spend])
        self.p2p.send_blocks_and_test([tip], node)


if __name__ == '__main__':
    OpReversebytesActivationTest().main()
