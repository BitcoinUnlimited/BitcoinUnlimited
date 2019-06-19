#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
#
# Test re-org scenarios with a mempool that contains transactions
# that spend (directly or indirectly) coinbase transactions.
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.blocktools import *

# Create one-input, one-output, no-fee transaction:
class RawTransactionsTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir)

        #connect to a local machine for debugging
        #url = "http://bitcoinrpc:DP6DvqZtqXarpeNWyN3LZTFchCCyCUuHwNF7E8pX99x1@%s:%d" % ('127.0.0.1', 18332)
        #proxy = AuthServiceProxy(url)
        #proxy.url = url # store URL on proxy for info
        #self.nodes.append(proxy)
        connect_nodes_full(self.nodes)

        self.is_network_split=False
        self.sync_all()

    def run_test(self):

        #prepare some coins for multiple *rawtransaction commands
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[0].generate(101)
        self.sync_all()
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(),1.5)
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(),1.0)
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(),5.0)
        self.nodes[0].generate(5)
        self.sync_all()

        #########################################
        # sendrawtransaction with missing input #
        #########################################
        inputs  = [ {'txid' : "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout' : 1}] #won't exists
        outputs = { self.nodes[0].getnewaddress() : 3.998, self.nodes[0].getnewaddress() : 1.0 }
        rawtx   = self.nodes[2].createrawtransaction(inputs, outputs)
        rawtx   = self.nodes[2].signrawtransaction(rawtx)

        try:
            rawtx   = self.nodes[2].sendrawtransaction(rawtx['hex'])
        except JSONRPCException as e:
            assert("Missing inputs" in e.error['message'])
        else:
            assert(False)

        #####################################
        # getrawtransaction with block hash #
        #####################################

        # make a tx by sending then generate 2 blocks; block1 has the tx in it
        tx = self.nodes[2].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        block1, block2 = self.nodes[2].generate(2)
        self.sync_all()
        # We should be able to get the raw transaction by providing the correct block
        gottx = self.nodes[0].getrawtransaction(tx, True, block1)
        assert_equal(gottx['txid'], tx)
        assert_equal(gottx['in_active_chain'], True)
        # We should not have the 'in_active_chain' flag when we don't provide a block
        gottx = self.nodes[0].getrawtransaction(tx, True)
        assert_equal(gottx['txid'], tx)
        assert 'in_active_chain' not in gottx
        # We should not get the tx if we provide an unrelated block
        assert_raises_rpc_error(-5, "No such transaction found", self.nodes[0].getrawtransaction, tx, True, block2)
        # An invalid block hash should raise the correct errors
        assert_raises_rpc_error(-8, "parameter 3 must be hexadecimal", self.nodes[0].getrawtransaction, tx, True, True)
        assert_raises_rpc_error(-8, "parameter 3 must be hexadecimal", self.nodes[0].getrawtransaction, tx, True, "foobar")
        assert_raises_rpc_error(-8, "parameter 3 must be of length 64", self.nodes[0].getrawtransaction, tx, True, "abcd1234")
        assert_raises_rpc_error(-5, "Block hash not found", self.nodes[0].getrawtransaction, tx, True, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")

        #########################
        # RAW TX MULTISIG TESTS #
        #########################
        # 2of2 test
        addr1 = self.nodes[2].getnewaddress()
        addr2 = self.nodes[2].getnewaddress()

        addr1Obj = self.nodes[2].validateaddress(addr1)
        addr2Obj = self.nodes[2].validateaddress(addr2)

        mSigObj = self.nodes[2].addmultisigaddress(2, [addr1Obj['pubkey'], addr2Obj['pubkey']])
        mSigObjValid = self.nodes[2].validateaddress(mSigObj)

        #use balance deltas instead of absolute values
        bal = self.nodes[2].getbalance()

        # send 1.2 BTC to msig adr
        txId = self.nodes[0].sendtoaddress(mSigObj, 1.2)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_blocks()
        assert_equal(self.nodes[2].getbalance(), bal+Decimal('1.20000000')) #node2 has both keys of the 2of2 ms addr., tx should affect the balance


        # 2of3 test from different nodes
        bal = self.nodes[2].getbalance()
        addr1 = self.nodes[1].getnewaddress()
        addr2 = self.nodes[2].getnewaddress()
        addr3 = self.nodes[2].getnewaddress()

        addr1Obj = self.nodes[1].validateaddress(addr1)
        addr2Obj = self.nodes[2].validateaddress(addr2)
        addr3Obj = self.nodes[2].validateaddress(addr3)

        mSigObj = self.nodes[2].addmultisigaddress(2, [addr1Obj['pubkey'], addr2Obj['pubkey'], addr3Obj['pubkey']])
        mSigObjValid = self.nodes[2].validateaddress(mSigObj)

        txId = self.nodes[0].sendtoaddress(mSigObj, 2.2)
        decTx = self.nodes[0].gettransaction(txId)
        rawTx = self.nodes[0].decoderawtransaction(decTx['hex'])
        sPK = rawTx['vout'][0]['scriptPubKey']['hex']
        self.nodes[0].generate(1)
        self.sync_blocks()

        #THIS IS A INCOMPLETE FEATURE
        #NODE2 HAS TWO OF THREE KEY AND THE FUNDS SHOULD BE SPENDABLE AND COUNT AT BALANCE CALCULATION
        assert_equal(self.nodes[2].getbalance(), bal) #for now, assume the funds of a 2of3 multisig tx are not marked as spendable

        txDetails = self.nodes[0].gettransaction(txId, True)
        decrawTx = self.nodes[0].decoderawtransaction(txDetails['hex'])
        vout = False
        for outpoint in decrawTx['vout']:
            if outpoint['value'] == Decimal('2.20000000'):
                vout = outpoint
                break

        bal = self.nodes[0].getbalance()
        inputs = [{ "txid" : txId, "vout" : vout['n'], "scriptPubKey" : vout['scriptPubKey']['hex'], "amount":vout['value'] }]
        outputs = { self.nodes[0].getnewaddress() : 2.19 }
        rawTx = self.nodes[2].createrawtransaction(inputs, outputs)
        rawTxPartialSigned = self.nodes[1].signrawtransaction(rawTx, inputs)
        assert_equal(rawTxPartialSigned['complete'], False) #node1 only has one key, can't comp. sign the tx
        rawTxSigned = self.nodes[2].signrawtransaction(rawTx, inputs)
        assert_equal(rawTxSigned['complete'], True) #node2 can sign the tx compl., own two of three keys
        self.nodes[2].enqueuerawtransaction(rawTxSigned['hex'],"flush")
        rawTx = self.nodes[0].decoderawtransaction(rawTxSigned['hex'])
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        assert_equal(self.nodes[0].getbalance(), bal+Decimal('50.00000000')+Decimal('2.19000000')) #block reward + tx

        #########################################
        # standard/nonstandard sendrawtransaction
        #########################################

        wallet = self.nodes[0].listunspent()
        wallet.sort(key=lambda x: x["amount"], reverse=False)
        utxo = wallet.pop()
        amt = utxo["amount"]
        addr = self.nodes[0].getaddressforms(self.nodes[0].getnewaddress())["legacy"]
        outp = {addr: amt-decimal.Decimal(.0001)}  # give some fee
        txn = createrawtransaction([utxo], outp, createWastefulOutput)  # create a nonstandard tx
        signedtxn = self.nodes[0].signrawtransaction(txn)
        mempool = self.nodes[0].getmempoolinfo()
        try:
            self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "STANDARD")
            assert 0 # should have failed because I'm insisting on a standard tx
        except JSONRPCException as e:
            assert(e.error["code"] == -26)

        try:
            self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "standard")
            assert 0 # should have failed, check case insensitivity
        except JSONRPCException as e:
            assert(e.error["code"] == -26)

        ret = self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "NONstandard")
        assert(len(ret) == 64)  # should succeed and return a txid

        # In regtest mode, nonstandard transactions are allowed by default
        ret2 = self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "default")
        assert ret == ret2  # I'm sending the same tx so it should work with the same result

        mempool2 = self.nodes[0].getmempoolinfo()
        assert mempool["size"] + 1 == mempool2["size"]  # one tx should have been added to the mempool

        self.nodes[0].generate(1)  # clean it up
        mempool3 = self.nodes[0].getmempoolinfo()
        assert mempool3["size"] == 0  # Check that the nonstandard tx in the mempool got mined

        # Now try it as if we were on mainnet (rejecting nonstandard transactions)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        # restart the node with a flag that forces the behavior to be more like mainnet -- don't accept nonstandard tx
        self.nodes = start_nodes(3, self.options.tmpdir, [ ["--acceptnonstdtxn=0"], [], [], []])
        connect_nodes_full(self.nodes)

        wallet = self.nodes[0].listunspent()
        wallet.sort(key=lambda x: x["amount"], reverse=False)
        utxo = wallet.pop()
        amt = utxo["amount"]
        addr = self.nodes[0].getaddressforms(self.nodes[0].getnewaddress())["legacy"]
        outp = {addr: amt-decimal.Decimal(.0001)}  # give some fee
        txn = createrawtransaction([utxo], outp, createWastefulOutput)  # create a nonstandard tx
        signedtxn = self.nodes[0].signrawtransaction(txn)
        mempool = self.nodes[0].getmempoolinfo()
        try:
            self.nodes[0].sendrawtransaction(signedtxn["hex"])
            assert 0 # should have failed because I'm insisting on a standard tx
        except JSONRPCException as e:
            assert e.error["code"] == -26
        try:
            self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "STANDARD")
            assert 0 # should have failed because I'm insisting on a standard tx
        except JSONRPCException as e:
            assert e.error["code"] == -26
        try:
            self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "default")
            assert 0 # should have failed because I'm insisting on a standard tx via the --acceptnonstdtxn flag
        except JSONRPCException as e:
            assert e.error["code"] == -26

        try:
            self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "somebadvalue")
            assert 0 # should have failed because I'm insisting on a standard tx via the --acceptnonstdtxn flag
        except JSONRPCException as e:
            assert e.error["code"] == -8
            assert e.error["message"] == 'Invalid transaction class'
        mempool4 = self.nodes[0].getmempoolinfo()
        assert mempool["size"] == mempool4["size"]  # all of these failures should have added nothing to mempool

        ret = self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "nonstandard")
        assert(len(ret) == 64)  # should succeed and return a txid
        mempool2 = self.nodes[0].getmempoolinfo()
        assert mempool["size"] + 1 == mempool2["size"]  # one tx should have been added to the mempool

        self.nodes[0].generate(1)  # clean it up
        mempool3 = self.nodes[0].getmempoolinfo()
        assert mempool3["size"] == 0  # Check that the nonstandard tx in the mempool got mined

        # finally, let's make sure that a standard tx works with the standard flag set
        utxo = wallet.pop()
        amt = utxo["amount"]
        addr = self.nodes[0].getaddressforms(self.nodes[0].getnewaddress())["legacy"]
        outp = {addr: amt-decimal.Decimal(.0001)}  # give some fee
        txn = createrawtransaction([utxo], outp, p2pkh)  # create a standard tx
        signedtxn = self.nodes[0].signrawtransaction(txn)
        txid = self.nodes[0].sendrawtransaction(signedtxn["hex"], False, "STANDARD")
        assert(len(txid) == 64)

        # getrawtransaction tests
        # 1. valid parameters - only supply txid
        txHash = rawTx["txid"]
        assert_equal(self.nodes[0].getrawtransaction(txHash), rawTxSigned['hex'])

        # 2. valid parameters - supply txid and 0 for non-verbose
        assert_equal(self.nodes[0].getrawtransaction(txHash, 0), rawTxSigned['hex'])

        # 3. valid parameters - supply txid and False for non-verbose
        assert_equal(self.nodes[0].getrawtransaction(txHash, False), rawTxSigned['hex'])

        # 4. valid parameters - supply txid and 1 for verbose.
        # We only check the "hex" field of the output so we don't need to update this test every time the output format changes.
        assert_equal(self.nodes[0].getrawtransaction(txHash, 1)["hex"], rawTxSigned['hex'])

        # 5. valid parameters - supply txid and True for non-verbose
        assert_equal(self.nodes[0].getrawtransaction(txHash, True)["hex"], rawTxSigned['hex'])

        # 6. invalid parameters - supply txid and string "Flase"
        assert_raises_rpc_error(-32700,"Error parsing JSON:Flase", self.nodes[0].getrawtransaction, txHash, "Flase")

        # 7. invalid parameters - supply txid and empty array
        assert_raises_rpc_error(-1,"not a boolean", self.nodes[0].getrawtransaction, txHash, [])

        # 8. invalid parameters - supply txid and empty dict
        assert_raises_rpc_error(-1,"not a boolean", self.nodes[0].getrawtransaction, txHash, {})

        inputs  = [ {'txid' : "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout' : 1, 'sequence' : 1000}]
        outputs = { self.nodes[0].getnewaddress() : 1 }
        rawtx   = self.nodes[0].createrawtransaction(inputs, outputs)
        decrawtx= self.nodes[0].decoderawtransaction(rawtx)
        assert_equal(decrawtx['vin'][0]['sequence'], 1000)

        inputs  = [ {'txid' : "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout' : 1, 'sequence' : -1}]
        outputs = { self.nodes[0].getnewaddress() : 1 }
        assert_raises(JSONRPCException, self.nodes[0].createrawtransaction, inputs, outputs)

        inputs  = [ {'txid' : "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout' : 1, 'sequence' : 4294967296}]
        outputs = { self.nodes[0].getnewaddress() : 1 }
        assert_raises(JSONRPCException, self.nodes[0].createrawtransaction, inputs, outputs)

        inputs  = [ {'txid' : "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout' : 1, 'sequence' : 'notanumber'}]
        outputs = { self.nodes[0].getnewaddress() : 1 }
        assert_raises(JSONRPCException, self.nodes[0].createrawtransaction, inputs, outputs)

        inputs  = [ {'txid' : "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout' : 1, 'sequence' : 4294967294}]
        outputs = { self.nodes[0].getnewaddress() : 1 }
        rawtx   = self.nodes[0].createrawtransaction(inputs, outputs)
        decrawtx= self.nodes[0].decoderawtransaction(rawtx)
        assert_equal(decrawtx['vin'][0]['sequence'], 4294967294)


if __name__ == '__main__':
    RawTransactionsTest().main()

def Test():
    t = RawTransactionsTest()
    #t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["rpc","net", "blk", "thin", "mempool", "req", "bench", "evict"],
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
