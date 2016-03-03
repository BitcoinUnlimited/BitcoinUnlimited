#!/usr/bin/env python2
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Exercise the listtransactions API

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import CTransaction
import cStringIO
import binascii

def txFromHex(hexstring):
    tx = CTransaction()
    f = cStringIO.StringIO(binascii.unhexlify(hexstring))
    tx.deserialize(f)
    return tx

def check_array_result(object_array, to_match, expected):
    """
    Pass in array of JSON objects, a dictionary with key/value pairs
    to match against, and another dictionary with expected key/value
    pairs.
    """
    num_matched = 0
    for item in object_array:
        all_match = True
        for key,value in to_match.items():
            if item[key] != value:
                all_match = False
        if not all_match:
            continue
        for key,value in expected.items():
            if item[key] != value:
                raise AssertionError("%s : expected %s=%s"%(str(item), str(key), str(value)))
            num_matched = num_matched+1
    if num_matched == 0:
        raise AssertionError("No objects matched %s"%(str(to_match)))

class ListTransactionsTest(BitcoinTestFramework):

    def run_test(self):
        # Simple send, 0 to 1:
        txid = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
        self.sync_all()
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid},
                           {"category":"send","account":"","amount":Decimal("-0.1"),"confirmations":0})
        check_array_result(self.nodes[1].listtransactions(),
                           {"txid":txid},
                           {"category":"receive","account":"","amount":Decimal("0.1"),"confirmations":0})
        # mine a block, confirmations should change:
        self.nodes[0].generate(1)
        self.sync_all()
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid},
                           {"category":"send","account":"","amount":Decimal("-0.1"),"confirmations":1})
        check_array_result(self.nodes[1].listtransactions(),
                           {"txid":txid},
                           {"category":"receive","account":"","amount":Decimal("0.1"),"confirmations":1})

        # send-to-self:
        txid = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.2)
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid, "category":"send"},
                           {"amount":Decimal("-0.2")})
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid, "category":"receive"},
                           {"amount":Decimal("0.2")})

        # sendmany from node1: twice to self, twice to node2:
        send_to = { self.nodes[0].getnewaddress() : 0.11,
                    self.nodes[1].getnewaddress() : 0.22,
                    self.nodes[0].getaccountaddress("from1") : 0.33,
                    self.nodes[1].getaccountaddress("toself") : 0.44 }
        txid = self.nodes[1].sendmany("", send_to)
        self.sync_all()
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.11")},
                           {"txid":txid} )
        check_array_result(self.nodes[0].listtransactions(),
                           {"category":"receive","amount":Decimal("0.11")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.22")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"receive","amount":Decimal("0.22")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.33")},
                           {"txid":txid} )
        check_array_result(self.nodes[0].listtransactions(),
                           {"category":"receive","amount":Decimal("0.33")},
                           {"txid":txid, "account" : "from1"} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.44")},
                           {"txid":txid, "account" : ""} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"receive","amount":Decimal("0.44")},
                           {"txid":txid, "account" : "toself"} )

        multisig = self.nodes[1].createmultisig(1, [self.nodes[1].getnewaddress()])
        self.nodes[0].importaddress(multisig["redeemScript"], "watchonly", False, True)
        txid = self.nodes[1].sendtoaddress(multisig["address"], 0.1)
        self.nodes[1].generate(1)
        self.sync_all()
        assert(len(self.nodes[0].listtransactions("watchonly", 100, 0, False)) == 0)
        check_array_result(self.nodes[0].listtransactions("watchonly", 100, 0, True),
                           {"category":"receive","amount":Decimal("0.1")},
                           {"txid":txid, "account" : "watchonly"} )

        self.run_rbf_opt_in_test()

    # Check that the opt-in-rbf flag works properly, for sent and received
    # transactions.
    def run_rbf_opt_in_test(self):
        # Check whether a transaction signals opt-in RBF itself
        def is_opt_in(node, txid):
            rawtx = node.getrawtransaction(txid, 1)
            for x in rawtx["vin"]:
                if x["sequence"] < 0xfffffffe:
                    return True
            return False

        # Find an unconfirmed output matching a certain txid
        def get_unconfirmed_utxo_entry(node, txid_to_match):
            utxo = node.listunspent(0, 0)
            for i in utxo:
                if i["txid"] == txid_to_match:
                    return i
            return None

        # 1. Chain a few transactions that don't opt-in.
        txid_1 = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        assert(not is_opt_in(self.nodes[0], txid_1))
        check_array_result(self.nodes[0].listtransactions(), {"txid": txid_1}, {"bip125-replaceable":"no"})
        sync_mempools(self.nodes)
        check_array_result(self.nodes[1].listtransactions(), {"txid": txid_1}, {"bip125-replaceable":"no"})

        # Tx2 will build off txid_1, still not opting in to RBF.
        utxo_to_use = get_unconfirmed_utxo_entry(self.nodes[1], txid_1)

        # Create tx2 using createrawtransaction
        inputs = [{"txid":utxo_to_use["txid"], "vout":utxo_to_use["vout"]}]
        outputs = {self.nodes[0].getnewaddress(): 0.999}
        tx2 = self.nodes[1].createrawtransaction(inputs, outputs)
        tx2_signed = self.nodes[1].signrawtransaction(tx2)["hex"]
        txid_2 = self.nodes[1].sendrawtransaction(tx2_signed)

        # ...and check the result
        assert(not is_opt_in(self.nodes[1], txid_2))
        check_array_result(self.nodes[1].listtransactions(), {"txid": txid_2}, {"bip125-replaceable":"no"})
        sync_mempools(self.nodes)
        check_array_result(self.nodes[0].listtransactions(), {"txid": txid_2}, {"bip125-replaceable":"no"})

        # Tx3 will opt-in to RBF
        utxo_to_use = get_unconfirmed_utxo_entry(self.nodes[0], txid_2)
        inputs = [{"txid": txid_2, "vout":utxo_to_use["vout"]}]
        outputs = {self.nodes[1].getnewaddress(): 0.998}
        tx3 = self.nodes[0].createrawtransaction(inputs, outputs)
        tx3_modified = txFromHex(tx3)
        tx3_modified.vin[0].nSequence = 0
        tx3 = binascii.hexlify(tx3_modified.serialize()).decode('utf-8')
        tx3_signed = self.nodes[0].signrawtransaction(tx3)['hex']
        txid_3 = self.nodes[0].sendrawtransaction(tx3_signed)

        assert(is_opt_in(self.nodes[0], txid_3))
        check_array_result(self.nodes[0].listtransactions(), {"txid": txid_3}, {"bip125-replaceable":"yes"})
        sync_mempools(self.nodes)
        check_array_result(self.nodes[1].listtransactions(), {"txid": txid_3}, {"bip125-replaceable":"yes"})

        # Tx4 will chain off tx3.  Doesn't signal itself, but depends on one
        # that does.
        utxo_to_use = get_unconfirmed_utxo_entry(self.nodes[1], txid_3)
        inputs = [{"txid": txid_3, "vout":utxo_to_use["vout"]}]
        outputs = {self.nodes[0].getnewaddress(): 0.997}
        tx4 = self.nodes[1].createrawtransaction(inputs, outputs)
        tx4_signed = self.nodes[1].signrawtransaction(tx4)["hex"]
        txid_4 = self.nodes[1].sendrawtransaction(tx4_signed)

        assert(not is_opt_in(self.nodes[1], txid_4))
        check_array_result(self.nodes[1].listtransactions(), {"txid": txid_4}, {"bip125-replaceable":"yes"})
        sync_mempools(self.nodes)
        check_array_result(self.nodes[0].listtransactions(), {"txid": txid_4}, {"bip125-replaceable":"yes"})

        # Replace tx3, and check that tx4 becomes unknown
        tx3_b = tx3_modified
        tx3_b.vout[0].nValue -= 0.004*100000000 # bump the fee
        tx3_b = binascii.hexlify(tx3_b.serialize()).decode('utf-8')
        tx3_b_signed = self.nodes[0].signrawtransaction(tx3_b)['hex']
        txid_3b = self.nodes[0].sendrawtransaction(tx3_b_signed, True)
        assert(is_opt_in(self.nodes[0], txid_3b))

        check_array_result(self.nodes[0].listtransactions(), {"txid": txid_4}, {"bip125-replaceable":"unknown"})
        sync_mempools(self.nodes)
        check_array_result(self.nodes[1].listtransactions(), {"txid": txid_4}, {"bip125-replaceable":"unknown"})

        # Check gettransaction as well:
        for n in self.nodes[0:2]:
            assert_equal(n.gettransaction(txid_1)["bip125-replaceable"], "no")
            assert_equal(n.gettransaction(txid_2)["bip125-replaceable"], "no")
            assert_equal(n.gettransaction(txid_3)["bip125-replaceable"], "yes")
            assert_equal(n.gettransaction(txid_3b)["bip125-replaceable"], "yes")
            assert_equal(n.gettransaction(txid_4)["bip125-replaceable"], "unknown")

        # After mining a transaction, it's no longer BIP125-replaceable
        self.nodes[0].generate(1)
        assert(txid_3b not in self.nodes[0].getrawmempool())
        assert_equal(self.nodes[0].gettransaction(txid_3b)["bip125-replaceable"], "no")
        assert_equal(self.nodes[0].gettransaction(txid_4)["bip125-replaceable"], "unknown")


if __name__ == '__main__':
    ListTransactionsTest().main()

