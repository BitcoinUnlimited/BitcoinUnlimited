#!/usr/bin/env python3
# Copyright (c) 2022 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test support for p2sh32 which activates with Upgrade9."""
import random
from typing import List, NamedTuple

from test_framework import cashaddr
from test_framework.blocktools import create_block, create_coinbase
from test_framework.key import CECKey
from test_framework.nodemessages import (
    CBlock,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    FromHex,
    uint256_from_str,
    ser_uint256,
)
from test_framework.mininode import P2PDataStore, NetworkThread
from test_framework import schnorr
from test_framework.script import (
    CScript,
    OP_CHECKSIG, OP_DROP, OP_DUP, OP_EQUAL, OP_EQUALVERIFY, OP_FALSE, OP_HASH160, OP_HASH256, OP_RETURN, OP_TRUE,
    SIGHASH_ALL, SIGHASH_FORKID,
    SignatureHashForkId,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, hash160, hash256
from test_framework.bunode import NodeConn
from test_framework.portseed import p2p_port

import logging

privkey = b"P2SH32!!" * 4;

def uint256_from_hex(h: str) -> int:
    return uint256_from_str(bytes.fromhex(h)[::-1])


def uint256_to_hex(u: int) -> str:
    return ser_uint256(u)[::-1].hex()


class UTXO(NamedTuple):
    outpt: COutPoint
    txout: CTxOut


class SegWit(NamedTuple):
    addr: str  # bitcoin cash address
    spk: CScript  # scriptPubKey
    rs: bytes  # redeemScript


class P2SH32Test(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.base_extra_args = ['-acceptnonstdtxn=0', '-whitelist=127.0.0.1', "-debug=mempool"]
        self.extra_args = [['-upgrade9activationtime=999999999999'] + self.base_extra_args]

    @staticmethod
    def get_rand_bytes(n_bytes: int = 32) -> bytes:
        assert n_bytes >= 0
        n_bits = n_bytes * 8
        return random.getrandbits(n_bits).to_bytes(length=n_bytes, byteorder='little')

    def run_test(self):
        node = self.nodes[0]  # convenience reference to the node
        self.bootstrap_p2p()  # add one p2p connection to the node

        # Setup a private key and address we will use for all transactions
        self.priv_key = CECKey()
        self.priv_key.set_secretbytes(privkey)
        # Make p2sh32 wrapping p2pkh
        pub_key = self.priv_key.get_pubkey()
        p2pkh_spk = redeem_script = CScript([OP_DUP, OP_HASH160, hash160(pub_key), OP_EQUALVERIFY, OP_CHECKSIG])
        p2pkh_addr = cashaddr.encode("bchreg", cashaddr.PUBKEY_TYPE, hash160(pub_key))
        self.addr_cashaddr = cashaddr.encode("bchreg", cashaddr.SCRIPT_TYPE, hash256(redeem_script))
        logging.info(f"Cashaddr: {self.addr_cashaddr}")
        # scriptPubKey for p2sh32
        self.spk = CScript([OP_HASH256, hash256(redeem_script), OP_EQUAL])
        self.redeem_scripts = {self.spk: redeem_script}
        # Make some spk's, etc for trivially spendable OP_TRUE, OP_FALSE scripts. We pad the redeem_scripts to ensure
        # the txns they appear in are >= 100 bytes.
        redeem_script_op_false = CScript([self.get_rand_bytes(), OP_DROP, OP_FALSE])
        redeem_script_op_true = CScript([self.get_rand_bytes(), OP_DROP, OP_TRUE])
        spk_op_false = CScript([OP_HASH256, hash256(redeem_script_op_false), OP_EQUAL])
        spk_op_true = CScript([OP_HASH256, hash256(redeem_script_op_true), OP_EQUAL])
        p2sh32_addr_false = cashaddr.encode("bchreg", cashaddr.SCRIPT_TYPE, hash256(redeem_script_op_false))
        p2sh32_addr_true = cashaddr.encode("bchreg", cashaddr.SCRIPT_TYPE, hash256(redeem_script_op_true))
        # Create 4 different segwit addresses (of various types)
        segwits = []
        for case in range(4):
            segwits.append(self.create_segwit_address_spk_and_redeem_script(case))

        # Generate two blocks to our p2pkh for spending pre-activation
        blockhashes = node.generatetoaddress(2, p2pkh_addr)
        # Generate two blocks to our p2sh32 that is OP_FALSE for spending pre-activation and failure post-activation
        blockhashes += node.generatetoaddress(2, p2sh32_addr_false)
        # Generate two blocks to our p2sh32 that is OP_TRUE for spending both pre and post activation
        blockhashes += node.generatetoaddress(2, p2sh32_addr_true)
        # Generate eight blocks, two for each of our segwit addresses
        for sw in segwits:
            blockhashes += node.generatetoaddress(2, sw.addr)
        # Generate to our p2sh32 -- we can't spend these until after activation.
        blockhashes += node.generatetoaddress(103, self.addr_cashaddr)

        def sum_values(utxos: List[UTXO]) -> int:
            return sum(txout.nValue for _, txout in utxos)

        def create_a_txn_spending_from_block(blockhash, from_spk, to_spk, *, sign=True) -> CTransaction:
            block = FromHex(CBlock(), node.getblock(blockhash, 0))
            tx = block.vtx[0]
            tx.calc_sha256()
            assert_equal(tx.vout[0].scriptPubKey.hex(), from_spk.hex())  # Sanity check
            self.utxos: List[UTXO] = []
            self.update_utxos(tx)  # grab the UTXO from this block
            return self.create_tx(self.utxos, [CTxOut(sum_values(self.utxos) - 500, to_spk)], sign=sign)

        # Attempt to send from the p2pkh in blocks 0 & 1 above *to* a p2sh32; these will be rejected as non-standard
        txns_send_to_p2sh32 = []
        for i in range(2):
            # Create the txn that spends from our p2pkh *to* our p2sh32
            tx = create_a_txn_spending_from_block(blockhashes[i], from_spk=p2pkh_spk, to_spk=self.spk)
            # Rejected as non-standard output (send to p2sh32 is non-standard pre-activation)
            self.send_txs([tx], success=False, reject_reason='scriptpubkey')
            assert tx.hash not in node.getrawmempool()
            txns_send_to_p2sh32.append(tx)

        txns_op_false = []
        for i in range(2, 4):
            tx = create_a_txn_spending_from_block(blockhashes[i], from_spk=spk_op_false, to_spk=p2pkh_spk, sign=False)
            tx.vin[0].scriptSig = CScript([redeem_script_op_false])  # Push redeemScript to "solve" hash puzzle
            tx.rehash()
            # Rejected as non-standard output (spend of a p2sh32 input is non-standard pre-activation)
            self.send_txs([tx], success=False, reject_reason='bad-txns-nonstandard-inputs')
            txns_op_false.append(tx)

        txns_op_true = []
        for i in range(4, 6):
            tx = create_a_txn_spending_from_block(blockhashes[i], from_spk=spk_op_true, to_spk=p2pkh_spk, sign=False)
            tx.vin[0].scriptSig = CScript([redeem_script_op_true])  # Push redeemScript to "solve" hash puzzle
            tx.rehash()
            # Rejected as non-standard output (spend of a p2sh32 input is non-standard pre-activation)
            self.send_txs([tx], success=False, reject_reason='bad-txns-nonstandard-inputs')
            txns_op_true.append(tx)

        txns_spend_segwit = [[], []]  # [0] -> list of 4 pre-activation txns, [1] -> list of 4 post-activation txns
        for i in range(6, 14):
            i0 = i - 6
            sw_idx = i0 // 2
            pre_post_idx = i0 % 2
            sw = segwits[sw_idx]
            tx = create_a_txn_spending_from_block(blockhashes[i], from_spk=sw.spk, to_spk=p2pkh_spk, sign=False)
            tx.vin[0].scriptSig = CScript([sw.rs])  # Push redeemScript to "solve" hash puzzle
            tx.vout.append(CTxOut(nValue=0, scriptPubKey=CScript([OP_RETURN, self.get_rand_bytes()])))  # pad tx >= 100
            tx.rehash()
            # Rejected as non-standard output (spend of a p2sh32 input is non-standard pre-activation)
            self.send_txs([tx], success=False, reject_reason='bad-txns-nonstandard-inputs')
            txns_spend_segwit[pre_post_idx].append(tx)

        txns_hash_puzzle_only = []
        for i in range(14, 16):
            tx = create_a_txn_spending_from_block(blockhashes[i], from_spk=self.spk, to_spk=p2pkh_spk, sign=False)
            tx.vin[0].scriptSig = CScript([redeem_script])  # Push redeemScript to "solve" hash puzzle
            tx.rehash()
            # Rejected as non-standard input (p2sh32 as input is non-standard pre-activation)
            self.send_txs([tx], success=False, reject_reason='bad-txns-nonstandard-inputs')
            assert tx.hash not in node.getrawmempool()
            txns_hash_puzzle_only.append(tx)

        # Next, sign a p2sh32 tx and confirm it CANNOT be sent (we send to our p2pkh to ensure standard output)
        block16 = FromHex(CBlock(), node.getblock(blockhashes[16], 0))
        tx = block16.vtx[0]
        tx.calc_sha256()
        assert_equal(tx.vout[0].scriptPubKey.hex(), self.spk.hex())
        self.utxos = []  # Clear the UTXOs to start fresh and spend only from our p2sh32 address
        self.update_utxos(tx)
        # Create the txn that spends from our p2sh32 *to* our p2pkh
        tx = self.create_tx(self.utxos, [CTxOut(sum_values(self.utxos) - 500, p2pkh_spk)])
        # Rejected as non-standard input (p2sh32 as input is non-standard pre-activation)
        self.send_txs([tx], success=False, reject_reason='bad-txns-nonstandard-inputs')
        assert tx.hash not in node.getrawmempool()

        # Restart the node, enabling non-standard txns
        nonstd_args = self.extra_args[0].copy()
        assert_equal(nonstd_args[1], "-acceptnonstdtxn=0")
        nonstd_args[1] = "-acceptnonstdtxn=1"
        self.restart_node(0, extra_args=nonstd_args)
        self.reconnect_p2p()
        # Rejected due to failure to evaluate script (non-empty stack)
        self.send_txs([tx], success=False,
                      reject_reason='non-mandatory-script-verify-flag (P2SH script evaluation of script does not result in a clean stack)')
        assert tx.hash not in node.getrawmempool()
        expected_hashes = set()
        # However, sending to p2sh32 should work
        self.send_txs([txns_send_to_p2sh32[0]], success=True)
        expected_hashes |= {txns_send_to_p2sh32[0].hash}
        # Also the other test cases should work
        self.send_txs([txns_hash_puzzle_only[0], txns_op_true[0], txns_op_false[0]], success=True)
        expected_hashes |= {txns_hash_puzzle_only[0].hash, txns_op_true[0].hash, txns_op_false[0].hash}
        for segwit_txn in txns_spend_segwit[0]:
            self.send_txs([segwit_txn], success=True)
            expected_hashes |= {segwit_txn.hash}
        assert_equal(expected_hashes, set(node.getrawmempool()))
        # Go back to rejecting non-standard txns
        self.restart_node(0, extra_args=self.extra_args[0])
        self.reconnect_p2p()
        assert not node.getrawmempool()  # Mempool now cleared again of non-std txns

        # Next, try to mine the p2sh-spending tx and confirm that the block is rejected (blk-bad-inputs)
        block = self.create_block(prev_block=FromHex(CBlock(), node.getblock(blockhashes[-1], False)),
                                  height=node.getblockchaininfo()["blocks"] + 1,
                                  txns=[tx])

        # This should be 'blk-bad-inputs', see issue #2258
        reject_bad_inputs = "Script Error: P2SH script evaluation of script does not result in a clean stack"
        self.send_blocks([block], success=False, reject_reason=reject_bad_inputs)
        assert_equal(blockhashes[-1], node.getbestblockhash())

        # Next, try to mine one of the p2sh-puzzle-solving txns and confirm that the block is accepted
        block = self.create_block(prev_block=FromHex(CBlock(), node.getblock(blockhashes[-1], False)),
                                  height=node.getblockchaininfo()["blocks"] + 1,
                                  txns=[txns_hash_puzzle_only[0]])
        block.rehash()
        self.send_blocks([block], success=True)
        blockhashes += [block.hash]
        assert_equal(blockhashes[-1], node.getbestblockhash())

        # Next, try to mine the two p2sh32's that have scripts OP_TRUE and OP_FALSE and confirm that the block is
        # accepted (both work pre-activation since it's just a puzzle-solve and the scripts aren't evaluated)
        block = self.create_block(prev_block=FromHex(CBlock(), node.getblock(blockhashes[-1], False)),
                                  height=node.getblockchaininfo()["blocks"] + 1,
                                  txns=[txns_op_true[0], txns_op_false[0]])
        block.rehash()
        self.send_blocks([block], success=True)
        blockhashes += [block.hash]
        assert_equal(blockhashes[-1], node.getbestblockhash())

        # Next, try to mine four segwit txns that are p2sh32. They should all be accepted. (since it's just a
        # hash-puzzle-solve and the scripts aren't evaluated)
        block = self.create_block(prev_block=FromHex(CBlock(), node.getblock(blockhashes[-1], False)),
                                  height=node.getblockchaininfo()["blocks"] + 1,
                                  txns=txns_spend_segwit[0])
        block.rehash()
        self.send_blocks([block], success=True)
        blockhashes += [block.hash]
        assert_equal(blockhashes[-1], node.getbestblockhash())

        # Next, activate the upgrade

        # Get the current MTP time
        activation_time = node.getblockchaininfo()["mediantime"]
        # Restart the node, enabling upgrade9
        self.restart_node(0, extra_args=[f"-upgrade9activationtime={activation_time}"] + self.base_extra_args)
        self.reconnect_p2p()

        # Now, the txn that spends p2sh32 should be accepted ok as "standard"
        # Also, the txn that sends *to* p2sh32 should also be accepted as "standard"
        # Also, the txn that spends p2sh32 of OP_TRUE should also be accepted as "standard"
        self.send_txs([tx, txns_send_to_p2sh32[0], txns_send_to_p2sh32[1], txns_op_true[1]])
        expected_hashes = {tx.hash, txns_send_to_p2sh32[0].hash, txns_send_to_p2sh32[1].hash, txns_op_true[1].hash}
        assert_equal(expected_hashes, set(node.getrawmempool()))

        # Mine 1 block to confirm the mempool
        blockhashes += node.generatetoaddress(1, self.addr_cashaddr)
        assert_equal(blockhashes[-1], node.getbestblockhash())

        assert not node.getrawmempool()  # Ensure mempool is indeed empty

        # Ensure the txns that spend from p2sh32 and the one that sends to p2sh32 are both indeed in the block
        block = FromHex(CBlock(), node.getblock(blockhashes[-1], False))
        tx_hashes = set()
        for i in range(1, len(block.vtx)):
            tx_i = block.vtx[i]
            tx_i.calc_sha256()
            tx_hashes.add(tx_i.hash)
        assert_equal(tx_hashes, expected_hashes)

        # Sanity check
        assert_equal(block.vtx[0].vout[0].scriptPubKey.hex(), block16.vtx[0].vout[0].scriptPubKey.hex())

        # Next, try to mine the remaining p2sh-puzzle-soling-only tx and confirm that the block is rejected
        # after activation (blk-bad-inputs)
        block = self.create_block(prev_block=FromHex(CBlock(), node.getblock(blockhashes[-1], False)),
                                  height=node.getblockchaininfo()["blocks"] + 1,
                                  txns=[txns_hash_puzzle_only[1]])

        # Reject reason should be "blk-bad-inputs". See issue #2258
        self.send_blocks([block], success=False, reject_reason='Script Error: Operation not valid with the current stack size')
        assert_equal(blockhashes[-1], node.getbestblockhash())

        # Do the same for the OP_FALSE txn (worked pre-activation, fails post-activation)
        block = self.create_block(prev_block=FromHex(CBlock(), node.getblock(blockhashes[-1], False)),
                                  height=node.getblockchaininfo()["blocks"] + 1,
                                  txns=[txns_op_false[1]])
        # Reject reason should be "blk-bad-inputs". See issue #2258
        self.send_blocks([block], success=False, reject_reason='Script Error: Script evaluated without error but finished with a false/empty top stack element')
        assert_equal(blockhashes[-1], node.getbestblockhash())

        # Do each of the four p2sh32 segwit txns -- they fail post-activation because they don't get the segwit
        # recovery exemption that p2sh20 gets, so they get evaluated and they don't leave a clean stack and/or
        # they leave a false stack top.
        for txn_segwit in txns_spend_segwit[1]:
            block = self.create_block(prev_block=FromHex(CBlock(), node.getblock(blockhashes[-1], False)),
                                      height=node.getblockchaininfo()["blocks"] + 1,
                                      txns=[txn_segwit])
            # reject reason should be 'blk-bad-inputs', see issue #2258
            self.send_blocks([block], success=False, reject_reason='Script Error: ')
            assert_equal(blockhashes[-1], node.getbestblockhash())

        # Also try and spend the same above txns to mempool-only as a sanity check
        # 1. The puzzle-solver fails because it actually evaluates the script but there are not enough items on the
        #    stack for what the script is trying to do (which is verify sigs).
        reject_reason_bad_stack_ops = ('mandatory-script-verify-flag-failed (Operation not valid with the current stack'
                                       ' size)')
        self.send_txs([txns_hash_puzzle_only[1]], success=False, reject_reason=reject_reason_bad_stack_ops)
        # 2. Do the same for OP_FALSE which should also be rejected from mempool because it has a false stack top.
        reject_reason_false_stack_top = ('mandatory-script-verify-flag-failed (Script evaluated without error but'
                                         ' finished with a false/empty top stack element)')
        self.send_txs([txns_op_false[1]], success=False, reject_reason=reject_reason_false_stack_top)
        # 3. Do each of the four p2sh32 segwit txns; they fail at mempool level as well due to non-clean stack and/or
        #    false.
        for txn_segwit in txns_spend_segwit[1]:
            script_sig = txn_segwit.vin[0].scriptSig
            if 5 >= len(script_sig) == script_sig[2] + 3:
                # The shorter segwit scripts leave a "false" on the stack top
                reject_reason = reject_reason_false_stack_top
            else:
                # The longer segwit scripts don't leave "false" on the stack top, but they leave more than 1 item
                # which violates the BCH "clean stack" rule
                reject_reason = 'non-mandatory-script-verify-flag (P2SH script evaluation of script does not result in a clean stack)'
            self.send_txs([txn_segwit], success=False, reject_reason=reject_reason)

    @staticmethod
    def create_segwit_address_spk_and_redeem_script(case: int) -> SegWit:
        """Returns a segwit (address, scriptPubKey, redeemScript) tuple.  Param `case` should be an integer from 0-3"""
        assert 0 <= case <= 3
        if case == 0:
            # Spending from a P2SH-P2WPKH coin,
            #   txhash:a45698363249312f8d3d93676aa714be59b0bd758e62fa054fb1ea6218480691
            redeem_script = bytes.fromhex('0014fcf9969ce1c98a135ed293719721fb69f0b686cb')
        elif case == 1:
            # Spending from a P2SH-P2WSH coin,
            #   txhash:6b536caf727ccd02c395a1d00b752098ec96e8ec46c96bee8582be6b5060fa2f
            redeem_script = bytes.fromhex('0020fc8b08ed636cb23afcb425ff260b3abd03380a2333b54cfa5d51ac52d803baf4')
        elif case == 2:
            # Short version 1 witness program
            redeem_script = bytes.fromhex('51020000')
        else:
            # Short version 3 witness program
            redeem_script = bytes.fromhex('53020080')
        script_hash = hash256(redeem_script)
        address = cashaddr.encode("bchreg", cashaddr.SCRIPT_TYPE, script_hash)
        spk = CScript([OP_HASH256, script_hash, OP_EQUAL])

        return SegWit(address, spk, redeem_script)

    def update_utxos(self, spend_tx: CTransaction):
        """Updates self.utxos with the effects of spend_tx

        Deletes spent utxos, creates new UTXOs for spend_tx.vout"""
        i = 0
        spent_ins = set()
        for inp in spend_tx.vin:
            spent_ins.add((inp.prevout.hash, inp.prevout.n))
        # Delete spends
        while i < len(self.utxos):
            outpt, txout = self.utxos[i]
            if (outpt.hash, outpt.n) in spent_ins:
                del self.utxos[i]
                continue
            i += 1
        # Update new unspents
        spend_tx.calc_sha256()
        for i in range(len(spend_tx.vout)):
            txout = spend_tx.vout[i]
            if txout.nValue <= 0 or not txout.scriptPubKey or txout.scriptPubKey[0] == OP_RETURN:
                # Skip empty, non-value, or OP_RETURN outputs
                continue
            self.utxos.append(UTXO(COutPoint(spend_tx.sha256, i), txout))

    def create_tx(self, inputs: List[UTXO], outputs: List[CTxOut], *, sign=True, sigtype='schnorr'):
        """Assumption: all inputs owned by self.priv_key"""
        tx = CTransaction()
        total_value = 0
        utxos = []
        for outpt, txout in inputs:
            utxos.append(txout)
            total_value += txout.nValue
            tx.vin.append(CTxIn(outpt))
        for out in outputs:
            total_value -= out.nValue
            assert total_value >= 0
            tx.vout.append(out)
        if sign:
            hashtype = SIGHASH_ALL | SIGHASH_FORKID
            for i in range(len(tx.vin)):
                inp = tx.vin[i]
                utxo = utxos[i]
                # Sign the transaction
                hashbyte = bytes([hashtype & 0xff])
                redeem_script = self.redeem_scripts.get(utxo.scriptPubKey)
                scriptcode = redeem_script or utxo.scriptPubKey
                sighash = SignatureHashForkId(scriptcode, tx, i, hashtype, utxo.nValue)
                txsig = b''
                if sigtype == 'schnorr':
                    txsig = schnorr.sign(privkey, sighash) + hashbyte
                elif sigtype == 'ecdsa':
                    txsig = self.priv_key.sign(sighash) + hashbyte
                pushes = [txsig, self.priv_key.get_pubkey()]
                if redeem_script is not None:
                    pushes.append(redeem_script)
                inp.scriptSig = CScript(pushes)
        tx.rehash()
        return tx

    @staticmethod
    def create_block(prev_block: CBlock, height: int, *, nTime: int = None, txns=None) -> CBlock:
        if prev_block.sha256 is None:
            prev_block.rehash()
        assert prev_block.sha256 is not None  # Satisfy linter with this assertion
        prev_block_hash = prev_block.sha256
        block_time = prev_block.nTime + 1 if nTime is None else nTime
        # First create the coinbase (pays out to OP_TRUE)
        coinbase = create_coinbase(height)

        txns = txns or []
        block = create_block(prev_block_hash, coinbase, block_time, txns=txns)
        block.solve()
        return block

    def bootstrap_p2p(self):
        """Add a P2P connection to the node."""
        self.p2p = P2PDataStore()
        self.connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], self.p2p)
        self.p2p.add_connection(self.connection)
        NetworkThread().start()
        self.p2p.wait_for_verack()
        assert(self.p2p.connection.state == "connected")

    def reconnect_p2p(self):
        self.connection.handle_close()
        self.bootstrap_p2p()

    def send_txs(self, txs, success=True, reject_reason=None, reconnect=False):
        """Sends txns to test node. Syncs and verifies that txns are in mempool

        Call with success = False if the txns should be rejected."""
        self.p2p.send_txs_and_test(txs, self.nodes[0], success=success, expect_ban=reconnect,
                                            reject_reason=reject_reason)
        if reconnect:
            self.reconnect_p2p()

    def send_blocks(self, blocks, success=True, reject_reason=None,
                    request_block=True, reconnect=False, timeout=60):
        """Sends blocks to test node. Syncs and verifies that tip has advanced to most recent block.

        Call with success = False if the tip shouldn't advance to the most recent block."""
        self.p2p.send_blocks_and_test(blocks, self.nodes[0], success=success,
                                               reject_reason=reject_reason, request_block=request_block,
                                               timeout=timeout, expect_disconnect=reconnect)
        if reconnect:
            self.reconnect_p2p()


if __name__ == '__main__':
    P2SH32Test().main()

from test_framework.util import standardFlags
# Create a convenient function for an interactive python debugging session
def Test():
    t = P2SH32Test()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
    }
    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
