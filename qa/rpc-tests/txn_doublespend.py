#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
#
# Test proper accounting with a double-spend conflict
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class TxnDoubleSpendTest(BitcoinTestFramework):

    def add_options(self, parser):
        parser.add_option("--mineblock", dest="mine_block", default=False, action="store_true",
                          help="Test double-spend of 1-confirmed transaction")

    def setup_network(self):
        # Start with split network:
        return super(TxnDoubleSpendTest, self).setup_network(True)

    def run_test(self):
        # All nodes should start with 25 mined blocks:
        starting_balance = COINBASE_REWARD*25
        for i in range(4):
            assert_equal(self.nodes[i].getbalance(), starting_balance)
            self.nodes[i].getnewaddress("")  # bug workaround, coins generated assigned to first getnewaddress!

        startHeight = self.nodes[2].getblockcount()

        # Coins are sent to node1_address
        node1_address = self.nodes[1].getnewaddress("from0")

        # First: use raw transaction API to send 1240 BTC to node1_address,
        # but don't broadcast:

        unspent = self.nodes[0].listunspent()

        doublespend_fee = Decimal('-.02')
        doublespend_amt = unspent[0]["amount"] + unspent[1]["amount"] - Decimal("1.0")
        rawtx_input_0 = {}
        rawtx_input_0["txid"] = unspent[0]["txid"]
        rawtx_input_0["vout"] = unspent[0]["vout"]
        rawtx_input_1 = {}
        rawtx_input_1["txid"] = unspent[1]["txid"]
        rawtx_input_1["vout"] = unspent[1]["vout"]
        inputs = [rawtx_input_0, rawtx_input_1]
        change_address = self.nodes[0].getnewaddress()
        outputs = {}
        outputs[node1_address] = doublespend_amt
        outputs[change_address] = Decimal("1.0") + doublespend_fee
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        doublespend = self.nodes[0].signrawtransaction(rawtx)
        assert_equal(doublespend["complete"], True)

        # Create two spends using 1 coin each
        txid1 = self.nodes[0].sendfrom("", node1_address, starting_balance + doublespend_fee, 0)

        # Have node0 mine a block:
        if (self.options.mine_block):
            self.nodes[0].generate(1)
            sync_blocks(self.nodes[0:2])

        tx1 = self.nodes[0].gettransaction(txid1)

        assert_equal(self.nodes[2].getmempoolinfo()["size"], 0)
        assert_equal(startHeight, self.nodes[2].getblockcount())

        # Now give doublespend and its parents to miner:
        doublespend_txid = self.nodes[2].sendrawtransaction(doublespend["hex"])
        # ... mine a block...
        self.nodes[2].generate(1)

        # Reconnect the split network, and sync chain:
        connect_nodes(self.nodes[1], 2)
        self.nodes[2].generate(1)  # Mine another block to make sure we sync
        sync_blocks(self.nodes)
        assert_equal(self.nodes[0].gettransaction(doublespend_txid)["confirmations"], 2)

        # Re-fetch transaction info:
        tx1 = self.nodes[0].gettransaction(txid1)

        # transaction should be conflicted
        assert_equal(tx1["confirmations"], -2)

        # Node0's total balance should be what the doublespend tx paid.  That is,
        # the starting balance, plus coinbase for two more matured blocks,
        # minus the doublespend send, plus fees (which are negative):
        expected = starting_balance + 2*COINBASE_REWARD - doublespend_amt + doublespend_fee
        assert_equal(self.nodes[0].getbalance(), expected)
        assert_equal(self.nodes[0].getbalance("*"), expected)

        # Node1's "from0" account balance should be just the doublespend:
        assert_equal(self.nodes[1].getbalance("from0"), doublespend_amt)

if __name__ == '__main__':
    TxnDoubleSpendTest().main()

def Test():
    t = TxnDoubleSpendTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["blk", "mempool", "net", "req"],
        "logtimemicros": 1
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
