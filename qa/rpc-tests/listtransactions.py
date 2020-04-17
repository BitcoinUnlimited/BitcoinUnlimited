#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
# Exercise the listtransactions API
import pdb
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import CTransaction, COIN
from io import BytesIO


def txFromHex(hexstring):
    return CTransaction().deserialize(hexstring)


class ListTransactionsTest(BitcoinTestFramework):

    def setup_nodes(self):
        enable_mocktime()
        return start_nodes(4, self.options.tmpdir)

    def run_test(self):
        self.test_listtransactionsfrom()
        self.test_listtransactions()

    def test_listtransactionsfrom(self):
        # Simple send, 0 to 1:
        self.sync_all()
        tmp = self.nodes[2].listtransactionsfrom("*", 10000, 0)
        curpos = len(tmp)
        txid = self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), 0.1)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Basic positive test
        tmp = self.nodes[2].listtransactionsfrom("*", 1, curpos)
        assert len(tmp) == 1
        assert tmp[0]["txid"] == txid

        tmp = self.nodes[2].listtransactionsfrom("*", 10, curpos)
        assert len(tmp) == 1

        # Negative tests
        # test beyond end of tx list
        tmp = self.nodes[2].listtransactionsfrom("*", 100, curpos + 100)
        assert(len(tmp) == 0)

        # test bad input values
        try:
            tmp = self.nodes[2].listtransactionsfrom("*", -1, curpos)
            assert 0
        except JSONRPCException:
            pass
        try:
            tmp = self.nodes[2].listtransactionsfrom("*", 100, -1)
            assert 0
        except JSONRPCException:
            pass

        # test multiple rows
        curpos += 1

        txidsA = [self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), 0.2), self.nodes[2].sendtoaddress(
            self.nodes[3].getnewaddress(), 0.3), self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), 0.4)]
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_blocks()

        tmp = self.nodes[2].listtransactionsfrom("*", 100, curpos)
        assert len(tmp) == 3
        assert tmp[0]["txid"] == txidsA[0]
        assert tmp[1]["txid"] == txidsA[1]
        assert tmp[2]["txid"] == txidsA[2]

        txidsB = [self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), 0.5), self.nodes[2].sendtoaddress(
            self.nodes[3].getnewaddress(), 0.6), self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), 0.7)]

        tmp = self.nodes[2].listtransactionsfrom("*", 100, curpos)
        assert len(tmp) == 6
        assert tmp[0]["txid"] == txidsA[0]
        assert tmp[1]["txid"] == txidsA[1]
        assert tmp[2]["txid"] == txidsA[2]
        assert tmp[3]["txid"] == txidsB[0]
        assert tmp[4]["txid"] == txidsB[1]
        assert tmp[5]["txid"] == txidsB[2]

        # test when I advance to the end, I get nothing
        curpos += len(tmp)
        tmp = self.nodes[2].listtransactionsfrom("*", 100, curpos)
        assert tmp == []

    def test_listtransactions(self):
        # Simple send, 0 to 1:
        txid = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
        self.sync_all()
        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid},
                            {"category": "send", "account": "", "amount": Decimal("-0.1"), "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid},
                            {"category": "receive", "account": "", "amount": Decimal("0.1"), "confirmations": 0})
        # mine a block, confirmations should change:
        self.nodes[0].generate(1)
        self.sync_blocks()
        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid},
                            {"category": "send", "account": "", "amount": Decimal("-0.1"), "confirmations": 1})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid},
                            {"category": "receive", "account": "", "amount": Decimal("0.1"), "confirmations": 1})

        # send-to-self:
        txid = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.2)
        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid, "category": "send"},
                            {"amount": Decimal("-0.2")})
        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid, "category": "receive"},
                            {"amount": Decimal("0.2")})

        # sendmany from node1: twice to self, twice to node2:
        send_to = {self.nodes[0].getnewaddress(): 0.11,
                   self.nodes[1].getnewaddress(): 0.22,
                   self.nodes[0].getaccountaddress("from1"): 0.33,
                   self.nodes[1].getaccountaddress("toself"): 0.44}
        txid = self.nodes[1].sendmany("", send_to)
        self.sync_all()
        assert_array_result(self.nodes[1].listtransactions(),
                            {"category": "send", "amount": Decimal("-0.11")},
                            {"txid": txid})
        assert_array_result(self.nodes[0].listtransactions(),
                            {"category": "receive", "amount": Decimal("0.11")},
                            {"txid": txid})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"category": "send", "amount": Decimal("-0.22")},
                            {"txid": txid})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"category": "receive", "amount": Decimal("0.22")},
                            {"txid": txid})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"category": "send", "amount": Decimal("-0.33")},
                            {"txid": txid})
        assert_array_result(self.nodes[0].listtransactions(),
                            {"category": "receive", "amount": Decimal("0.33")},
                            {"txid": txid, "account": "from1"})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"category": "send", "amount": Decimal("-0.44")},
                            {"txid": txid, "account": ""})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"category": "receive", "amount": Decimal("0.44")},
                            {"txid": txid, "account": "toself"})

        multisig = self.nodes[1].createmultisig(1, [self.nodes[1].getnewaddress()])
        self.nodes[0].importaddress(multisig["redeemScript"], "watchonly", False, True)
        txid = self.nodes[1].sendtoaddress(multisig["address"], 0.1)
        self.nodes[1].generate(1)
        self.sync_blocks()
        assert(len(self.nodes[0].listtransactions("watchonly", 100, 0, False)) == 0)
        assert_array_result(self.nodes[0].listtransactions("watchonly", 100, 0, True),
                            {"category": "receive", "amount": Decimal("0.1")},
                            {"txid": txid, "account": "watchonly"})


if __name__ == '__main__':
    ListTransactionsTest().main(None, {
        "debug": ["all"],
    })


def Test():
    t = ListTransactionsTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["all","-libevent"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }
    flags = standardFlags()
    # flags.append("--tmpdir=/tmp/test")
    t.main(flags, bitcoinConf, None)
