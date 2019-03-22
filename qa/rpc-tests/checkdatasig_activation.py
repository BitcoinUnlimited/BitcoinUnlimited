#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
This test checks activation of OP_CHECKDATASIG
"""
import os, random, string
from test_framework.util import findBitcoind, expectException, JSONRPCException
import test_framework.loginit
from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import waitFor, satoshi_round, assert_equal, assert_raises_rpc_error, start_node, standardFlags
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
from test_framework.script import *

# far into the future
NOV152018_START_TIME = 2000000000

# Error due to invalid opcodes
BAD_OPCODE_ERROR = b'mandatory-script-verify-flag-failed (Opcode missing or not understood)'
RPC_BAD_OPCODE_ERROR = "16: " + \
    BAD_OPCODE_ERROR.decode("utf-8")


class PreviousSpendableOutput():
    def __init__(self, tx=CTransaction(), n=-1):
        self.tx = tx
        self.n = n  # the output we're spending


class CheckDataSigActivationTest(ComparisonTestFramework):
    def __init__(self):
        super().__init__()
        self.num_nodes = 1
        self.setup_clean_chain = True

    def setup_network(self):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir,
                                 ['-debug', '-whitelist=127.0.0.1',
                                  "-consensus.forkNov2018Time=%d" % NOV152018_START_TIME]))
        self.is_network_split = False

    def create_checkdatasig_tx(self, count):
        node = self.nodes[0]
        utxos = node.listunspent()
        assert(len(utxos) > 0)
        utxo = utxos[0]
        tx = CTransaction()
        value = int(satoshi_round(utxo["amount"]) * COIN) // count
        tx.vin = [CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]))]
        tx.vout = []

        # test using the signdata rpc
        addr = node.getnewaddress()
        msg = "Joo Janta 200 Super-Chromatic Peril Sensitive Sunglasses"
        message = bytearray(msg.encode())
        rpc = node.signdata(addr, "string", msg,"verbose")

        signature = bytearray.fromhex(rpc["signature"])
        pubkey = bytearray.fromhex(rpc["pubkey"])

        # test with hard-coded values
        #signature = bytearray.fromhex(
        #    '30440220256c12175e809381f97637933ed6ab97737d263eaaebca6add21bced67fd12a402205ce29ecc1369d6fc1b51977ed38faaf41119e3be1d7edfafd7cfaf0b6061bd07')
        #pubkey = bytearray.fromhex(
        #    '038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508')

        for _ in range(count):
            tx.vout.append(CTxOut(value, CScript(
                [signature, message, pubkey, OP_CHECKDATASIG])))
        tx.vout[0].nValue -= min(value/100, 1000) #node.calculate_fee(tx.toHex())
        tx_signed = node.signrawtransaction(tx.toHex())["hex"]
        return tx_signed

    def testDatasigRpc(self):
        node = self.nodes[0]
        # bad address
        expectException(lambda: node.signdata("bad","string", "foo"), JSONRPCException)
        # address I can't sign
        expectException(lambda: node.signdata('bchreg:qq0ndugr327fwxucntduem4t3jvvmjtdevmdry0lqc',"string", "foo"), JSONRPCException)
        addr = node.getnewaddress()
        # bad message format
        expectException(lambda: node.signdata(addr,"bad", "foo"), JSONRPCException)
        expectException(lambda: node.signdata(addr,"hex", "zzbad"), JSONRPCException)
        expectException(lambda: node.signdata(addr,"hash", "ba0d"), JSONRPCException) # its hex but wrong length
        expectException(lambda: node.signdata(addr,"hash", "z"*32), JSONRPCException) # not hex correct length

        # check same sig for same input of different format (works because using rfc6979 deterministic sigs)
        s = ''.join(random.choice(string.ascii_uppercase + string.digits) for _ in range(20))
        sig0 = node.signdata(addr,"string", s)
        sig1 = node.signdata(addr,"hex", hexlify(s.encode()).decode())
        sig2 = node.signdata(addr,"hash", hexlify(sha256(s.encode())).decode())
        assert_equal(sig0, sig1)
        assert_equal(sig0, sig2)

        # good signatures
        node.importprivatekeys("no-rescan", 'cU4WAhpniFvwT8Z13MjNyE1tkzp8n7wDPwwe8WzqqBAejZXq948J')
        sig = node.signdata('bchreg:qq3srvg7hrzf9wu5h33du5l7n3fpx7jw5gdhhk390u',"string", "foo")
        assert_equal(sig, '3045022100C4EB26D78AE898C72EF959A96DE51B423563E384A72B580493C9195F8811D36602201CC6561EAFAC7C9BC55B80D2605C1EBF9ACAB9AD80F0A08247370D2ED2B9408A')
        sig2 = node.signdata('bchreg:qq3srvg7hrzf9wu5h33du5l7n3fpx7jw5gdhhk390u',"hex", "0102030405060708090a")
        assert_equal(sig2, '304502210085626204B90AF4B62546037A89786B279BBB59D003A6D767630386B173905C8A0220740EC7B0743E94C4B9427FCCE1E1CB327CB0605501BF447E697E0206E14FBE58')

        # If I use the "hash" format and calculate the sha256 myself, it ought to create the same signature as "signdata"
        assert_equal(sig, node.signdata('bchreg:qq3srvg7hrzf9wu5h33du5l7n3fpx7jw5gdhhk390u',"hash", hexlify(sha256(b"foo")).decode()))


    def run_test(self):
        self.testDatasigRpc()
        self.test = TestManager(self, self.options.tmpdir)
        self.test.add_all_connections(self.nodes)
        # Start up network handling in another thread
        NetworkThread().start()
        self.test.run()

    def get_tests(self):
        node = self.nodes[0]

        # First, we generate some coins to spend.
        node.generate(125)

        # Create various outputs using the OP_CHECKDATASIG
        # to check for activation.
        tx_hex = self.create_checkdatasig_tx(25)
        txid = node.sendrawtransaction(tx_hex)
        assert(txid in set(node.getrawmempool()))

        node.generate(1)
        assert(txid not in set(node.getrawmempool()))

        # register the spendable outputs.
        tx = FromHex(CTransaction(), tx_hex)
        tx.rehash()
        spendable_checkdatasigs = [PreviousSpendableOutput(tx, i)
                                   for i in range(len(tx.vout))]

        def spend_checkdatasig():
            outpoint = spendable_checkdatasigs.pop()
            out = outpoint.tx.vout[outpoint.n]
            tx = CTransaction()
            tx.vin = [CTxIn(COutPoint(outpoint.tx.sha256, outpoint.n))]
            tx.vout = [CTxOut(out.nValue, CScript([])),
                       CTxOut(0, CScript([random.getrandbits(800), OP_RETURN]))]
            tx.vout[0].nValue -= min(tx.vout[0].nValue/100, 1000) # node.calculate_fee(tx)
            tx.rehash()
            return tx

        # Check that transactions using checkdatasig are not accepted yet.
        logging.info("Try to use the checkdatasig opcodes before activation")

        tx0 = spend_checkdatasig()
        tx0_hex = ToHex(tx0)
        assert_raises_rpc_error(-26, RPC_BAD_OPCODE_ERROR,
                                node.sendrawtransaction, tx0_hex)

        # Push MTP forward just before activation.
        logging.info("Pushing MTP just before the activation and check again")
        node.setmocktime(NOV152018_START_TIME)

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
            b = next_block(NOV152018_START_TIME + i - 1)
            yield accepted(b)

        # Check again just before the activation time
        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     NOV152018_START_TIME - 1)
        assert_raises_rpc_error(-26, RPC_BAD_OPCODE_ERROR,
                                node.sendrawtransaction, tx0_hex)

        def add_tx(block, tx):
            block.vtx.append(tx)
            block.hashMerkleRoot = block.calc_merkle_root()
            block.solve()

        b = next_block(NOV152018_START_TIME + 6)
        add_tx(b, tx0)
        yield rejected(b, RejectResult(16, b'bad-blk-signatures'))

        logging.info("Activates checkdatasig")
        fork_block = next_block(NOV152018_START_TIME + 6)
        yield accepted(fork_block)

        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     NOV152018_START_TIME)

        tx0id = node.sendrawtransaction(tx0_hex)
        assert(tx0id in set(node.getrawmempool()))

        # Transactions can also be included in blocks.
        nov152018forkblock = next_block(NOV152018_START_TIME + 7)
        add_tx(nov152018forkblock, tx0)
        yield accepted(nov152018forkblock)

        logging.info("Cause a reorg that deactivate the checkdatasig opcodes")

        # Invalidate the checkdatasig block, ensure tx0 gets back to the mempool.
        assert(tx0id not in set(node.getrawmempool()))

        node.invalidateblock(format(nov152018forkblock.sha256, 'x'))
        waitFor(3, lambda: tx0id in set(node.getrawmempool()), "Transaction shoud be included in the mempool")

        node.invalidateblock(format(fork_block.sha256, 'x'))
        waitFor(3, lambda: tx0id not in set(node.getrawmempool()), "Transaction should not be included in the memopool")


if __name__ == '__main__':
    CheckDataSigActivationTest().main()

# Create a convenient function for an interactive python debugging session
def Test():
    t = CheckDataSigActivationTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["blk", "mempool", "net", "req"],
        "logtimemicros": 1
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
