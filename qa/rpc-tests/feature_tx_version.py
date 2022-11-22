#!/usr/bin/env python3
# Copyright (c) 2022 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test enforcement of strict tx version. Before upgrade9 it is a relay-only rule
   and after upgrade9 tx versions must be either 1 or 2 by consensus."""
import time
from typing import Optional

from test_framework import cashaddr
from test_framework.blocktools import (
    create_block,
    create_coinbase,
    create_tx_with_script,
)
from test_framework.messages import (
    CBlock,
    CTransaction,
    FromHex,
)
from test_framework.p2p import P2PDataStore
from test_framework.script import (
    CScript,
    hash160,
    OP_EQUAL,
    OP_HASH160,
    OP_TRUE,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than_or_equal,
)

DUST = 546


class TxVersionTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.base_extra_args = ['-acceptnonstdtxn=0', '-expire=0', '-whitelist=127.0.0.1']
        self.extra_args = [['-upgrade9activationtime=999999999999'] + self.base_extra_args]

    def run_test(self):

        node = self.nodes[0]  # convenience reference to the node

        self.block_heights = dict()

        self.bootstrap_p2p()  # Add one p2p connection to the node

        genesis = FromHex(CBlock(), node.getblock(node.getbestblockhash(), 0))

        # To simplify and speed up this test, we do no signing, just us an "anyone can spend" signature-less p2sh
        # script for everything.
        redeem_script = CScript([OP_TRUE])
        anyonecanspend = CScript([OP_HASH160, hash160(redeem_script), OP_EQUAL])
        anyonecanspend_address = cashaddr.encode("bchreg", cashaddr.SCRIPT_TYPE, hash160(redeem_script))

        # Create a new block
        b1 = self.create_block(genesis, nTime=int(time.time()),
                               script_pub_key=anyonecanspend, redeem_script=redeem_script,
                               nVersion_cb=1)
        self.send_blocks([b1])

        # Allow the block to mature, and then some
        blocks = [b1]
        for i in range(130):
            blocks.append(self.create_block(blocks[-1],
                                            script_pub_key=anyonecanspend, redeem_script=redeem_script,
                                            nVersion_cb=1))
        self.send_blocks(blocks[1:])

        for block in blocks:
            for txn in block.vtx:
                # Ensure all the txns are nVersion==1
                assert_equal(txn.nVersion, 1)

        spend_index = 0

        def create_various_version_txns_and_test():
            nonlocal spend_index

            # Test: Create 2 txns that have nVersion=1 and nVersion=2, send to mempool -- they should be accepted ok and
            #       mined ok
            tx1 = self.create_tx(blocks[spend_index].vtx[0], 0, script_pub_key=anyonecanspend,
                                 redeem_script=redeem_script,
                                 nVersion=1)
            spend_index += 1
            assert_equal(tx1.nVersion, 1)
            tx2 = self.create_tx(blocks[spend_index].vtx[0], 0, script_pub_key=anyonecanspend,
                                 redeem_script=redeem_script,
                                 nVersion=2)
            spend_index += 1
            assert_equal(tx2.nVersion, 2)
            self.send_txs([tx1, tx2], success=True)
            assert_equal(set(node.getrawmempool()), {tx1.hash, tx2.hash})  # Mempool should have both txns
            hashes = node.generatetoaddress(1, anyonecanspend_address)
            assert_equal(len(node.getrawmempool()), 0)  # Mempool should have 0 txns
            blk = FromHex(CBlock(), node.getblock(hashes[-1], 0))
            blocks.append(blk)  # Remember this block we just mined
            missing = {tx1.hash, tx2.hash}
            for txn in blk.vtx:
                txn.calc_sha256()
                missing.discard(txn.hash)
            assert not missing, "Txs not found in block as expected!"

            # Test: Create txns that have nVersion out of bounds, send to mempool -- should be rejected as non-standard
            # if upgrade9 is not enabled, or rejected for being out-of-spec if upgrade9 is enabled
            tx3 = self.create_tx(blocks[spend_index].vtx[0], 0, script_pub_key=anyonecanspend, redeem_script=redeem_script,
                                 nVersion=3)
            spend_index += 1
            assert_equal(tx3.nVersion, 3)
            self.send_txs([tx3], success=False, reject_reason="was not accepted: version")
            tx0 = self.create_tx(blocks[spend_index].vtx[0], 0, script_pub_key=anyonecanspend, redeem_script=redeem_script,
                                 nVersion=0)
            spend_index += 1
            assert_equal(tx0.nVersion, 0)
            self.send_txs([tx0], success=False, reject_reason="was not accepted: version")
            tx123456 = self.create_tx(blocks[spend_index].vtx[0], 0, script_pub_key=anyonecanspend, redeem_script=redeem_script,
                                     nVersion=123456)
            spend_index += 1
            assert_equal(tx123456.nVersion, 123456)
            self.send_txs([tx123456], success=False, reject_reason="was not accepted: version")
            assert len(node.getrawmempool()) == 0, "Mempool should have 0 txns"
            return tx3, tx0, tx123456

        # Pre-Upgrade9 Test: The below function call creates 2 txns that have nVersion=1 and nVersion=2, sends them to
        # the mempool -- they should be accepted ok and mined ok.  It also creates 3 txns that have nVersion out of
        # range, sends them to mempool -- they should be rejected as non-standard. It returns the 3 out-of-bounds
        # txns.
        tx3, tx0, tx123456 = create_various_version_txns_and_test()

        # However, the same txns should be ok if mined in a block
        blocks.append(self.create_block(blocks[-1], script_pub_key=anyonecanspend, redeem_script=redeem_script,
                                        txns=[tx3, tx0, tx123456]))
        self.send_blocks(blocks[-1:])
        assert_equal(node.getbestblockhash(), blocks[-1].hash)
        missing = {tx0.hash, tx3.hash, tx123456.hash}
        for txn in blocks[-1].vtx:
            txn.calc_sha256()
            missing.discard(txn.hash)
        assert not missing, "Txs not found in block as expected!"

        activation_time = blocks[-1].nTime
        # Restart the node, enabling upgrade9
        self.restart_node(0, extra_args=[f"-upgrade9activationtime={activation_time}"] + self.base_extra_args)
        self.reconnect_p2p()

        self.log.info("Advance blockchain forward to enable upgrade9")
        iters = 0
        est_median_time = node.getblockchaininfo()["mediantime"]
        while est_median_time < activation_time:
            iters += 1
            est_median_time += 1  # create_block below just advances mediantime by 1 second each time
            blocks.append(self.create_block(blocks[-1], spend=blocks[spend_index].vtx[0],
                                            script_pub_key=anyonecanspend, redeem_script=redeem_script))
            spend_index += 1
        assert iters > 0
        self.send_blocks(blocks[-iters:])
        # Paranoia: Ensure node accepted above chain
        assert_equal(node.getbestblockhash(), blocks[-1].sha256.to_bytes(length=32, byteorder="big").hex())
        # Ensure upgrade9 activated
        assert_greater_than_or_equal(node.getblockchaininfo()["mediantime"], activation_time)
        self.log.info(f"Iterated {iters} times to bring mediantime ({node.getblockchaininfo()['mediantime']}) up to "
                      f"activation_time ({activation_time}) -- Upgrade9 is now activated for the next block!")

        # Post-Upgrade9 Test: The below function call creates 2 txns that have nVersion=1 and nVersion=2, sends them to
        # the mempool -- they should be accepted ok and mined ok.  It also creates 3 txns that have nVersion out of
        # range, sends them to mempool -- they should be rejected as out of consensus. It returns the 3 out-of-bounds
        # txns.

        tx3, tx0, tx123456 = create_various_version_txns_and_test()

        # This time, under Upgrade9, the same txns should be REJECTED if mined in a block
        for bad_tx in (tx3, tx0, tx123456):
            badblk = self.create_block(blocks[-1], script_pub_key=anyonecanspend, redeem_script=redeem_script,
                                       txns=[bad_tx])
            missing = {bad_tx.hash}
            for txn in badblk.vtx:
                txn.calc_sha256()
                missing.discard(txn.hash)
            assert not missing, "Tx not found in block as expected!"
            # Now, send the block and it should be rejected by consensus
            self.send_blocks([badblk], success=False, reject_reason="bad-txns-version")

        # Next, try a few blocks with funny coinbase version as well just for belt-and-suspenders
        bad_versions = [3, 0, -1, 0x7fffffff, 32132, 4, -2, 65536]
        for bv in bad_versions:
            badblk = self.create_block(blocks[-1], script_pub_key=anyonecanspend, nVersion_cb=bv)
            self.send_blocks([badblk], success=False, reject_reason="bad-txns-version")
        # Also do the above, but with a spend_tx
        for bv in bad_versions:
            badblk = self.create_block(blocks[-1], script_pub_key=anyonecanspend, nVersion_cb=1, nVersion_spend=bv,
                                       redeem_script=redeem_script, spend=blocks[spend_index].vtx[0])
            self.send_blocks([badblk], success=False, reject_reason="bad-txns-version")

        # Ensure nothing changed and the tip we expect is blocks[-1]
        assert_equal(node.getbestblockhash(), blocks[-1].hash)

    @staticmethod
    def create_tx(spend_tx, n, value=None, script_pub_key=CScript([OP_TRUE]), nVersion=None, redeem_script=None):
        """this is a little handier to use than the version in blocktools.py"""
        if value is None:
            value = spend_tx.vout[0].nValue - 1000
            assert value >= DUST
        tx = create_tx_with_script(spend_tx, n, amount=value, script_pub_key=script_pub_key)
        if nVersion is not None:
            tx.nVersion = nVersion
        if redeem_script is not None:
            tx.vin[0].scriptSig = CScript([redeem_script])
        tx.rehash()
        return tx

    def create_block(self, prev_block: CBlock, *, spend: Optional[CTransaction] = None,
                     script_pub_key=CScript([OP_TRUE]), redeem_script=None,
                     nTime: int = None, nVersion_cb=None, nVersion_spend=None, txns=None) -> CBlock:
        if prev_block.sha256 is None:
            prev_block.rehash()
        assert prev_block.sha256 is not None  # Satisfy linter with this assertion
        prev_block_hash = prev_block.sha256
        block_time = prev_block.nTime + 1 if nTime is None else nTime
        # First create the coinbase
        height = self.block_heights.get(prev_block_hash, 0) + 1
        coinbase = create_coinbase(height, scriptPubKey=script_pub_key)
        if nVersion_cb is not None:
            coinbase.nVersion = nVersion_cb
        coinbase.rehash()

        txns = txns or []
        if spend is not None:
            # all but one satoshi to fees
            coinbase.vout[0].nValue += spend.vout[0].nValue - DUST
            coinbase.rehash()
            # spend 1 satoshi
            tx = self.create_tx(spend_tx=spend, n=0, value=DUST, script_pub_key=script_pub_key, nVersion=nVersion_spend,
                                redeem_script=redeem_script)
            tx.rehash()
            txns.append(tx)

        block = create_block(prev_block_hash, coinbase, block_time, txns=txns)
        block.solve()
        self.block_heights[block.sha256] = height
        return block

    def bootstrap_p2p(self):
        """Add a P2P connection to the node.

        Helper to connect and wait for version handshake."""
        self.nodes[0].add_p2p_connection(P2PDataStore())
        # We need to wait for the initial getheaders from the peer before we
        # start populating our blockstore. If we don't, then we may run ahead
        # to the next subtest before we receive the getheaders. We'd then send
        # an INV for the next block and receive two getheaders - one for the
        # IBD and one for the INV. We'd respond to both and could get
        # unexpectedly disconnected if the DoS score for that error is 50.
        self.nodes[0].p2p.wait_for_getheaders(timeout=5)

    def reconnect_p2p(self):
        """Tear down and bootstrap the P2P connection to the node.

        The node gets disconnected several times in this test. This helper
        method reconnects the p2p and restarts the network thread."""
        bs, lbh, ts, p2p = None, None, None, None
        if self.nodes[0].p2ps:
            p2p = self.nodes[0].p2p
            bs, lbh, ts = p2p.block_store, p2p.last_block_hash, p2p.tx_store
        self.nodes[0].disconnect_p2ps()
        self.bootstrap_p2p()
        if p2p and (bs or lbh or ts):
            # Set up the block store again so that p2p node can adequately send headers again for everything
            # node might want after a restart
            p2p = self.nodes[0].p2p
            p2p.block_store, p2p.last_block_hash, p2p.tx_store = bs, lbh, ts

    def send_blocks(self, blocks, success=True, reject_reason=None,
                    request_block=True, reconnect=False, timeout=60):
        """Sends blocks to test node. Syncs and verifies that tip has advanced to most recent block.

        Call with success = False if the tip shouldn't advance to the most recent block."""
        self.nodes[0].p2p.send_blocks_and_test(blocks, self.nodes[0], success=success,
                                               reject_reason=reject_reason, request_block=request_block,
                                               timeout=timeout, expect_disconnect=reconnect)
        if reconnect:
            self.reconnect_p2p()

    def send_txs(self, txs, success=True, reject_reason=None, reconnect=False):
        """Sends txns to test node. Syncs and verifies that txns are in mempool

        Call with success = False if the txns should be rejected."""
        self.nodes[0].p2p.send_txs_and_test(txs, self.nodes[0], success=success, expect_disconnect=reconnect,
                                            reject_reason=reject_reason)
        if reconnect:
            self.reconnect_p2p()


if __name__ == '__main__':
    TxVersionTest().main()
