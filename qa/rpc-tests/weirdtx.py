#!/usr/bin/env python3
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
# This is a template to make creating new QA tests easy.
# You can also use this template to quickly start and connect a few regtest nodes.

import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.blocktools import *
from test_framework.script import *


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


class MyTest (BitcoinTestFramework):

    def setup_chain(self, bitcoinConfDict=None, wallets=None):
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

        wallet = self.nodes[0].listunspent()
        wallet.sort(key=lambda x: x["amount"], reverse=False)

        data = b"flying is throwing yourself against the ground and missing"
        print(self.nodes[0].getbalance())
        # try some nonstandard tx
        utxo = wallet.pop()
        amt = utxo["amount"]
        outp = {data: amt - decimal.Decimal(.0001)}  # give some fee
        txn = createrawtransaction([utxo], outp, MatchString)
        signedtxn = self.nodes[0].signrawtransaction(txn)
        txid = self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "NONSTANDARD")
        assert len(txid) == 64
        self.nodes[0].generate(1)
        self.sync_blocks()
        assert self.nodes[0].getmempoolinfo()["size"] == 0
        print([x.getbalance() for x in self.nodes])

        # spend the puzzle
        addr = self.nodes[1].getaddressforms(self.nodes[1].getnewaddress())["legacy"]
        solveScript = CScript([data])
        txn2 = createrawtransaction([{'txid': txid, 'vout': 0, 'sig': solveScript}],
                                    {addr: amt - decimal.Decimal(.0002)}, p2pkh)
        txid2 = self.nodes[1].sendrawtransaction(txn2, False, "NONSTANDARD")
        assert len(txid2) == 64
        self.nodes[1].generate(1)
        self.sync_blocks()
        assert self.nodes[1].getmempoolinfo()["size"] == 0
        print([x.getbalance() for x in self.nodes])

        # ---

        addr0 = self.nodes[1].getaddressforms(self.nodes[0].getnewaddress())["legacy"]
        addr1 = self.nodes[1].getaddressforms(self.nodes[1].getnewaddress())["legacy"]

        utxo = wallet.pop()
        amt = utxo["amount"]
        outp = [((addr0, addr1), amt - decimal.Decimal(.0001))]  # give some fee
        txn = createrawtransaction([utxo], outp, SignWithAorB)
        signedtxn = self.nodes[0].signrawtransaction(txn)
        txid = self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "NONSTANDARD")
        assert len(txid) == 64
        self.nodes[0].generate(1)

        # bitcoind is not sophisticated enough to spend this ^

        # ---

        utxo = wallet.pop()
        amt = utxo["amount"]
        outp = {data: amt - decimal.Decimal(.0001)}  # give some fee
        txn = createrawtransaction([utxo], outp, Hash256Puzzle)
        signedtxn = self.nodes[0].signrawtransaction(txn)
        txid = self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "NONSTANDARD")
        assert len(txid) == 64
        self.nodes[0].generate(1)
        self.sync_blocks()

        # example of stopping and restarting the nodes
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.nodes = start_nodes(2, self.options.tmpdir, [[], [], [], []])
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_blocks()
        print(self.nodes[0].getinfo())

        # spend the puzzle
        addr = self.nodes[1].getaddressforms(self.nodes[1].getnewaddress())["legacy"]
        solveScript = CScript([data])
        txn2 = createrawtransaction([{'txid': txid, 'vout': 0, 'sig': solveScript}],
                                    {addr: amt - decimal.Decimal(.0002)}, p2pkh)
        txid2 = self.nodes[1].sendrawtransaction(txn2, False, "NONSTANDARD")
        assert len(txid2) == 64
        self.nodes[1].generate(1)
        self.sync_blocks()
        assert len(self.nodes[1].listunspent()) == 2


if __name__ == '__main__':
    MyTest().main()

# Create a convenient function for an interactive python debugging session


def Test():
    t = MyTest()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }

    # you may want these additional flags:
    # "--srcdir=<out-of-source-build-dir>/debug/src"
    # "--tmppfx=/ramdisk/test"
    flags = []  # "--nocleanup", "--noshutdown"
    if os.path.isdir("/ramdisk/test"):  # execution is much faster if a ramdisk is used
        flags.append("--tmppfx=/ramdisk/test")

    t.main(flags, bitcoinConf, None)
