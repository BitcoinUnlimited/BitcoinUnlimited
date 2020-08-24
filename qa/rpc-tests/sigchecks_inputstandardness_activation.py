#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Developers
# Copyright (c) 2020 Bitcoin Unlimited
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test activation of per-input sigchecks limit standardness rule
"""
import logging
logging.getLogger().setLevel(logging.INFO)

from test_framework.blocktools import (
    create_block,
    create_coinbase,
    make_conform_to_ctor,
)
from test_framework.nodemessages import (
    CBlock,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    FromHex,
    ToHex,
)
from test_framework.mininode import P2PDataStore
from test_framework.script import (
    CScript,
    OP_CHECKDATASIG,
    OP_CHECKDATASIGVERIFY,
    OP_3DUP,
    OP_RETURN,
    OP_TRUE,
)

from test_framework.mininode import (
    P2PDataStore,
    NodeConn,
    NetworkThread,
)

from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import pad_tx
from test_framework.util import assert_equal, assert_raises_rpc_error, p2p_port, waitFor
from collections import deque

# The upgrade activation time, which we artificially set far into the future.
MAY2020_START_TIME = 2000000000

TX_INPUT_SIGCHECKS_ERROR = "non-mandatory-script-verify-flag (Validation resources exceeded (SigChecks))"


def create_transaction(spendfrom, custom_script, amount=None):
    # Fund and sign a transaction to a given output.
    # spendfrom should be a CTransaction with first output to OP_TRUE.

    # custom output will go on position 1, after position 0 which will be
    # OP_TRUE (so it can be reused).
    customout = CTxOut(0, bytes(custom_script))
    # set output amount to required dust if not given
    customout.nValue = amount or (len(customout.serialize()) + 148) * 3

    ctx = CTransaction()
    ctx.vin.append(CTxIn(COutPoint(spendfrom.sha256, 0), bytes([OP_TRUE])))
    ctx.vout.append(CTxOut(0, bytes([OP_TRUE])))
    ctx.vout.append(customout)
    pad_tx(ctx)

    fee = len(ctx.serialize())
    ctx.vout[0].nValue = spendfrom.vout[0].nValue - customout.nValue - fee
    ctx.rehash()

    return ctx




class InputSigChecksActivationTest(BitcoinTestFramework):
    def __init__(self):
        super().__init__()
        self.set_test_params()

    def check_for_no_ban_on_rejected_tx(self, node, tx, reject_reason=None):
        """Check we are not disconnected when sending a txn that the node rejects."""
        self.pynode.send_txs_and_test([tx], node, success=False, reject_reason=reject_reason)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.block_heights = {}
        self.extra_args = [[
            "-consensus.forkMay2020Time={}".format(MAY2020_START_TIME),
        ]]

    def getbestblock(self, node):
        """Get the best block. Register its height so we can use build_block."""
        block_height = node.getblockcount()
        blockhash = node.getblockhash(block_height)
        block = FromHex(CBlock(), node.getblock(blockhash, 0))
        block.calc_sha256()
        self.block_heights[block.sha256] = block_height
        return block

    def build_block(self, parent, transactions=(),
                    nTime=None, cbextrascript=None):
        """Make a new block with an OP_1 coinbase output.

        Requires parent to have its height registered."""
        parent.calc_sha256()
        block_height = self.block_heights[parent.sha256] + 1
        block_time = (parent.nTime + 1) if nTime is None else nTime

        block = create_block(
            parent.sha256, create_coinbase(block_height), block_time)
        if cbextrascript is not None:
            block.vtx[0].vout.append(CTxOut(0, cbextrascript))
            block.vtx[0].rehash()
        block.vtx.extend(transactions)
        make_conform_to_ctor(block)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        self.block_heights[block.sha256] = block_height
        return block

    def run_test(self):
        (node,) = self.nodes
        self.pynode = P2PDataStore()
        self.connection = NodeConn('127.0.0.1', p2p_port(0), node, self.pynode)
        self.pynode.add_connection(self.connection)
        NetworkThread().start()
        self.pynode.wait_for_verack()
        # Get out of IBD
        node.generate(1)

        tip = self.getbestblock(node)

        logging.info("Create some blocks with OP_1 coinbase for spending.")
        blocks = []
        for _ in range(20):
            tip = self.build_block(tip)
            blocks.append(tip)
        self.pynode.send_blocks_and_test(blocks, node, timeout=10)
        self.spendable_outputs = deque(block.vtx[0] for block in blocks)

        logging.info("Mature the blocks.")
        node.generate(100)

        tip = self.getbestblock(node)

        # To make compact and fast-to-verify transactions, we'll use
        # CHECKDATASIG over and over with the same data.
        # (Using the same stuff over and over again means we get to hit the
        # node's signature cache and don't need to make new signatures every
        # time.)
        cds_message = b''
        # r=1 and s=1 ecdsa, the minimum values.
        cds_signature = bytes.fromhex('3006020101020101')
        # Recovered pubkey
        cds_pubkey = bytes.fromhex(
            '03089b476b570d66fad5a20ae6188ebbaf793a4c2a228c65f3d79ee8111d56c932')

        fundings = []

        def make_spend(scriptpubkey, scriptsig):
            # Add a funding tx to fundings, and return a tx spending that using
            # scriptsig.
            logging.debug(
                "Gen tx with locking script {} unlocking script {} .".format(
                    scriptpubkey.hex(), scriptsig.hex()))

            # get funds locked with OP_1
            sourcetx = self.spendable_outputs.popleft()
            # make funding that forwards to scriptpubkey
            fundtx = create_transaction(sourcetx, scriptpubkey)
            fundings.append(fundtx)

            # make the spending
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(fundtx.sha256, 1), scriptsig))
            tx.vout.append(CTxOut(0, CScript([OP_RETURN])))
            pad_tx(tx)
            tx.rehash()
            return tx

        logging.info("Generating txes used in this test")

        # "Good" txns that pass our rule:

        goodtxes = [
            # most dense allowed input -- 2 sigchecks with a 26-byte scriptsig.
            make_spend(CScript([cds_message,
                                cds_pubkey,
                                OP_3DUP,
                                OP_CHECKDATASIGVERIFY,
                                OP_CHECKDATASIGVERIFY]),
                       CScript([b'x' * 16,
                                cds_signature])),

            # 4 sigchecks with a 112-byte scriptsig, just at the limit for this
            # sigchecks count.
            make_spend(CScript([cds_message,
                                cds_pubkey,
                                OP_3DUP,
                                OP_CHECKDATASIGVERIFY,
                                OP_3DUP,
                                OP_CHECKDATASIGVERIFY,
                                OP_3DUP,
                                OP_CHECKDATASIGVERIFY,
                                OP_CHECKDATASIGVERIFY]),
                       CScript([b'x' * 101,
                                cds_signature])),

            # "nice" transaction - 1 sigcheck with 9-byte scriptsig.
            make_spend(CScript([cds_message, cds_pubkey, OP_CHECKDATASIG]), CScript(
                [cds_signature])),

            # 1 sigcheck with 0-byte scriptsig.
            make_spend(CScript([cds_signature, cds_message,
                                cds_pubkey, OP_CHECKDATASIG]), CScript([])),
        ]

        badtxes = [
            # "Bad" txns:
            # 2 sigchecks with a 25-byte scriptsig, just 1 byte too short.
            make_spend(CScript([cds_message,
                                cds_pubkey,
                                OP_3DUP,
                                OP_CHECKDATASIGVERIFY,
                                OP_CHECKDATASIGVERIFY]),
                       CScript([b'x' * 15,
                                cds_signature])),

            # 4 sigchecks with a 111-byte scriptsig, just 1 byte too short.
            make_spend(CScript([cds_message,
                                cds_pubkey,
                                OP_3DUP,
                                OP_CHECKDATASIGVERIFY,
                                OP_3DUP,
                                OP_CHECKDATASIGVERIFY,
                                OP_3DUP,
                                OP_CHECKDATASIGVERIFY,
                                OP_CHECKDATASIGVERIFY]),
                       CScript([b'x' * 100,
                                cds_signature])),
        ]

        goodtxids = set(t.hash for t in goodtxes)
        badtxids = set(t.hash for t in badtxes)

        logging.info("Funding the txes")
        tip = self.build_block(tip, fundings)
        self.pynode.send_blocks_and_test([tip], node, timeout=10)

        # Activation tests

        logging.info("Approach to just before upgrade activation")
        # Move our clock to the uprade time so we will accept such
        # future-timestamped blocks.
        node.setmocktime(MAY2020_START_TIME + 10)
        # Mine six blocks with timestamp starting at
        # SIGCHECKS_ACTIVATION_TIME-1
        blocks = []
        for i in range(-1, 5):
            tip = self.build_block(tip, nTime=MAY2020_START_TIME + i)
            blocks.append(tip)
        self.pynode.send_blocks_and_test(blocks, node, timeout=10)
        assert_equal(node.getblockchaininfo()['mediantime'], MAY2020_START_TIME - 1)

        logging.info(
            "The next block will activate, but the activation block itself must follow old rules")

        logging.info("Send all the transactions just before upgrade")

        self.pynode.send_txs_and_test(goodtxes, node)
        self.pynode.send_txs_and_test(badtxes, node)

        assert_equal(set(node.getrawmempool()), goodtxids | badtxids)

        # ask the node to mine a block, it should include the bad txes.
        [blockhash] = node.generate(1)
        assert_equal(set(node.getblock(blockhash, 1)[
                     'tx'][1:]), goodtxids | badtxids)
        assert_equal(node.getrawmempool(), [])

        # discard that block
        node.invalidateblock(blockhash)
        waitFor(30, lambda: set(node.getrawmempool()) == goodtxids | badtxids)

        logging.info("Mine the activation block itself")
        tip = self.build_block(tip)
        self.pynode.send_blocks_and_test([tip], node, timeout=10)

        logging.info("We have activated!")
        assert_equal(node.getblockchaininfo()['mediantime'], MAY2020_START_TIME)

        logging.info("The high-sigchecks transactions got evicted but the good ones are still around")
        waitFor(20, lambda: True if set(node.getrawmempool()) == goodtxids else logging.info(node.getrawmempool()))

        logging.info("Now the high-sigchecks transactions are rejected from mempool.")
        # try sending some of the bad txes again after the upgrade
        for tx in badtxes:
            self.check_for_no_ban_on_rejected_tx(node, tx, None)  # No reject reason because we don't log on rejection
            assert_raises_rpc_error(-26, TX_INPUT_SIGCHECKS_ERROR, node.sendrawtransaction, ToHex(tx))

        logging.info("But they can still be mined!")

        # Now make a block with all the txes, they still are accepted in blocks!
        tip = self.build_block(tip, goodtxes + badtxes)
        self.pynode.send_blocks_and_test([tip], node, timeout=10)

        assert_equal(node.getbestblockhash(), tip.hash)


if __name__ == '__main__':
    InputSigChecksActivationTest().main()


# Create a convenient function for an interactive python debugging session
def Test():
    from test_framework.util import standardFlags
    t = InputSigChecksActivationTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
