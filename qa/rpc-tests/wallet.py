#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
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
import binascii
from test_framework.script import *
from test_framework.nodemessages import *

def GenerateSingleSigP2SH(btcAddress):
    redeemScript = CScript([OP_DUP, OP_HASH160, bitcoinAddress2bin(btcAddress), OP_EQUALVERIFY, OP_CHECKSIG])
    p2shAddressBin = hash160(redeemScript)
    p2shAddress = encodeBitcoinAddress(bytes([196]), p2shAddressBin)  # 196 is regtest P2SH addr prefix
    pubkeyScript = CScript([OP_HASH160, p2shAddressBin, OP_EQUAL])
    return ( p2shAddress, redeemScript)

def waitForRescan(node):
    info = node.getinfo()
    while "rescanning" in info["status"]:
        logging.info("rescanning")
        time.sleep(.25)
        info = node.getinfo()


class WalletTest (BitcoinTestFramework):

    def check_fee_amount(self, curr_balance, balance_with_fee, fee_per_byte, tx_size):
        """Return curr_balance after asserting the fee was in range"""
        fee = balance_with_fee - curr_balance
        target_fee = fee_per_byte * tx_size
        if fee < target_fee:
            raise AssertionError("Fee of %s BTC too low! (Should be %s BTC)"%(str(fee), str(target_fee)))
        # allow the node's estimation to be at most 2 bytes off
        if fee > fee_per_byte * (tx_size + 2):
            raise AssertionError("Fee of %s BTC too high! (Should be %s BTC)"%(str(fee), str(target_fee)))
        return curr_balance

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        logging.info("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.node_args = [['-usehd=0'], ['-usehd=0'], ['-usehd=0']]
        self.nodes = start_nodes(3, self.options.tmpdir, self.node_args)
        connect_nodes_full(self.nodes)
        self.is_network_split=False
        self.sync_blocks()

    def run_test (self):

        # Check that there's no UTXO on none of the nodes
        assert_equal(len(self.nodes[0].listunspent()), 0)
        assert_equal(len(self.nodes[1].listunspent()), 0)
        assert_equal(len(self.nodes[2].listunspent()), 0)

        logging.info("Mining blocks...")

        self.nodes[0].generate(1)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(walletinfo['immature_balance'], 50)
        assert_equal(walletinfo['balance'], 0)

        self.sync_blocks()
        self.nodes[1].generate(101)
        self.sync_blocks()

        assert_equal(self.nodes[0].getbalance(), 50)
        assert_equal(self.nodes[1].getbalance(), 50)
        assert_equal(self.nodes[2].getbalance(), 0)

        # Check that only first and second nodes have UTXOs
        assert_equal(len(self.nodes[0].listunspent()), 1)
        assert_equal(len(self.nodes[1].listunspent()), 1)
        assert_equal(len(self.nodes[2].listunspent()), 0)

        # Send 21 BTC from 0 to 2 using sendtoaddress call.
        # Second transaction will be child of first, and will require a fee
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 11)
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 10)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(walletinfo['immature_balance'], 0)

        # Have node0 mine a block, thus it will collect its own fee.
        self.nodes[0].generate(1)
        self.sync_all()

        # Exercise locking of unspent outputs
        unspent_0 = self.nodes[2].listunspent()[0]
        unspent_0 = {"txid": unspent_0["txid"], "vout": unspent_0["vout"]}
        self.nodes[2].lockunspent(False, [unspent_0])
        assert_raises(JSONRPCException, self.nodes[2].sendtoaddress, self.nodes[2].getnewaddress(), 20)
        assert_equal([unspent_0], self.nodes[2].listlockunspent())
        self.nodes[2].lockunspent(True, [unspent_0])
        assert_equal(len(self.nodes[2].listlockunspent()), 0)

        # Have node1 generate 100 blocks (so node0 can recover the fee)
        self.nodes[1].generate(100)
        self.sync_all()

        # node0 should end up with 100 btc in block rewards plus fees, but
        # minus the 21 plus fees sent to node2
        assert_equal(self.nodes[0].getbalance(), 100-21)
        assert_equal(self.nodes[2].getbalance(), 21)

        # Node0 should have two unspent outputs.
        # Create a couple of transactions to send them to node2, submit them through
        # node1, and make sure both node0 and node2 pick them up properly:
        node0utxos = self.nodes[0].listunspent(1)
        assert_equal(len(node0utxos), 2)

        # create both transactions
        txns_to_send = []
        for utxo in node0utxos:
            inputs = []
            outputs = {}
            inputs.append({ "txid" : utxo["txid"], "vout" : utxo["vout"]})
            outputs[self.nodes[2].getnewaddress("from1")] = utxo["amount"]
            raw_tx = self.nodes[0].createrawtransaction(inputs, outputs)
            txns_to_send.append(self.nodes[0].signrawtransaction(raw_tx))

        # Have node 1 (miner) send the transactions
        self.nodes[1].enqueuerawtransaction(txns_to_send[0]["hex"])
        self.nodes[1].enqueuerawtransaction(txns_to_send[1]["hex"], "flush")

        # Have node1 mine a block to confirm transactions:
        self.nodes[1].generate(1)
        self.sync_all()

        assert_equal(self.nodes[0].getbalance(), 0)
        assert_equal(self.nodes[2].getbalance(), 100)
        assert_equal(self.nodes[2].getbalance("from1"), 100-21)

        # Send 10 BTC normal
        address = self.nodes[0].getnewaddress("test")
        fee_per_byte = Decimal('0.001') / 1000
        self.nodes[2].settxfee(fee_per_byte * 1000)
        txid = self.nodes[2].sendtoaddress(address, 10, "", "", False)
        self.nodes[2].generate(1)
        self.sync_all()
        node_2_bal = self.check_fee_amount(self.nodes[2].getbalance(), Decimal('90'), fee_per_byte, count_bytes(self.nodes[2].getrawtransaction(txid)))
        assert_equal(self.nodes[0].getbalance(), Decimal('10'))

        # Send 10 BTC with subtract fee from amount
        txid = self.nodes[2].sendtoaddress(address, 10, "", "", True)
        self.nodes[2].generate(1)
        self.sync_all()
        node_2_bal -= Decimal('10')
        assert_equal(self.nodes[2].getbalance(), node_2_bal)
        node_0_bal = self.check_fee_amount(self.nodes[0].getbalance(), Decimal('20'), fee_per_byte, count_bytes(self.nodes[2].getrawtransaction(txid)))

        # Sendmany 10 BTC
        txid = self.nodes[2].sendmany('from1', {address: 10}, 0, "", [])
        self.nodes[2].generate(1)
        self.sync_all()
        node_0_bal += Decimal('10')
        node_2_bal = self.check_fee_amount(self.nodes[2].getbalance(), node_2_bal - Decimal('10'), fee_per_byte, count_bytes(self.nodes[2].getrawtransaction(txid)))
        assert_equal(self.nodes[0].getbalance(), node_0_bal)

        # Sendmany 10 BTC with subtract fee from amountd
        txid = self.nodes[2].sendmany('from1', {address: 10}, 0, "", [address])
        self.nodes[2].generate(1)
        self.sync_all()
        node_2_bal -= Decimal('10')
        assert_equal(self.nodes[2].getbalance(), node_2_bal)
        node_0_bal = self.check_fee_amount(self.nodes[0].getbalance(), node_0_bal + Decimal('10'), fee_per_byte, count_bytes(self.nodes[2].getrawtransaction(txid)))

        # Test ResendWalletTransactions:
        # Create a couple of transactions, then start up a fourth
        # node (nodes[3]) and ask nodes[0] to rebroadcast.
        # EXPECT: nodes[3] should have those transactions in its mempool.
        txid1 = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        txid2 = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), 1)
        sync_mempools(self.nodes)

        self.nodes.append(start_node(3, self.options.tmpdir, ['-usehd=0']))
        connect_nodes_bi(self.nodes, 0, 3)
        sync_blocks(self.nodes)

        relayed = self.nodes[0].resendwallettransactions()
        assert_equal(set(relayed), {txid1, txid2})
        sync_mempools(self.nodes)

        assert(txid1 in self.nodes[3].getrawmempool())

        # Exercise balance rpcs
        assert_equal(self.nodes[0].getwalletinfo()["unconfirmed_balance"], 1)
        assert_equal(self.nodes[0].getunconfirmedbalance(), 1)

        #check if we can list zero value tx as available coins
        #1. create rawtx
        #2. hex-changed one output to 0.0
        #3. sign and send
        #4. check if recipient (node0) can list the zero value tx
        usp = self.nodes[1].listunspent()
        inputs = [{"txid":usp[0]['txid'], "vout":usp[0]['vout']}]
        outputs = {self.nodes[1].getnewaddress(): 49.998, self.nodes[0].getnewaddress(): 11.11}

        rawTx = self.nodes[1].createrawtransaction(inputs, outputs).replace("c0833842", "00000000") #replace 11.11 with 0.0 (int32)
        decRawTx = self.nodes[1].decoderawtransaction(rawTx)
        signedRawTx = self.nodes[1].signrawtransaction(rawTx)
        decRawTx = self.nodes[1].decoderawtransaction(signedRawTx['hex'])
        zeroValueTxid= decRawTx['txid']
        sendResp = self.nodes[1].sendrawtransaction(signedRawTx['hex'])

        self.sync_all()
        self.nodes[1].generate(1) #mine a block
        self.sync_all()

        unspentTxs = self.nodes[0].listunspent() #zero value tx must be in listunspents output
        found = False
        for uTx in unspentTxs:
            if uTx['txid'] == zeroValueTxid:
                found = True
                assert_equal(uTx['amount'], Decimal('0'))
                assert_equal(uTx['satoshi'], Decimal('0'))
        assert(found)

        #do some -walletbroadcast tests
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.nodes = start_nodes(3, self.options.tmpdir, [["-walletbroadcast=0", "-usehd=0"],["-walletbroadcast=0", "-usehd=0"],["-walletbroadcast=0", "-usehd=0"]])
        connect_nodes_full(self.nodes)
        self.sync_all()

        txIdNotBroadcasted  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 2)
        txObjNotBroadcasted = self.nodes[0].gettransaction(txIdNotBroadcasted)
        self.nodes[1].generate(1) #mine a block, tx should not be in there
        self.sync_all()
        assert_equal(self.nodes[2].getbalance(), node_2_bal) #should not be changed because tx was not broadcasted

        #now broadcast from another node, mine a block, sync, and check the balance
        self.nodes[1].sendrawtransaction(txObjNotBroadcasted['hex'])
        self.nodes[1].generate(1)
        self.sync_all()
        node_2_bal += 2
        txObjNotBroadcasted = self.nodes[0].gettransaction(txIdNotBroadcasted)
        assert_equal(self.nodes[2].getbalance(), node_2_bal)

        #create another tx
        txIdNotBroadcasted  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 2)

        #restart the nodes with -walletbroadcast=1
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.node_args = [['-usehd=0'], ['-usehd=0'], ['-usehd=0']]
        self.nodes = start_nodes(3, self.options.tmpdir, self.node_args)
        connect_nodes_full(self.nodes)
        sync_blocks(self.nodes)

        self.nodes[0].generate(1)
        sync_blocks(self.nodes)
        node_2_bal += 2

        #tx should be added to balance because after restarting the nodes tx should be broadcastet
        assert_equal(self.nodes[2].getbalance(), node_2_bal)

        #send a tx with value in a string (PR#6380 +)
        txId  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), "2")
        txObj = self.nodes[0].gettransaction(txId)
        assert_equal(txObj['amount'], Decimal('-2'))
        assert_equal(txObj['satoshi'], Decimal('-200000000'));

        txId  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), "0.0001")
        txObj = self.nodes[0].gettransaction(txId)
        assert_equal(txObj['amount'], Decimal('-0.0001'))
        assert_equal(txObj['satoshi'], Decimal('-10000'))

        #check if JSON parser can handle scientific notation in strings
        txId  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), "1e-4")
        txObj = self.nodes[0].gettransaction(txId)
        assert_equal(txObj['amount'], Decimal('-0.0001'))

        try:
            txId  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), "1f-4")
        except JSONRPCException as e:
            assert("Error parsing JSON:1f-4" in e.error['message'])
        else:
            raise AssertionError("Must not parse invalid amounts")

        # this is now a valid call because we convert on server side instead of client side
        self.nodes[0].generate("2")

        # Import address and private key to check correct behavior of spendable unspents
        # 1. Send some coins to generate new UTXO
        address_to_import = self.nodes[2].getnewaddress()
        txid = self.nodes[0].sendtoaddress(address_to_import, 1)
        self.nodes[0].generate(1)
        self.sync_all()

        # 2. Import address from node2 to node1
        self.nodes[1].importaddress(address_to_import)

        # 3. Validate that the imported address is watch-only on node1
        assert(self.nodes[1].validateaddress(address_to_import)["iswatchonly"])

        # 4. Check that the unspents after import are not spendable
        assert_array_result(self.nodes[1].listunspent(),
                           {"address": address_to_import},
                           {"spendable": False})

        # 5. Import private key of the previously imported address on node1
        priv_key = self.nodes[2].dumpprivkey(address_to_import)
        self.nodes[1].importprivkey(priv_key)

        # 6. Check that the unspents are now spendable on node1
        assert_array_result(self.nodes[1].listunspent(),
                           {"address": address_to_import},
                           {"spendable": True})

        # Mine a block from node0 to an address from node1
        cbAddr = self.nodes[1].getnewaddress()
        blkHash = self.nodes[0].generatetoaddress(1, cbAddr)[0]
        cbTxId = self.nodes[0].getblock(blkHash)['tx'][0]
        self.sync_all()

        # Check that the txid and balance is found by node1
        try:
            self.nodes[1].gettransaction(cbTxId)
        except JSONRPCException as e:
            assert("Invalid or non-wallet transaction id" not in e.error['message'])

        sync_blocks(self.nodes)

        # test multiple private key import, and watch only address import
        bal = self.nodes[2].getbalance()
        addrs = [ self.nodes[1].getnewaddress() for i in range(0,21)]
        pks   = [ self.nodes[1].dumpprivkey(x) for x in addrs]
        for a in addrs:
            self.nodes[0].sendtoaddress(a, 1)
        self.nodes[0].generate(1)
        sync_blocks(self.nodes)
        self.nodes[2].importprivatekeys(pks[0], pks[1])
        waitForRescan(self.nodes[2])
        assert(bal + 2 == self.nodes[2].getbalance())
        self.nodes[2].importprivatekeys("rescan", pks[2], pks[3])
        waitForRescan(self.nodes[2])
        assert(bal + 4 == self.nodes[2].getbalance())
        self.nodes[2].importprivatekeys("no-rescan", pks[4], pks[5])
        time.sleep(1)
        assert(bal + 4 == self.nodes[2].getbalance())  # since the recan didn't happen, there won't be a balance change
        self.nodes[2].importaddresses("rescan") # force a rescan although we imported nothing
        waitForRescan(self.nodes[2])
        assert(bal + 6 == self.nodes[2].getbalance())

        # import 5 addresses each (bug fix check)
        self.nodes[2].importaddresses(addrs[6], addrs[7], addrs[8], addrs[9], addrs[10])  # import watch only addresses
        waitForRescan(self.nodes[2])
        assert(bal + 6 == self.nodes[2].getbalance()) # since watch only, won't show in balance
        assert(bal + 11 == self.nodes[2].getbalance("*",1,True)) # show the full balance

        self.nodes[2].importaddresses("rescan", addrs[11], addrs[12], addrs[13], addrs[14], addrs[15])  # import watch only addresses
        waitForRescan(self.nodes[2])
        assert(bal + 6 == self.nodes[2].getbalance()) # since watch only, won't show in balance
        assert(bal + 16 == self.nodes[2].getbalance("*",1,True)) # show the full balance

        self.nodes[2].importaddresses("no-rescan", addrs[16], addrs[17], addrs[18], addrs[19], addrs[20])  # import watch only addresses
        time.sleep(1)
        assert(bal + 6 == self.nodes[2].getbalance()) # since watch only, won't show in balance
        assert(bal + 16 == self.nodes[2].getbalance("*",1,True)) # show the full balance, will be same because no rescan
        self.nodes[2].importaddresses("rescan") # force a rescan although we imported nothing
        waitForRescan(self.nodes[2])
        assert(bal + 21 == self.nodes[2].getbalance("*",1,True)) # show the full balance

        # verify that none of the importaddress calls added the address with a label (bug fix check)
        txns = self.nodes[2].listreceivedbyaddress(0, True, True)
        for i in range(6,21):
            assert_array_result(txns,
                               {"address": addrs[i]},
                               {"label": ""})

        # now try P2SH
        btcAddress = self.nodes[1].getnewaddress()
        btcAddress = self.nodes[1].getaddressforms(btcAddress)["legacy"]
        ( p2shAddress, redeemScript) = GenerateSingleSigP2SH(btcAddress)
        self.nodes[0].sendtoaddress(p2shAddress,1)

        btcAddress2 = self.nodes[1].getnewaddress()
        btcAddress2 = self.nodes[1].getaddressforms(btcAddress2)["legacy"]
        ( p2shAddress2, redeemScript2) = GenerateSingleSigP2SH(btcAddress2)
        self.nodes[0].sendtoaddress(p2shAddress2,1)

        self.nodes[0].generate(1)
        sync_blocks(self.nodes)

        bal1 = self.nodes[2].getbalance('*', 1, True)
        self.nodes[2].importaddresses(hexlify(redeemScript).decode("ascii"),hexlify(redeemScript2).decode("ascii"))
        waitForRescan(self.nodes[2])
        bal2 = self.nodes[2].getbalance('*', 1, True)
        assert_equal(bal1 + 2, bal2)

        # verify that none of the importaddress calls added the address with a label (bug fix check)
        txns = self.nodes[2].listreceivedbyaddress(0, True, True)
        assert_array_result(txns,
                            {"address": self.nodes[2].getaddressforms(p2shAddress)["bitcoincash"]},
                            {"label": ""})
        assert_array_result(txns,
                            {"address": self.nodes[2].getaddressforms(p2shAddress2)["bitcoincash"]},
                            {"label": ""})

        #check if wallet or blochchain maintenance changes the balance
        self.sync_all()
        blocks = self.nodes[0].generate(2)
        self.sync_all()
        balance_nodes = [self.nodes[i].getbalance() for i in range(3)]
        block_count = self.nodes[0].getblockcount()

        # Check modes:
        #   - True: unicode escaped as \u....
        #   - False: unicode directly as UTF-8
        for mode in [True, False]:
            self.nodes[0].ensure_ascii = mode
            # unicode check: Basic Multilingual Plane, Supplementary Plane respectively
            for s in [u'рыба', u'𝅘𝅥𝅯']:
                addr = self.nodes[0].getaccountaddress(s)
                label = self.nodes[0].getaccount(addr)
                assert_equal(label, s)
                assert(s in self.nodes[0].listaccounts().keys())
        self.nodes[0].ensure_ascii = True # restore to default

        # maintenance tests
        maintenance = [
            '-rescan',
            '-reindex',
            '-zapwallettxes=1',
            '-zapwallettxes=2',
            '-salvagewallet',
        ]
        for m in maintenance:
            logging.info("check " + m)
            stop_nodes(self.nodes)
            wait_bitcoinds()
            self.node_args = [['-usehd=0', m], ['-usehd=0', m], ['-usehd=0', m]]
            self.nodes = start_nodes(3, self.options.tmpdir, self.node_args)
            # wait for blockchain to catch up
            waitFor(60, lambda : [block_count] * 3 == [self.nodes[i].getblockcount() for i in range(3)])
            # wait for wallet to catch up to blockchain
            waitFor(60, lambda : balance_nodes == [self.nodes[i].getbalance() for i in range(3)], lambda: print("balances: " + str([self.nodes[i].getbalance() for i in range(3)]) + " expecting: " + str(balance_nodes)))

        # Exercise listsinceblock with the last two blocks
        coinbase_tx_1 = self.nodes[0].listsinceblock(blocks[0])
        assert_equal(coinbase_tx_1["lastblock"], blocks[1])
        assert_equal(len(coinbase_tx_1["transactions"]), 1)
        assert_equal(coinbase_tx_1["transactions"][0]["blockhash"], blocks[1])
        assert_equal(coinbase_tx_1["transactions"][0]["satoshi"], Decimal('2500000000'))
        assert_equal(len(self.nodes[0].listsinceblock(blocks[1])["transactions"]), 0)


if __name__ == '__main__':
    WalletTest ().main ()

def Test():
    t = WalletTest()
    bitcoinConf = {
        "debug": ["selectcoins", "rpc","net", "blk", "thin", "mempool", "req", "bench", "evict"]
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
