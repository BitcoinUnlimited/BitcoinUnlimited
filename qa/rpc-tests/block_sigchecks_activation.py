#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test activation of block sigchecks limits
"""
import pdb
import logging

# The minimum number of max_block_size bytes required per executed signature
# check operation in a block. I.e. maximum_block_sigchecks = maximum_block_size
# / BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO (network rule).
BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO = 141

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
from test_framework.util import assert_equal, p2p_port, waitFor
from collections import deque

# Set test to run with sigops deactivation far in the future.
MAY2020_START_TIME = SIGCHECKS_ACTIVATION_TIME = 2000000000

# We are going to use a tiny block size so we don't need to waste too much
# time with making transactions. (note -- minimum block size is 1000000)
# (just below a multiple, to test edge case)
MAXBLOCKSIZE = 8000 * BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO - 1
assert MAXBLOCKSIZE == 1127999

MAX_BLOCK_SIGCHECKS = 7999

# Blocks with too many sigchecks from cache give this error in log file:
# BLOCK_SIGCHECKS_VALIDATION_RESOURCES_EXCEEDED = "Validation resources exceeded (SigChecks)"

# Block has tx in it with too many sigchecks
BLOCK_SIGCHECKS_BAD_TX_SIGCHECKS = "Invalid block due to bad-tx-sigchecks"

# Block as a whole has too many sigchecks
BLOCK_SIGCHECKS_BAD_BLOCK_SIGCHECKS = "Invalid block due to bad-blk-sigchecks"

# Consensus parameter: maximum sigchecks in a transaction
MAX_TX_SIGCHECK = 3000


def create_transaction(spendfrom, custom_script, satisfier=bytes([OP_TRUE]), amount=None):
    # Fund and sign a transaction to a given output.
    # spendfrom should be a CTransaction with first output to OP_TRUE.

    # custom output will go on position 1, after position 0 which will be
    # OP_TRUE (so it can be reused).
    customout = CTxOut(0, bytes(custom_script))
    # set output amount to required dust if not given
    customout.nValue = amount or (len(customout.serialize()) + 148) * 3

    ctx = CTransaction()
    ctx.vin.append(CTxIn(COutPoint(spendfrom.sha256, 0), satisfier))
    ctx.vout.append(CTxOut(0, bytes([OP_TRUE])))
    ctx.vout.append(customout)
    pad_tx(ctx)

    fee = len(ctx.serialize())
    ctx.vout[0].nValue = spendfrom.vout[0].nValue - customout.nValue - fee
    ctx.rehash()

    return ctx


def check_for_ban_on_rejected_tx(pynode, node, tx, reject_reason=None):
    """Check we are disconnected when sending a txn that the node rejects, then reconnect after.

    (Can't actually get banned, since bitcoind won't ban local peers.)"""
    pynode.send_txs_and_test([tx], node, success=False, expect_disconnect=True, reject_reason=reject_reason, timeout=10)
    #disconnect_all(node)
    #node.add_p2p_connection(P2PDataStore())


def check_for_ban_on_rejected_block(pynode, node, block, reject_reason=None, expect_ban=True):
    """Check we are disconnected when sending a block that the node rejects,
    then reconnect after.

    (Can't actually get banned, since bitcoind won't ban local peers.)"""
    pynode.send_blocks_and_test([block], node, success=False, reject_reason=reject_reason, expect_ban=expect_ban, expect_disconnect=True, timeout=30)
    # disconnect_all(node)
    # node.add_p2p_connection(P2PDataStore())


def check_for_no_ban_on_rejected_tx(pynode, node, tx, reject_reason=None):
    """Check we are not disconnected when sending a txn that the node rejects."""
    pynode.send_txs_and_test([tx], node, success=False, reject_reason=reject_reason, timeout=10)


class BlockSigChecksActivationTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.block_heights = {}
        self.extra_args = [[
            "-consensus.forkMay2020Time={}".format(MAY2020_START_TIME),
            "-consensus.maxBlockSigChecks={}".format(MAX_BLOCK_SIGCHECKS)
        ]]

    def getbestblock(self, node):
        """Get the best block. Register its height so we can use build_block."""
        block_height = node.getblockcount()
        blockhash = node.getblockhash(block_height)
        block = FromHex(CBlock(), node.getblock(blockhash, 0))
        block.calc_sha256()
        self.block_heights[block.sha256] = block_height
        return block

    def build_block(self, parent, transactions=(), nTime=None, cbextrascript=None):
        """Make a new block with an OP_1 coinbase output.

        Requires parent to have its height registered."""
        parent.calc_sha256()
        block_height = self.block_heights[parent.sha256] + 1
        block_time = (parent.nTime + 1) if nTime is None else nTime

        block = create_block(parent.sha256, create_coinbase(block_height), block_time)
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
        node = self.nodes[0]
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
        self.pynode.send_blocks_and_test(blocks, node, success=True)
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
        cds_pubkey = bytes.fromhex('03089b476b570d66fad5a20ae6188ebbaf793a4c2a228c65f3d79ee8111d56c932')

        def minefunding2(n):
            """ Mine a block with a bunch of outputs that are very dense
            sigchecks when spent (2 sigchecks each); return the inputs that can
            be used to spend. """
            cds_scriptpubkey = CScript([cds_message, cds_pubkey, OP_3DUP, OP_CHECKDATASIGVERIFY, OP_CHECKDATASIGVERIFY])
            # The scriptsig is carefully padded to have size 26, which is the
            # shortest allowed for 2 sigchecks for mempool admission.
            # The resulting inputs have size 67 bytes, 33.5 bytes/sigcheck.
            cds_scriptsig = CScript([b'x' * 16, cds_signature])
            assert_equal(len(cds_scriptsig), 26)

            logging.debug("Gen {} with locking script {} unlocking script {} .".format(n, cds_scriptpubkey.hex(), cds_scriptsig.hex()))

            tx = self.spendable_outputs.popleft()
            usable_inputs = []
            txes = []
            for i in range(n):
                tx = create_transaction(tx, cds_scriptpubkey, bytes([OP_TRUE]) if i == 0 else b"")
                txes.append(tx)
                usable_inputs.append(CTxIn(COutPoint(tx.sha256, 1), cds_scriptsig))
            newtip = self.build_block(tip, txes)
            self.pynode.send_blocks_and_test([newtip], node, timeout=10)
            return usable_inputs, newtip

        logging.info("Funding special coins that have high sigchecks")

        # mine 5000 funded outputs (10000 sigchecks)
        # will be used pre-activation and post-activation
        usable_inputs, tip = minefunding2(5000)
        # assemble them into 50 txes with 100 inputs each (200 sigchecks)
        submittxes_1 = []
        while len(usable_inputs) >= 100:
            tx = CTransaction()
            tx.vin = [usable_inputs.pop() for _ in range(100)]
            tx.vout = [CTxOut(0, CScript([OP_RETURN]))]
            tx.rehash()
            submittxes_1.append(tx)

        # mine 5000 funded outputs (10000 sigchecks)
        # will be used post-activation
        usable_inputs, tip = minefunding2(5000)
        # assemble them into 50 txes with 100 inputs each (200 sigchecks)
        submittxes_2 = []
        while len(usable_inputs) >= 100:
            tx = CTransaction()
            tx.vin = [usable_inputs.pop() for _ in range(100)]
            tx.vout = [CTxOut(0, CScript([OP_RETURN]))]
            tx.rehash()
            submittxes_2.append(tx)

        # Check high sigcheck transactions
        logging.info("Create transaction that have high sigchecks")

        fundings = []

        def make_spend(sigcheckcount):
            # Add a funding tx to fundings, and return a tx spending that using
            # scriptsig.
            logging.debug("Gen tx with {} sigchecks.".format(sigcheckcount))

            def get_script_with_sigcheck(count):
                return CScript([cds_message,
                                cds_pubkey] + (count - 1) * [OP_3DUP, OP_CHECKDATASIGVERIFY] + [OP_CHECKDATASIG])

            # get funds locked with OP_1
            sourcetx = self.spendable_outputs.popleft()
            # make funding that forwards to scriptpubkey
            last_sigcheck_count = ((sigcheckcount - 1) % 30) + 1
            fundtx = create_transaction(
                sourcetx, get_script_with_sigcheck(last_sigcheck_count))

            fill_sigcheck_script = get_script_with_sigcheck(30)

            remaining_sigcheck = sigcheckcount
            while remaining_sigcheck > 30:
                fundtx.vout[0].nValue -= 1000
                fundtx.vout.append(CTxOut(100, bytes(fill_sigcheck_script)))
                remaining_sigcheck -= 30

            fundtx.rehash()
            fundings.append(fundtx)

            # make the spending
            scriptsig = CScript([cds_signature])

            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(fundtx.sha256, 1), scriptsig))

            input_index = 2
            remaining_sigcheck = sigcheckcount
            while remaining_sigcheck > 30:
                tx.vin.append(
                    CTxIn(
                        COutPoint(
                            fundtx.sha256,
                            input_index),
                        scriptsig))
                remaining_sigcheck -= 30
                input_index += 1

            tx.vout.append(CTxOut(0, CScript([OP_RETURN])))
            pad_tx(tx)
            tx.rehash()
            return tx

        # Create transactions with many sigchecks.
        good_tx = make_spend(MAX_TX_SIGCHECK)
        bad_tx = make_spend(MAX_TX_SIGCHECK + 1)
        tip = self.build_block(tip, fundings)
        self.pynode.send_blocks_and_test([tip], node)

        # Both tx are accepted before the activation.
        pre_activation_sigcheck_block = self.build_block(tip, [good_tx, bad_tx])
        self.pynode.send_blocks_and_test([pre_activation_sigcheck_block], node)
        node.invalidateblock(pre_activation_sigcheck_block.hash)

        # after block is invalidated these tx are put back into the mempool.  Test uses them later so evict.
        waitFor(10, lambda: node.getmempoolinfo()["size"]==2)
        node.evicttransaction(good_tx.hash)
        node.evicttransaction(bad_tx.hash)

        # Activation tests

        logging.info("Approach to just before upgrade activation")
        # Move our clock to the uprade time so we will accept such
        # future-timestamped blocks.
        node.setmocktime(SIGCHECKS_ACTIVATION_TIME + 10)
        # Mine six blocks with timestamp starting at
        # SIGCHECKS_ACTIVATION_TIME-1
        blocks = []
        for i in range(-1, 5):
            tip = self.build_block(tip, nTime=SIGCHECKS_ACTIVATION_TIME + i)
            blocks.append(tip)
        self.pynode.send_blocks_and_test(blocks, node)
        assert_equal(node.getblockchaininfo()['mediantime'], SIGCHECKS_ACTIVATION_TIME - 1)

        logging.info("The next block will activate, but the activation block itself must follow old rules")
        # Send the 50 txes and get the node to mine as many as possible (it should do all)
        # The node is happy mining and validating a 10000 sigcheck block before
        # activation.
        self.pynode.send_txs_and_test(submittxes_1, node)
        [blockhash] = node.generate(1)
        assert_equal(set(node.getblock(blockhash, 1)["tx"][1:]), { t.hash for t in submittxes_1})

        # We have activated, but let's invalidate that.
        assert_equal(node.getblockchaininfo()['mediantime'], SIGCHECKS_ACTIVATION_TIME)
        node.invalidateblock(blockhash)

        # Try again manually and invalidate that too
        goodblock = self.build_block(tip, submittxes_1)
        self.pynode.send_blocks_and_test([goodblock], node)
        node.invalidateblock(goodblock.hash)
        # All transactions should be back in mempool: validation is very slow in debug build
        waitFor(60,lambda: set(node.getrawmempool()) == {t.hash for t in submittxes_1})

        logging.info("Mine the activation block itself")
        tip = self.build_block(tip)
        self.pynode.send_blocks_and_test([tip], node)

        logging.info("We have activated!")
        assert_equal(node.getblockchaininfo()['mediantime'], SIGCHECKS_ACTIVATION_TIME)

        # All transactions get re-evaluated to count sigchecks, so wait for them
        waitFor(60,lambda: set(node.getrawmempool()) == {t.hash for t in submittxes_1})

        logging.info("Try a block with a transaction going over the limit (limit: {})".format(MAX_TX_SIGCHECK))
        bad_tx_block = self.build_block(tip, [bad_tx])
        check_for_ban_on_rejected_block(self.pynode, node, bad_tx_block, reject_reason=BLOCK_SIGCHECKS_BAD_TX_SIGCHECKS)

        logging.info("Try a block with a transaction just under the limit (limit: {})".format(MAX_TX_SIGCHECK))
        good_tx_block = self.build_block(tip, [good_tx])
        self.pynode.send_blocks_and_test([good_tx_block], node)
        node.invalidateblock(good_tx_block.hash)

        # save this tip for later
        # ~ upgrade_block = tip

        # Transactions still in pool:
        waitFor(60, lambda: set(node.getrawmempool()) == {t.hash for t in submittxes_1})

        logging.info("Try sending 10000-sigcheck blocks after activation (limit: {})".format(MAXBLOCKSIZE // BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO))
        # Send block with same txes we just tried before activation
        badblock = self.build_block(tip, submittxes_1)
        check_for_ban_on_rejected_block(self.pynode, node, badblock, reject_reason="Invalid block due to bad-blk-sigchecks", expect_ban=True)

        logging.info("There are too many sigchecks in mempool to mine in a single block. Make sure the node won't mine invalid blocks.  Num tx: %s" % str(node.getmempoolinfo()))
        blk = node.generate(1)
        tip = self.getbestblock(node)
        # only 39 txes got mined.
        assert_equal(len(node.getrawmempool()), 11)

        logging.info("Try sending 10000-sigcheck block with fresh transactions after activation (limit: {})".format(MAXBLOCKSIZE // BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO))
        # Note: in the following tests we'll be bumping timestamp in order
        # to bypass any kind of 'bad block' cache on the node, and get a
        # fresh evaluation each time.

        # Try another block with 10000 sigchecks but all fresh transactions
        badblock = self.build_block(tip, submittxes_2, nTime=SIGCHECKS_ACTIVATION_TIME + 5)
        check_for_ban_on_rejected_block(self.pynode, node, badblock, reject_reason=BLOCK_SIGCHECKS_BAD_BLOCK_SIGCHECKS)

        # Send the same txes again with different block hash. Currently we don't
        # cache valid transactions in invalid blocks so nothing changes.
        badblock = self.build_block(tip, submittxes_2, nTime=SIGCHECKS_ACTIVATION_TIME + 6)
        check_for_ban_on_rejected_block(self.pynode, node, badblock, reject_reason=BLOCK_SIGCHECKS_BAD_BLOCK_SIGCHECKS)

        # Put all the txes in mempool, in order to get them cached:
        self.pynode.send_txs_and_test(submittxes_2, node)
        # Send them again, the node still doesn't like it. But the log
        # error message has now changed because the txes failed from cache.
        badblock = self.build_block(tip, submittxes_2, nTime=SIGCHECKS_ACTIVATION_TIME + 7)
        check_for_ban_on_rejected_block(self.pynode, node, badblock, reject_reason=BLOCK_SIGCHECKS_BAD_BLOCK_SIGCHECKS)

        logging.info("Try sending 8000-sigcheck block after activation (limit: {})".format(MAXBLOCKSIZE // BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO))
        badblock = self.build_block(tip, submittxes_2[:40], nTime=SIGCHECKS_ACTIVATION_TIME + 5)
        check_for_ban_on_rejected_block(self.pynode, node, badblock, reject_reason=BLOCK_SIGCHECKS_BAD_BLOCK_SIGCHECKS)
        # redundant, but just to mirror the following test...
        node.set("consensus.maxBlockSigChecks=%d" % MAX_BLOCK_SIGCHECKS)

        logging.info("Bump the excessiveblocksize limit by 1 byte, and send another block with same txes (new sigchecks limit: {})".format((MAXBLOCKSIZE + 1) // BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO))
        node.set("consensus.maxBlockSigChecks=%d" % (MAX_BLOCK_SIGCHECKS+1))
        tip = self.build_block(tip, submittxes_2[:40], nTime=SIGCHECKS_ACTIVATION_TIME + 6)
        # It should succeed now since limit should be 8000.
        self.pynode.send_blocks_and_test([tip], node)


if __name__ == '__main__':
    BlockSigChecksActivationTest().main()


# Create a convenient function for an interactive python debugging session
def Test():
    from test_framework.util import standardFlags, SetupPythonLogConfig
    t = BlockSigChecksActivationTest()
    t.drop_to_pdb = True
    SetupPythonLogConfig("DEBUG")
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
