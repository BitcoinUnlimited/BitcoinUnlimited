#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test re-org scenarios with a mempool that contains transactions
# that spend (directly or indirectly) coinbase transactions.
#
import pdb
from collections import OrderedDict

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.script import *

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

        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)

        self.is_network_split=False
        self.sync_all()

    def wastefulOutput(self, btcAddress):
        data = b"""this is junk data."""
        # for long data: ret = CScript([OP_PUSHDATA1, len(data), data, OP_DROP, OP_DUP, OP_HASH160, bitcoinAddress2bin(btcAddress), OP_EQUALVERIFY, OP_CHECKSIG])
        ret = CScript([len(data), data, OP_DROP, OP_DUP, OP_HASH160, bitcoinAddress2bin(btcAddress), OP_EQUALVERIFY, OP_CHECKSIG])
        # ret = CScript([OP_DUP, OP_HASH160, bitcoinAddress2bin(btcAddress), OP_EQUALVERIFY, OP_CHECKSIG])
        return ret

    def run_test(self):

        #prepare some coins for multiple *rawtransaction commands
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[0].generate(101)
        self.sync_all()
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(),1.5)
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(),1.0)
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(),5.0)
        self.sync_all()
        self.nodes[0].generate(5)
        self.sync_all()

        #########################################
        # sendrawtransaction with missing input #
        #########################################
        inputs  = [ {'txid' : "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout' : 1}] #won't exists
        outputs = { self.nodes[0].getnewaddress() : 4.998 }
        rawtx   = self.nodes[2].createrawtransaction(inputs, outputs)
        rawtx   = self.nodes[2].signrawtransaction(rawtx)

        try:
            rawtx   = self.nodes[2].sendrawtransaction(rawtx['hex'])
        except JSONRPCException as e:
            assert("Missing inputs" in e.error['message'])
        else:
            assert(False)


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
        self.sync_all()
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
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        #THIS IS A INCOMPLETE FEATURE
        #NODE2 HAS TWO OF THREE KEY AND THE FUNDS SHOULD BE SPENDABLE AND COUNT AT BALANCE CALCULATION
        assert_equal(self.nodes[2].getbalance(), bal) #for now, assume the funds of a 2of3 multisig tx are not marked as spendable

        txDetails = self.nodes[0].gettransaction(txId, True)
        rawTx = self.nodes[0].decoderawtransaction(txDetails['hex'])
        vout = False
        for outpoint in rawTx['vout']:
            if outpoint['value'] == Decimal('2.20000000'):
                vout = outpoint
                break

        bal = self.nodes[0].getbalance()
        inputs = [{ "txid" : txId, "vout" : vout['n'], "scriptPubKey" : vout['scriptPubKey']['hex']}]
        outputs = { self.nodes[0].getnewaddress() : 2.19 }
        rawTx = self.nodes[2].createrawtransaction(inputs, outputs)
        rawTxPartialSigned = self.nodes[1].signrawtransaction(rawTx, inputs)
        assert_equal(rawTxPartialSigned['complete'], False) #node1 only has one key, can't comp. sign the tx

        rawTxSigned = self.nodes[2].signrawtransaction(rawTx, inputs)
        assert_equal(rawTxSigned['complete'], True) #node2 can sign the tx compl., own two of three keys
        self.nodes[2].sendrawtransaction(rawTxSigned['hex'])
        rawTx = self.nodes[0].decoderawtransaction(rawTxSigned['hex'])
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        assert_equal(self.nodes[0].getbalance(), bal+Decimal('50.00000000')+Decimal('2.19000000')) #block reward + tx

        ###################################
        # RAW TX, with custom output script
        ###################################

        # Start with a standard output script, so we can spend it
        node = self.nodes[0]
        newAddr = self.nodes[1].getnewaddress()
        wallet = node.listunspent()
        utxo = wallet.pop()
        outp = OrderedDict()
        outscript = CScript([OP_DUP, OP_HASH160, bitcoinAddress2bin(newAddr), OP_EQUALVERIFY, OP_CHECKSIG])
        outscripthex = hexlify(outscript).decode("ascii")
        amt = utxo["amount"]
        outp[outscripthex] = amt
        txn = node.createrawtransaction([utxo], outp)
        txna = node.createrawtransaction([utxo], {newAddr:utxo["amount"]})
        assert(txn==txna)  # verify that we made the same tx as bitcoind

        # ok let's add an OP_RETURN
        outscript = CScript([OP_RETURN, b"This is some random data"])
        outscripthex = hexlify(outscript).decode("ascii")
        outp[outscripthex] = 0
        txn = node.createrawtransaction([utxo], outp)

        signedtxn = node.signrawtransaction(txn)
        txhash = node.sendrawtransaction(signedtxn["hex"])

        priorbal = self.nodes[1].getbalance()
        node.generate(1)
        self.sync_all()
        bal = self.nodes[1].getbalance()
        assert (bal-priorbal == amt)

        # Ok now make a weird tx output, spending the prior output
        newAddr2 = self.nodes[1].getnewaddress()

        outscript = self.wastefulOutput(newAddr2)
        outscripthex = hexlify(outscript).decode("ascii")
        txn2 = node.createrawtransaction([{"txid":txhash,"vout":0}], {outscripthex:amt})
        signedtxn2 = self.nodes[1].signrawtransaction(txn2)
        assert(signedtxn2["complete"])
        txhash2 = node.sendrawtransaction(signedtxn2["hex"])
        self.sync_all()
        self.nodes[1].generate(1)
        bal = self.nodes[1].getbalance()
        assert(bal == 0)  # Even though I spent to myself, bitcoind is not smart enough to notice this balance

        # bitcoind can't spend the weird output, because it can't understand the output script.
        if 0:
            newAddr3 = self.nodes[1].getnewaddress()
            # txn3 = self.nodes[1].createrawtransaction([{"txid":txhash2,"vout":0,"scriptPubKey":"" }], {newAddr:amt})
            txn3 = self.nodes[1].createrawtransaction([{"txid":txhash2,"vout":0,}], {newAddr:amt})
            pdb.set_trace()
            signedtxn3 = self.nodes[1].signrawtransaction(txn3)
            # assert(signedtxn3["complete"])
            txhash3 = self.nodes[1].sendrawtransaction(signedtxn3["hex"])
            self.nodes[1].generate(1)
            self.sync_all()
            bal = self.nodes[1].getbalance()
            assert(bal == amt)

if __name__ == '__main__':
    RawTransactionsTest().main()


def Test():
  t = RawTransactionsTest()
  bitcoinConf = {
    "debug":["net","blk","thin","mempool","req","bench","evict"],
    "blockprioritysize":2000000  # we don't want any transactions rejected due to insufficient fees...
  }
  t.main(["--tmpdir=/ramdisk/test", "--nocleanup","--noshutdown"],bitcoinConf,None)
