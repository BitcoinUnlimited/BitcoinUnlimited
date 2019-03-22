#!/usr/bin/env python3
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# parts taken from weirdtx.py

import test_framework.loginit

import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.blocktools import *
from test_framework.script import *
from test_framework.key import *
from binascii import unhexlify, hexlify

def Hash256Puzzle(s):
    if type(s) is str:
        s = s.encode()
    ret = CScript([OP_HASH256, hash256(s), OP_EQUAL])
    return ret


def MatchString(s):
    if type(s) is str:
        s = s.encode()
    ret = CScript([s, OP_EQUAL])
    return ret


def p2pkh_list(addr):
    return [OP_DUP, OP_HASH160, bitcoinAddress2bin(addr), OP_EQUALVERIFY, OP_CHECKSIG]


def SignWithAorB(twoAddrs):
    ret = CScript([OP_IF] + p2pkh_list(twoAddrs[0]) + [OP_ELSE] + p2pkh_list(twoAddrs[1]) + [OP_ENDIF])
    return ret


class SigHashMatchTest(BitcoinTestFramework):
    def setup_chain(self, bitcoinConfDict, wallets=None):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        # generate enough blocks so that nodes[0] has a balance
        self.sync_blocks()
        self.nodes[0].generate(150)
        self.sync_blocks()

        unspents = self.nodes[0].listunspent()
        unspents.sort(key=lambda x: x["amount"], reverse=False)
        utxo = unspents.pop()
        amt = utxo["amount"]
        addr = utxo["address"]
        outp = {"dummy" : amt - decimal.Decimal(.0001)}  # give some fee
        txn = CTransaction().deserialize(createrawtransaction([utxo], outp, p2pkh))

        # create signature manually using txn.SignatureHash() calculation
        # plus the new signdata RPC call, append the sighashbyte and make sure
        # it is accepted by the node.

        privkey = self.nodes[0].dumpprivkey(addr)
        key = CECKey()
        key.set_secretbytes(decodeBase58(privkey)[1:-5])
        key.set_compressed(True)
        pub = key.get_pubkey()

        sighashcode = CScript([pub, OP_CHECKSIG])
        sighash = txn.SignatureHash(0, bytes(sighashcode),
                                    int((amt)*100000000))
        txn_mansig = unhexlify(self.nodes[0].signdata(addr, "hash",
                                            hexlify(sighash).decode("ascii")))
        fullsig = txn_mansig+b"\x41"
        txn.vin[0].scriptSig = CScript([fullsig])
        txid = self.nodes[0].sendrawtransaction(txn.toHex())
        assert len(txid) == 64

if __name__ == '__main__':
    SigHashMatchTest().main(bitcoinConfDict = {"usecashaddr" : 0})

# Create a convenient function for an interactive python debugging session


def Test():
    t = SigHashMatchTest()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000,  # we don't want any transactions rejected due to insufficient fees...
        "usecashaddr" : False
    }

    # you may want these additional flags:
    # "--srcdir=<out-of-source-build-dir>/debug/src"
    # "--tmppfx=/ramdisk/test"
    flags = []  # "--nocleanup", "--noshutdown"
    if os.path.isdir("/ramdisk/test"):  # execution is much faster if a ramdisk is used
        flags.append("--tmppfx=/ramdisk/test")

    t.main(flags, bitcoinConf, None)
