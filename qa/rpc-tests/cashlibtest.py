#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import test_framework.cashlib as cashlib
from test_framework.nodemessages import *
from test_framework.script import *

BCH = 100000000


class MyTest (BitcoinTestFramework):

    def setup_chain(self, bitcoinConfDict=None, wallets=None):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain(self.options.tmpdir, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        faulted = False
        try:
            cashlib.spendscript(OP_1)
        except AssertionError:
            faulted = True
            pass
        assert faulted, "only data in spend scripts"

        try:
            cashlib.signTxInput(b"", 0, 5, b"", b"", cashlib.SIGHASH_ALL)
        except AssertionError:
            faulted = True
            pass
        assert faulted, "not signing with bitcoin cash forkid"

        # grab inputs from 2 different full nodes and sign a single tx that spends them both
        wallets = [self.nodes[0].listunspent(), self.nodes[1].listunspent()]
        inputs = [x[0] for x in wallets]
        privb58 = [self.nodes[0].dumpprivkey(inputs[0]["address"]), self.nodes[1].dumpprivkey(inputs[1]["address"])]

        privkeys = [decodeBase58(x)[1:-5] for x in privb58]
        pubkeys = [cashlib.pubkey(x) for x in privkeys]

        tx = CTransaction()
        for i in inputs:
            tx.vin.append(CTxIn(COutPoint(i["txid"], i["vout"]), b"", 0xffffffff))

        destPrivKey = cashlib.randombytes(32)
        destPubKey = cashlib.pubkey(destPrivKey)
        destHash = cashlib.addrbin(destPubKey)

        output = CScript([OP_DUP, OP_HASH160, destHash, OP_EQUALVERIFY, OP_CHECKSIG])

        amt = int(sum([x["amount"] for x in inputs]) * BCH)
        tx.vout.append(CTxOut(amt, output))

        sighashtype = 0x41
        n = 0
        for i, priv in zip(inputs, privkeys):
            sig = cashlib.signTxInput(tx, n, i["amount"], i["scriptPubKey"], priv, sighashtype)
            tx.vin[n].scriptSig = cashlib.spendscript(sig)  # P2PK
            n += 1

        txhex = hexlify(tx.serialize()).decode("utf-8")
        txid = self.nodes[0].enqueuerawtransaction(txhex)

        assert txid == hexlify(cashlib.txid(txhex)[::-1]).decode("utf-8")

        # Now spend the created output to an anyone can spend address
        tx2 = CTransaction()
        tx2.vin.append(CTxIn(COutPoint(cashlib.txid(txhex), 0), b"", 0xffffffff))
        tx2.vout.append(CTxOut(amt, CScript([OP_1])))
        sig2 = cashlib.signTxInput(tx2, 0, amt, output, destPrivKey, sighashtype)
        tx2.vin[0].scriptSig = cashlib.spendscript(sig2, destPubKey)

        tx2id = self.nodes[0].enqueuerawtransaction(hexlify(tx2.serialize()).decode("utf-8"))
        # Check that all tx were created, and commit them
        waitFor(6, lambda: self.nodes[0].getmempoolinfo()["size"] == 2)
        blk = self.nodes[0].generate(1)
        self.sync_blocks()
        assert self.nodes[0].getmempoolinfo()["size"] == 0
        assert self.nodes[1].getmempoolinfo()["size"] == 0


if __name__ == '__main__':
    env = os.getenv("BITCOIND", None)
    if env is None:
        env = os.path.dirname(os.path.abspath(__file__))
        env = env + os.sep + ".." + os.sep + ".." + os.sep + "src" + os.sep + "bitcoind"
        env = os.path.abspath(env)
    path = os.path.dirname(env)
    try:
        cashlib.init(path + os.sep + ".libs" + os.sep + "libbitcoincash.so")
        MyTest().main()
    except OSError as e:
        print("Issue loading shared library.  This is expected during cross compilation since the native python will not load the .so: %s" % str(e))

# Create a convenient function for an interactive python debugging session


def Test():
    t = MyTest()
    bitcoinConf = {
        "debug": ["rpc", "net", "blk", "thin", "mempool", "req", "bench", "evict"],
    }

    flags = [] # ["--nocleanup", "--noshutdown"]
    if os.path.isdir("/ramdisk/test"):
        flags.append("--tmpdir=/ramdisk/test/cashlibtest")
    binpath = findBitcoind()
    flags.append("--srcdir=%s" % binpath)
    cashlib.init(binpath + os.sep + ".libs" + os.sep + "libbitcoincash.so")
    t.main(flags, bitcoinConf, None)
