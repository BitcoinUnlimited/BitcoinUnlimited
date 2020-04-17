#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
This tests the CHECKMULTISIG mode that uses Schnorr transaction signatures
and repurposes the dummy element to indicate which signatures are being checked.
- acceptance both in mempool and blocks.
- check non-banning for peers who send invalid txns that would have been valid
on the other side of the upgrade.
- check banning of peers for some fully-invalid transactions.
"""

from test_framework.blocktools import (
    create_block,
    create_coinbase,
    create_transaction,
    make_conform_to_ctor,
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
    OP_0,
    OP_1,
    OP_CHECKMULTISIG,
    OP_TRUE,
    SIGHASH_ALL,
    SIGHASH_FORKID,
    SignatureHashForkId,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, p2p_port, waitFor
import logging

# ECDSA checkmultisig with non-null dummy are invalid since the new mode
# refuses ECDSA.
ECDSA_NULLDUMMY_ERROR = 'mandatory-script-verify-flag-failed (Only Schnorr signatures allowed in this operation)'

# A mandatory (bannable) error occurs when people pass Schnorr signatures into
# legacy OP_CHECKMULTISIG.
SCHNORR_LEGACY_MULTISIG_ERROR = 'mandatory-script-verify-flag-failed (Signature cannot be 65 bytes in CHECKMULTISIG)'

# Blocks with invalid scripts give this error:
BADINPUTS_ERROR = 'bad-blk-signatures'


# This 64-byte signature is used to test exclusion & banning according to
# the above error messages.
# Tests of real 64 byte ECDSA signatures can be found in script_tests.
sig64 = b'\0'*64

class P2PNode(P2PDataStore):
    pass

class SchnorrMultisigTest(BitcoinTestFramework):
    def __init__(self):
        super().__init__()
        self.set_test_params()

    def set_test_params(self):
        self.num_nodes = 1
        self.block_heights = {}
        self.extra_args = [["-debug=mempool"]]

    def bootstrap_p2p(self):
        """Add a P2P connection to the node.

        Helper to connect and wait for version handshake."""
        self.p2p = P2PNode()
        self.connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], self.p2p)
        self.p2p.add_connection(self.connection)
        NetworkThread().start()
        self.p2p.wait_for_verack()
        assert(self.p2p.connection.state == "connected")

    def getbestblock(self, node):
        """Get the best block. Register its height so we can use build_block."""
        block_height = node.getblockcount()
        blockhash = node.getblockhash(block_height)
        block = FromHex(CBlock(), node.getblock(blockhash, 0))
        block.calc_sha256()
        self.block_heights[block.sha256] = block_height
        return block

    def build_block(self, parent, transactions=(), nTime=None):
        """Make a new block with an OP_1 coinbase output.

        Requires parent to have its height registered."""
        parent.calc_sha256()
        block_height = self.block_heights[parent.sha256] + 1
        block_time = (parent.nTime + 1) if nTime is None else nTime

        block = create_block(
            parent.sha256, create_coinbase(block_height, scriptPubKey = CScript([OP_TRUE])), block_time)
        block.vtx.extend(transactions)
        make_conform_to_ctor(block)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        self.block_heights[block.sha256] = block_height
        return block

    def check_for_ban_on_rejected_tx(self, tx, reject_reason=None):
        """Check we are banned when sending a txn that the node rejects.

        (Can't actually get banned, since bitcoind won't ban local peers.)"""
        self.p2p.send_txs_and_test(
            [tx], self.nodes[0], success=False, expect_ban=True, reject_reason=reject_reason)

    def check_for_ban_on_rejected_block(self, block, reject_reason=None):
        """Check we are banned when sending a block that the node rejects.

        (Can't actually get banned, since bitcoind won't ban local peers.)"""
        self.p2p.send_blocks_and_test(
            [block], self.nodes[0], success=False, reject_reason=reject_reason, expect_ban=True)

    def run_test(self):
        logging.info("Initializing test directory "+self.options.tmpdir)
        node = self.nodes[0]

        self.bootstrap_p2p()

        tip = self.getbestblock(node)

        logging.info("Create some blocks with OP_1 coinbase for spending.")
        blocks = []
        for _ in range(10):
            tip = self.build_block(tip)
            blocks.append(tip)
        self.p2p.send_blocks_and_test(blocks, node, success=True)
        spendable_outputs = [block.vtx[0] for block in blocks]

        logging.info("Mature the blocks and get out of IBD.")
        node.generate(100)

        tip = self.getbestblock(node)

        logging.info("Setting up spends to test and mining the fundings.")
        fundings = []

        # Generate a key pair
        privkeybytes = b"Schnorr!" * 4
        private_key = CECKey()
        private_key.set_secretbytes(privkeybytes)
        # get uncompressed public key serialization
        public_key = private_key.get_pubkey()

        def create_fund_and_spend_tx(dummy=OP_0, sigtype='ecdsa'):
            spendfrom = spendable_outputs.pop()

            script = CScript([OP_1, public_key, OP_1, OP_CHECKMULTISIG])

            value = spendfrom.vout[0].nValue

            # Fund transaction
            txfund = create_transaction(spendfrom, 0, b'', value, script)
            txfund.rehash()
            fundings.append(txfund)

            # Spend transaction
            txspend = CTransaction()
            txspend.vout.append(
                CTxOut(value-1000, CScript([OP_TRUE])))
            txspend.vin.append(
                CTxIn(COutPoint(txfund.sha256, 0), b''))

            # Sign the transaction
            sighashtype = SIGHASH_ALL | SIGHASH_FORKID
            hashbyte = bytes([sighashtype & 0xff])
            sighash = SignatureHashForkId(
                script, txspend, 0, sighashtype, value)
            if sigtype == 'schnorr':
                txsig = schnorr.sign(privkeybytes, sighash) + hashbyte
            elif sigtype == 'ecdsa':
                txsig = private_key.sign(sighash) + hashbyte
            txspend.vin[0].scriptSig = CScript([dummy, txsig])
            txspend.rehash()

            return txspend

        # This is valid.
        ecdsa0tx = create_fund_and_spend_tx(OP_0, 'ecdsa')

        # This is invalid.
        ecdsa1tx = create_fund_and_spend_tx(OP_1, 'ecdsa')

        # This is invalid.
        schnorr0tx = create_fund_and_spend_tx(OP_0, 'schnorr')

        # This is valid.
        schnorr1tx = create_fund_and_spend_tx(OP_1, 'schnorr')

        tip = self.build_block(tip, fundings)
        self.p2p.send_blocks_and_test([tip], node)

        logging.info("Send a legacy ECDSA multisig into mempool.")
        self.p2p.send_txs_and_test([ecdsa0tx], node)
        waitFor(10, lambda: node.getrawmempool() == [ecdsa0tx.hash])

        logging.info("Trying to mine a non-null-dummy ECDSA.")
        self.check_for_ban_on_rejected_block(
            self.build_block(tip, [ecdsa1tx]), BADINPUTS_ERROR)
        logging.info(
            "If we try to submit it by mempool or RPC, it is rejected and we are banned")
        assert_raises_rpc_error(-26, ECDSA_NULLDUMMY_ERROR, node.sendrawtransaction, ToHex(ecdsa1tx))
        self.check_for_ban_on_rejected_tx(ecdsa1tx, ECDSA_NULLDUMMY_ERROR)

        logging.info(
            "Submitting a Schnorr-multisig via net, and mining it in a block")
        self.p2p.send_txs_and_test([schnorr1tx], node)
        waitFor(10, lambda: set(node.getrawmempool()) == {ecdsa0tx.hash, schnorr1tx.hash})
        tip = self.build_block(tip, [schnorr1tx])
        self.p2p.send_blocks_and_test([tip], node)

        logging.info(
            "That legacy ECDSA multisig is still in mempool, let's mine it")
        waitFor(10, lambda: node.getrawmempool() == [ecdsa0tx.hash])
        tip = self.build_block(tip, [ecdsa0tx])
        self.p2p.send_blocks_and_test([tip], node)
        waitFor(10, lambda: node.getrawmempool() == [])

        logging.info(
            "Trying Schnorr in legacy multisig is invalid and banworthy.")
        self.check_for_ban_on_rejected_tx(
            schnorr0tx, SCHNORR_LEGACY_MULTISIG_ERROR)
        self.check_for_ban_on_rejected_block(
            self.build_block(tip, [schnorr0tx]), BADINPUTS_ERROR)

if __name__ == '__main__':
    SchnorrMultisigTest().main()

    # Create a convenient function for an interactive python debugging session
def Test():
    from test_framework.util import standardFlags
    t = SchnorrMultisigTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["dbase", "selectcoins"], # ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000,  # we don't want any transactions rejected due to insufficient fees...
        "logtimemicros":1,
        "checkmempool":0,
        # "par":1  # Reduce the # of threads in bitcoind for easier debugging
    }


    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
