#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


# Test Schnorr signatures.
# This test first compares the signatures created in Python with those created by the bitcoind library.
from test_framework.script import *
from test_framework.util import *
from test_framework.nodemessages import *
from test_framework.test_framework import BitcoinTestFramework
import test_framework.cashlib as cashlib
import test_framework.schnorr as schnorr
import logging
import test_framework.loginit
import array

import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"

# This is test is not about actually veryfing node behavior
# across fork but just to test how nodes behave once schnorr
# is activated. Hence just set activation time in the past so
# that node 0 and 1 would be able to use schnorr sig and drop
# transaction that come from pre-activation nodes

class PlaceHolder():
    def __init__(self, d=None):
        self.data = d

class SchnorrSigTest (BitcoinTestFramework):

    def setup_chain(self, bitcoinConfDict=None, wallets=None):
        # initialize_chain(self.options.tmpdir, bitcoinConfDict, wallets)
        # I cannot used cached because I am using mocktime
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir)

        # Now interconnect the nodes
        connect_nodes_full(self.nodes)
        # Let the framework know if the network is fully connected.
        # If not, the framework assumes this partition: (0,1) and (2,3)
        # For more complex partitions, you can't use the self.sync* member functions
        self.is_network_split = False
        self.sync_blocks()

    def pubkeySearch(self):
        print("pid: ", os.getpid())
        privkey = cashlib.randombytes(32)
        pubkey = schnorr.getpubkey(privkey, compressed=True)
        lens = array.array("Q",[0 for i in range (0,101)])
        data = 1
        while 1:
            databytes = struct.pack(">Q", data)
            sig = cashlib.signData(databytes, privkey)
            l = len(sig)
            if l == 64:
                print("data: ", data, " ", hexlify(databytes))
                print("sig: ", hexlify(sig))
                print("privkey:", hexlify(privkey))
                pdb.set_trace()
            lens[l] += 1
            data += 1
            if ((data&16383)==0):
                print(data)
                print(lens[60:])

    def basicSchnorrSigning(self):
        # First try a canned sig (taken from schnorr.py)
        privkey = bytes.fromhex("12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747")

        pubkey = schnorr.getpubkey(privkey, compressed=True)
        assert pubkey == bytes.fromhex("030b4c866585dd868a9d62348a9cd008d6a312937048fff31670e7e920cfc7a744")

        msg = b"Very deterministic message"
        msghash = hash256(msg)
        assert msghash == bytes.fromhex("5255683da567900bfd3e786ed8836a4e7763c221bf1ac20ece2a5171b9199e8a")

        sig = schnorr.sign(privkey, msghash)
        assert sig == bytes.fromhex("2c56731ac2f7a7e7f11518fc7722a166b02438924ca9d8b4d111347b81d0717571846de67ad3d913a8fdf9d8f3f73161a4c48ae81cb183b214765feb86e255ce")
        sig2 = cashlib.signHashSchnorr(privkey, msghash)
        assert sig2 == sig

        logging.info("random Schnorr signature comparison")
        # Next try random signatures
        for i in range(1, 1000):
            privkey = cashlib.randombytes(32)
            pubkey = schnorr.getpubkey(privkey, compressed=True)
            pubkey2 = cashlib.pubkey(privkey)
            assert pubkey == pubkey2

            msg = cashlib.randombytes(random.randint(0, 10000))
            hsh = cashlib.hash256(msg)

            sigpy = schnorr.sign(privkey, hsh)
            sigcashlib = cashlib.signHashSchnorr(privkey, hsh)
            assert sigpy == sigcashlib


    def run_test(self):
        self.basicSchnorrSigning()

        self.nodes[0].generate(15)
        self.sync_blocks()
        self.nodes[1].generate(15)
        self.sync_blocks()
        self.nodes[2].generate(15)
        self.sync_blocks()
        self.nodes[0].generate(100)
        self.sync_blocks()

        logging.info("Schnorr signature transaction generation and commitment")

        resultWallet = []
        alltx = []

        wallets = [self.nodes[0].listunspent(), self.nodes[1].listunspent()]
        for txcount in range(0, 2):
            inputs = [x[0] for x in wallets]
            for x in wallets:  # Remove this utxo so we don't use it in the next time around
                del x[0]
            privb58 = [self.nodes[0].dumpprivkey(inputs[0]["address"]), self.nodes[1].dumpprivkey(inputs[1]["address"])]

            privkeys = [decodeBase58(x)[1:-5] for x in privb58]
            pubkeys = [cashlib.pubkey(x) for x in privkeys]

            for doubleSpend in range(0, 2):  # Double spend this many times
                tx = CTransaction()
                for i in inputs:
                    tx.vin.append(CTxIn(COutPoint(i["txid"], i["vout"]), b"", 0xffffffff-doubleSpend))  # subtracting doubleSpend changes the tx slightly

                destPrivKey = cashlib.randombytes(32)
                destPubKey = cashlib.pubkey(destPrivKey)
                destHash = cashlib.addrbin(destPubKey)

                output = CScript([OP_DUP, OP_HASH160, destHash, OP_EQUALVERIFY, OP_CHECKSIG])

                amt = int(sum([x["amount"] for x in inputs]) * cashlib.BCH)
                tx.vout.append(CTxOut(amt, output))

                sighashtype = 0x41
                n = 0
                for i, priv in zip(inputs, privkeys):
                    sig = cashlib.signTxInputSchnorr(tx, n, i["amount"], i["scriptPubKey"], priv, sighashtype)
                    tx.vin[n].scriptSig = cashlib.spendscript(sig)  # P2PK
                    n += 1

                txhex = hexlify(tx.serialize()).decode("utf-8")
                txid = self.nodes[0].enqueuerawtransaction(txhex)

        # because enqueuerawtransaction and propagation is asynchronous we need to wait for it
        waitFor(30, lambda: self.nodes[1].getmempoolinfo()['size'] == txcount+1)
        mp = [i.getmempoolinfo() for i in self.nodes]
        assert txcount+1 == mp[0]['size'] == mp[1]['size']

        nonSchnorrBlkHash = self.nodes[0].getbestblockhash()
        self.nodes[0].generate(1)

        assert self.nodes[0].getmempoolinfo()['size'] == 0
        waitFor(30, lambda: self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash())

        # Since we doublespent the above txs, we can't be sure which succeeded (we'd have to check the blocks)
        # Instead just grab new ones from a node's wallet
        wallets = self.nodes[0].listunspent()
        resultWallet = []
        for i in wallets:
            privb58 = self.nodes[0].dumpprivkey(i["address"])
            privKey = decodeBase58(privb58)[1:-5]
            pubKey = cashlib.pubkey(privKey)
            resultWallet.append([privKey, pubKey, i["satoshi"], PlaceHolder(i['txid']), i['vout'], CScript(i["scriptPubKey"])])

        FEEINSAT = 5000
        # now spend all the new utxos again
        for spendLoop in range(0, 2):
            incomingWallet = resultWallet
            resultWallet = []

            logging.info("spend iteration %d, num transactions %d" % (spendLoop, len(incomingWallet)))
            for w in incomingWallet:
                txidHolder = PlaceHolder()
                tx = CTransaction()
                tx.vin.append(CTxIn(COutPoint(w[3].data, w[4]), b"", 0xffffffff))

                NOUTS = 10 - spendLoop*2
                if NOUTS < 0:
                    NOUTS = 1
                amtPerOut = int((w[2]-FEEINSAT)/NOUTS)
                for outIdx in range(0, NOUTS):
                    destPrivKey = cashlib.randombytes(32)
                    destPubKey = cashlib.pubkey(destPrivKey)
                    destHash = cashlib.addrbin(destPubKey)
                    output = CScript([OP_DUP, OP_HASH160, destHash, OP_EQUALVERIFY, OP_CHECKSIG])
                    tx.vout.append(CTxOut(amtPerOut, output))
                    resultWallet.append([destPrivKey, destPubKey, amtPerOut, txidHolder, outIdx, output])

                sighashtype = 0x41
                n = 0
                sig = cashlib.signTxInputSchnorr(tx, n, w[2], w[5], w[0], sighashtype)
                # In this test we only have P2PK or P2PKH type constraint scripts so the length can be used to distinguish them
                if len(w[5]) == 35:
                    tx.vin[n].scriptSig = cashlib.spendscript(sig)  # P2PK
                else:
                    tx.vin[n].scriptSig = cashlib.spendscript(sig, w[1])  # P2PKH

                txhex = hexlify(tx.serialize()).decode("utf-8")
                txid = self.nodes[1].enqueuerawtransaction(txhex)
                alltx.append(txhex)
                txidHolder.data = txid

            # because enqueuerawtransaction and propagation is asynchronous we need to wait for it
            waitFor(30, lambda: self.nodes[0].getmempoolinfo()['size'] == len(incomingWallet))
            while self.nodes[0].getmempoolinfo()['size'] != 0:
                self.nodes[0].generate(1)
            waitFor(30, lambda: self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash())

if __name__ == '__main__':
    binpath = findBitcoind()
    try:
        cashlib.init(binpath + os.sep + ".libs" + os.sep + "libbitcoincash.so")
        SchnorrSigTest().main()
    except OSError as e:
        print("Issue loading cashlib shared library.  This is expected during cross compilation since the native python will not load the .so so no error will be reported: %s" % str(e))


# Create a convenient function for an interactive python debugging session
def Test():
    t = SchnorrSigTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }

    flags = []  # ["--nocleanup", "--noshutdown"]
    if os.path.isdir("/ramdisk/test/t"):
        flags.append("--tmpdir=/ramdisk/test/t")
    binpath = findBitcoind()
    flags.append("--srcdir=%s" % binpath)
    cashlib.init(binpath + os.sep + ".libs" + os.sep + "libbitcoincash.so")

    t.main(flags, bitcoinConf, None)
