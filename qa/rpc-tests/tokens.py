#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging
logging.basicConfig(format='%(asctime)s.%(levelname)s: %(message)s', level=logging.INFO, stream=sys.stdout)
from decimal import *
getcontext().prec = 8
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

BCH = Decimal(100000000)


def flatten(lst):
    ret = []
    for i in lst:
        if type(i) is list:
            ret += flatten(i)
        elif type(i) is tuple:
            ret += flatten(i)
        else:
            ret.append(i)
    return ret


class TokenTest (BitcoinTestFramework):

    def setup_chain(self, bitcoinConfDict=None, wallets=None):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        # enable opgroup enforcement
        [x.set("mining.opgroup=1") for x in self.nodes]
        self.nodes[0].generate(102)
        self.sync_blocks()

        assert_equal(self.nodes[0].getbalance(), 100)

        tokenInfo = self.nodes[0].token("new")

        addr = [self.nodes[0].getnewaddress() for x in range(0, 5)]
        addr1 = [self.nodes[1].getnewaddress() for x in range(0, 5)]

        # I need some bch over at node 1 to pay fees
        n1bchaddr = self.nodes[1].getnewaddress()
        self.nodes[0].sendtoaddress(n1bchaddr, 40)

        grpId = tokenInfo["groupIdentifier"]
        bchbal = self.nodes[0].getbalance()  # we'll use this later to ensure that group coins aren't in bch balance
        # fill a txo that can mint (the extra 1000 is for the fee)
        fundingtx = self.nodes[0].sendtoaddress(tokenInfo["controllingAddress"], Decimal(1000) / BCH)
        self.nodes[0].generate(1)
        # mint coins

        # test some bad param combinations
        try:
            self.nodes[0].token("mint")
            assert 0, "bad parameter combination"
        except JSONRPCException:
            pass
        try:
            self.nodes[0].token("mint", grpId)
            assert 0, "bad parameter combination"
        except JSONRPCException:
            pass
        try:
            self.nodes[0].token("mint", grpId, addr[0])
            assert 0, "bad parameter combination"
        except JSONRPCException:
            pass
        try:
            self.nodes[0].token("mint", grpId, addr[0], 5, addr[1])
            assert 0, "bad parameter combination"
        except JSONRPCException:
            pass

        self.nodes[0].token("mint", grpId, *flatten(zip(addr, [1000000] * 5)))
        self.nodes[0].generate(1)
        # verify that the bch balance doesn't include the group coins.  It won't be exact because of fees
        assert(self.nodes[0].getbalance() < 100 + bchbal)
        # verify that we have those many tokens
        assert(self.nodes[0].token("balance", grpId) == 1000000 * 5)
        # verify that they are in the right txos
        for a in addr:
            assert(self.nodes[0].token("balance", grpId, a) == 1000000)
        # send those tokens to node 1
        self.nodes[0].token("send", grpId, *flatten(zip(addr1, [1000] * 5)))
        self.nodes[0].generate(1)
        self.sync_blocks()
        # verify that node one has the correct tokens in the correct txos
        assert self.nodes[1].token("balance", grpId) == 5000
        for a in addr1:
            assert self.nodes[1].token("balance", grpId, a) == 1000
        assert self.nodes[0].token("balance", grpId) == (1000000 * 5) - 5000

        # Do the same as above but all unconfirmed tx

        tokenInfo = self.nodes[0].token("new")
        addr = [self.nodes[0].getnewaddress() for x in range(0, 5)]
        addr1 = [self.nodes[1].getnewaddress() for x in range(0, 5)]
        grpId = tokenInfo["groupIdentifier"]
        bchbal = self.nodes[0].getbalance()  # we'll use this later to ensure that group coins aren't in bch balance
        # fill a txo that can mint
        self.nodes[0].sendtoaddress(tokenInfo["controllingAddress"], 30)
        self.nodes[0].token("mint", grpId, *flatten(zip(addr, [1000000] * 5)))
        # verify that the bch balance doesn't include the group coins.  It won't be exact because of fees
        assert(self.nodes[0].getbalance() < 100 + bchbal - Decimal(1000000 * 5) / BCH)
        assert(self.nodes[0].token("balance", grpId) == 1000000 * 5)
        for a in addr:
            assert self.nodes[0].token("balance", grpId, a) == 1000000
        self.nodes[0].token("send", tokenInfo["groupIdentifier"], *flatten(zip(addr1, [1000] * 5)))
        wait = 0
        while self.nodes[1].token("balance", grpId) != 5000:  # tx prop may take some time
            time.sleep(.1)
            wait += .1
            assert(wait < 5)
        for a in addr1:
            assert self.nodes[1].token("balance", grpId, a) == 1000
        assert self.nodes[0].token("balance", grpId) == (1000000 * 5) - 5000
        self.nodes[0].generate(1)
        self.sync_blocks()

        tx = self.nodes[1].token("send", grpId, tokenInfo["controllingAddress"], 5000)
        wait = 0
        while self.nodes[0].token("balance", grpId, tokenInfo["controllingAddress"]) < 5000:
            time.sleep(.1)
            wait += .1
            assert wait < 5

        # test melting the exact amount
        bal1 = self.nodes[0].token("balance", grpId)
        tx = self.nodes[0].token("melt", grpId, 5000)
        self.nodes[0].generate(1)
        assert bal1 == self.nodes[0].token("balance", grpId) + 5000

        # test melting tokens I have but aren't in the grp address
        try:
            tx1 = self.nodes[0].token("melt", grpId, 1234567)
            assert 0, "melt should have failed"
        except JSONRPCException:
            pass

        # test melting fewer than I have in the controlling address
        tx2 = self.nodes[0].token("send", grpId, tokenInfo["controllingAddress"], 3000000)
        tx3 = self.nodes[0].token("melt", grpId, 1234567)
        assert bal1 == self.nodes[0].token("balance", grpId) + 5000 + 1234567

        # test single mint
        a0 = self.nodes[0].getnewaddress()
        a1 = self.nodes[1].getnewaddress()
        sm = self.nodes[1].token("singlemint", a0, 1000, a1, 1000)
        smGrp = sm["groupIdentifier"]
        self.nodes[1].token("send", smGrp, a0, 100)
        self.nodes[1].generate(1)
        self.sync_blocks()
        time.sleep(1)  # strangely blocks are synced but wallet is not updated without a delay
        assert 1100 == self.nodes[0].token("balance", smGrp)
        assert 1100 == self.nodes[0].token("balance", smGrp, a0)
        assert 900 == self.nodes[1].token("balance", smGrp)
        a2 = self.nodes[0].getnewaddress()
        self.nodes[1].token("send", smGrp, a2, 234)
        count = 0
        while self.nodes[0].token("balance", smGrp, a2) != 234:
            time.sleep(1)
            count += 1
            if count > 10:
                assert 0, "single-mint zero conf token balance did not sync"
        assert 1334 == self.nodes[0].token("balance", smGrp)


if __name__ == '__main__':
    TokenTest().main()

# Create a convenient function for an interactive python debugging session


def Test():
    t = TokenTest()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }

    # you may want these additional flags:
    # "--srcdir=<out-of-source-build-dir>/debug/src"
    args = ["--nocleanup", "--noshutdown"]
    if os.path.isdir("/ramdisk/test"):
        args.append("--tmpdir=/ramdisk/test")
    t.main(args, bitcoinConf, None)
