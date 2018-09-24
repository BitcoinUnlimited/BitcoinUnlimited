#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
# This test exercises the mempool acceptance data path.

from threading import Thread
import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

import test_framework.cashlib as cashlib
from test_framework.nodemessages import *
from test_framework.script import *


class PayDest:
    """A payment destination.  All the info you need to send a payment here and make a subsequent payment 
       from this address"""

    def __init__(self, node=None):
        """Pass a node to use an address from that node's wallet.  Pass None to generate a local address"""
        if node is None:
            self.privkey = cashlib.randombytes(32)
        else:
            addr = node.getnewaddress()
            privb58 = node.dumpprivkey(addr)
            self.privkey = decodeBase58(privb58)[1:-5]
        self.pubkey = cashlib.pubkey(self.privkey)
        self.hash = cashlib.addrbin(self.pubkey)

    def __str__(self):
        return "priv:%s pub:%s hash:%s" % (hexlify(self.privkey), hexlify(self.pubkey), hexlify(self.hash))


def createConflictingTx(dests, source, count, fee=1):
    """ Create "count" conflicting transactions that spend the "source" to "dests" evenly.  Conflicting tx are created
    by varying the fee.  Change the base "fee" if you want which is actually the fee PER dest.

    source: a dictionary in RPC listunspent format, with additional "privkey" field which is the private key in bytes
    dests: a list of PayDest objects.
    count: the number of conflicting tx to return
    fee: what to deduct as the fee (in Satoshi)
    """
    generatedTx = []
    hexOP_DUP = OP_DUP.toHex()
    binOP_DUP = ord(OP_DUP.toBin())

    for c in range(count):
        w = source
        if 1:
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(w["txid"], w["vout"]), b"", 0xffffffff))

            amt = int(w["satoshi"] / len(dests)) - (fee + c)  # really total fee ends up fee*dest

            i = 0
            for d in dests:
                script = CScript([OP_DUP, OP_HASH160, d.hash, OP_EQUALVERIFY, OP_CHECKSIG])
                tx.vout.append(CTxOut(amt, script))
                i += 1

            sighashtype = 0x41
            sig = cashlib.signTxInput(tx, 0, w["satoshi"], w["scriptPubKey"], w["privkey"], sighashtype)
            # construct the signature script -- it may be one of 2 types
            if w["scriptPubKey"][0:2] == hexOP_DUP or w["scriptPubKey"][0] == binOP_DUP:  # P2PKH starts with OP_DUP
                tx.vin[0].scriptSig = cashlib.spendscript(sig, w["pubkey"])  # P2PKH
            else:
                tx.vin[0].scriptSig = cashlib.spendscript(sig)  # P2PK

            generatedTx.append(tx)

    return generatedTx


def createTx(dests, sources, node, maxx=None, fee=1, nextWallet=None, generatedTx=None):
    """ Create "maxx" transactions that spend from individual "sources" to every "dests" evenly (many fan-out 
    transactions).  If "generatedTx" is a list the created transactions are put into it.  Otherwise they are
    sent to "node".  If "nextWallet" is a list the outputs of all these created tx are put into it in a format
    compatible with "sources" (you can use nextWallet as the sources input in a subsequent call to this function).

    Change the base "fee" if you want which is actually the fee PER dest.

    sources: list of dictionaries in RPC listunspent format, with optional additional "privkey" field which 
       is the private key in bytes.  If "privkey" does not exist, "node" is asked for it.
    dests: a list of PayDest objects.
    fee: what to deduct as the fee (in Satoshi)
    nextWallet: [output] pass an empty list to get a valid wallet if all the createdTx are committed.
    generatedTx: [output] pass an empty list to skip submitting the tx to node, and instead return them in this list.

    returns the number of transactions created.
    """

    hexOP_DUP = OP_DUP.toHex()
    binOP_DUP = ord(OP_DUP.toBin())
    count = 0
    for w in sources:
        nextOuts = []
        if not count is None and count == maxx:
            break

        # if sources is from a bitcoind wallet, I need to grab some info in order to sign
        if not "privkey" in w:
            privb58 = node.dumpprivkey(w["address"])
            privkey = decodeBase58(privb58)[1:-5]
            pubkey = cashlib.pubkey(privkey)
            w["privkey"] = privkey
            w["pubkey"] = pubkey

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(w["txid"], w["vout"]), b"", 0xffffffff))

        amt = int(w["satoshi"] / len(dests)) - fee  # really fee ends up fee*dest

        i = 0
        for d in dests:
            script = CScript([OP_DUP, OP_HASH160, d.hash, OP_EQUALVERIFY, OP_CHECKSIG])
            tx.vout.append(CTxOut(amt, script))
            nextOuts.append({"vout": i, "privkey": d.privkey, "scriptPubKey": script,
                             "satoshi": amt, "pubkey": d.pubkey})
            i += 1

        sighashtype = 0x41
        n = 0
        # print("amountin: %d amountout: %d outscript: %s" % (w["satoshi"], amt, w["scriptPubKey"]))
        sig = cashlib.signTxInput(tx, n, w["satoshi"], w["scriptPubKey"], w["privkey"], sighashtype)

        if w["scriptPubKey"][0:2] == hexOP_DUP or w["scriptPubKey"][0] == binOP_DUP:  # P2PKH starts with OP_DUP
            tx.vin[n].scriptSig = cashlib.spendscript(sig, w["pubkey"])  # P2PKH
        else:
            tx.vin[n].scriptSig = cashlib.spendscript(sig)  # P2PK

        if not type(generatedTx) is list:  # submit these tx to the node
            txhex = hexlify(tx.serialize()).decode("utf-8")
            txid = None
            try:
                txid = node.enqueuerawtransaction(txhex)
            except JSONRPCException as e:
                logging.error("TX submission failed because %s" % str(e))
                logging.error("tx was: %s" % txhex)
                logging.error("amountin: %d amountout: %d outscript: %s" % (w["satoshi"], amt, w["scriptPubKey"]))
                raise
        else:  # return them in generatedTx
            generatedTx.append(tx)

        for out in nextOuts:
            tx.rehash()
            out["txid"] = tx.hash
            # I've already filled nextOuts with all the other needed fields

        if type(nextWallet) is list:
            nextWallet += nextOuts
        count += 1
    return count


class MyTest (BitcoinTestFramework):
    def __init__(self, bigTest=0):
        self.bigTest = bigTest
        BitcoinTestFramework.__init__(self)

    def setup_chain(self, bitcoinConfDict=None, wallets=None):
        logging.info("Initializing test directory " + self.options.tmpdir)
        initialize_chain(self.options.tmpdir, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        # Now interconnect the nodes
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def threadedCreateTx(self, dests, sources, nodeIdx, maxx=None):
        """Create a bunch of transactions using multiple python threads"""
        NUMTHREADS = 4
        if sources is None:
            wallets = self.nodes[nodeIdx].listunspent()
        else:
            wallets = sources
        total = min(len(wallets), maxx)
        logging.info("creating %d tx desired %d" % (total, maxx))

        wchunks = []
        count = 0
        for i in range(NUMTHREADS - 1):
            start = count
            count += int(total / NUMTHREADS)
            wchunks.append(wallets[start: count])
        wchunks.append(wallets[count:])

        threads = []
        outputs = []
        txPerThread = int(total / NUMTHREADS)
        for i in range(NUMTHREADS - 1):
            node = get_rpc_proxy(self.nodes[nodeIdx].url, nodeIdx, timeout=30)
            t = Thread(target=createTx, args=(dests, wchunks[i], node, txPerThread, 1, outputs))
            t.start()
            threads.append(t)

        createTx(dests, wchunks[NUMTHREADS - 1], self.nodes[nodeIdx],
                 total - (txPerThread * (NUMTHREADS - 1)), 1, outputs)
        for t in threads:
            t.join()
        return (total, outputs)

    def conflictTest(self, dests0, dests1, wallet, numNodes=2):
        """Tests issuing a bunch of conflicting transactions.  Expects that you give it a wallet with lots of free UTXO, and nothing in the mempool
        """
        assert(self.nodes[0].getmempoolinfo()["size"] == 0)  # Expects a clean mempool
        if 1:  # test many conflicts
            NTX = 50
            i = 0
            for c in range(NTX):
                source = wallet.pop()
                txs = createConflictingTx(dests0, source, c)

                for t in txs:
                    n = self.nodes[i % len(self.nodes)]
                    n.enqueuerawtransaction(t.toHex())
                    i += 1

            for n in self.nodes:
                waitFor(30, lambda: True if n.getmempoolinfo()["size"] >= NTX - 5 else None)
            time.sleep(5)
            # we have to allow < because bloom filter false positives in the node's
            # sending logic may cause it to not get an INV
            for n in self.nodes:
                assert(n.getmempoolinfo()["size"] <= NTX)  # if its > then a doublespend got through
            self.commitMempool()  # clear out this test

        if 1:  # test 2 conflicting transactions
            NTX = 100
            wallet2 = []
            gtx2 = []
            amt = createTx(dests0, wallet[0:NTX], 1, NTX, 1, wallet2, gtx2)
            wallet3 = []
            gtx3 = []
            # create conflicting tx with slightly different payment amounts
            amt = createTx(dests0, wallet[0:NTX], 1, NTX, 2, wallet3, gtx3)

            gtx = zip(gtx2, gtx3)
            for g in gtx:
                self.nodes[0].enqueuerawtransaction(g[0].toHex())
                try:
                    self.nodes[0].enqueuerawtransaction(g[1].toHex())
                    # assert(0)  # double spend may not return an error because processing happens asynchronously
                except JSONRPCException as e:
                    if e.error["code"] != -26:  # txn-mempool-conflict
                        raise

            # forget about the tx I used above
            wallet = wallet[NTX:]

            waitFor(30, lambda: True if self.nodes[0].getmempoolinfo()["size"] >= NTX else None)
            waitFor(30, lambda: True if self.nodes[1].getmempoolinfo()["size"] >= NTX else None)
            time.sleep(4)  # see if any conflicts will be added to the mempool

            assert(self.nodes[0].getmempoolinfo()["size"] == NTX)
            assert(self.nodes[1].getmempoolinfo()["size"] == NTX)

            NTX1 = NTX

            # test conflicting tx sent to different nodes
            NTX = 200
            wallet2 = []
            gtx2 = []
            amt = createTx(dests0, wallet[0:NTX], 1, NTX, 1, wallet2, gtx2)
            wallet3 = []
            gtx3 = []
            # create conflicting tx with slightly different payment amounts
            amt = createTx(dests0, wallet[0:NTX], 1, NTX, 2, wallet3, gtx3)

            gtx = zip(gtx2, gtx3)

            count = 0
            mempools = [x.getmempoolinfo() for x in self.nodes]
            for g in gtx:
                count += 1
                self.nodes[count % numNodes].enqueuerawtransaction(g[0].toHex())
                try:
                    self.nodes[(count + 1) % numNodes].enqueuerawtransaction(g[1].toHex())
                except JSONRPCException as e:
                    if e.error["code"] != -26:  # txn-mempool-conflict
                        pass  # we may get an error or not depending on propagation speed of 1st tx

            # There is no good way to tell if the mempool sync process has fully
            # completed because out of testing the process of accepting tx is never
            # complete
            waitFor(30, lambda: True if self.nodes[0].getmempoolinfo()["size"] >= NTX + NTX1 else None)
            waitFor(30, lambda: True if self.nodes[1].getmempoolinfo()["size"] >= NTX + NTX1 else None)

            time.sleep(4)  # see if any conflicts will be added to the mempool
            assert(self.nodes[0].getmempoolinfo()["size"] == NTX + NTX1)
            assert(self.nodes[1].getmempoolinfo()["size"] == NTX + NTX1)

            # forget about the tx I used
            wallet = wallet[NTX:]

    def commitMempool(self):
        """Commit all the tx in mempools on all nodes into blocks"""
        for n in self.nodes:
            while n.getmempoolinfo()["size"] != 0:
                n.generate(1)
                self.sync_blocks()

    def run_test(self):
        decContext = decimal.getcontext().prec
        decimal.getcontext().prec = 8 + 8  # 8 digits to get to 21million, and each bitcoin is 100 million satoshis

        self.nodes[0].generate(200)
        self.sync_blocks()

        # Get some addresses
        dests1 = [PayDest(self.nodes[1]) for x in range(20)]
        dests0 = [PayDest(self.nodes[0]) for x in range(20)]

        # Create 51 transaction and ensure that they get synced
        NTX = 101
        (amt, wallet) = self.threadedCreateTx(dests1, None, 0, NTX)
        assert(amt == NTX)
        waitFor(10, lambda: True if self.nodes[0].getmempoolinfo()["size"] >= NTX else None)
        mp = waitFor(30, lambda: [x.getmempoolinfo() for x in self.nodes] if amt - self.nodes[1].getmempoolinfo()
                     ["size"] < 5 else None, lambda: "timeout mempool is: " + str([x.getmempoolinfo() for x in self.nodes]))
        logging.info(mp)

        w0 = wallet[0:500]
        wallet = wallet[500:]
        self.commitMempool()
        self.conflictTest(dests0, dests1, w0)
        self.commitMempool()

        # Create 500 transaction and ensure that they get synced
        NTX = 500
        start = time.monotonic()
        (amt, wallet) = self.threadedCreateTx(dests0, wallet, 1, NTX)
        end = time.monotonic()
        logging.info("created %d tx in %s seconds. On node 0.  Speed %f tx/sec" %
                     (amt, end - start, float(amt) / (end - start)))
        mp = waitFor(20, lambda: [x.getmempoolinfo() for x in self.nodes] if amt - self.nodes[1].getmempoolinfo()
                     ["size"] < 10 else None, lambda: "timeout mempool is: " + str([x.getmempoolinfo() for x in self.nodes]))
        logging.info(mp)

        # Create 5000 transactions and ensure that they get synced
        if self.bigTest:
            self.commitMempool()
            NTX = 5000
            start = time.monotonic()
            (amt, wallet) = self.threadedCreateTx(dests1, wallet, 0, NTX)
            end = time.monotonic()
            logging.info("created %d tx in %s seconds. On node 0.  Speed %f tx/sec" %
                     (amt, end - start, float(amt) / (end - start)))
            mp = waitFor(300, lambda: [x.getmempoolinfo() for x in self.nodes] if amt - self.nodes[1].getmempoolinfo()
                     ["size"] < 20 else None, lambda: "timeout mempool is: " + str([x.getmempoolinfo() for x in self.nodes]))
            logging.info(mp)

        if self.bigTest:
            self.commitMempool()
            NTX = 10000
            start = time.monotonic()
            (amt, wallet) = self.threadedCreateTx(dests0, wallet, 1, NTX)
            end = time.monotonic()
            logging.info("created %d tx in %s seconds. On node 0.  Speed %f tx/sec" %
                         (amt, end - start, float(amt) / (end - start)))
            start = time.monotonic()
            mp = waitFor(300, lambda: [x.getmempoolinfo() for x in self.nodes] if amt - self.nodes[0].getmempoolinfo()[
                         "size"] < 50 else None, lambda: "timeout mempool is: " + str([x.getmempoolinfo() for x in self.nodes]))
            end = time.monotonic()
            logging.info("synced %d tx in %s seconds.  Speed %f tx/sec" %
                         (amt, end - start, float(amt) / (end - start)))
            logging.info(mp)

        # Now test pushing all of the mempool tx to other nodes
        NTX = self.nodes[0].getmempoolinfo()["size"]  # find how many I am pushing

        # Start up node 3
        self.nodes.append(start_node(2, self.options.tmpdir))
        connect_nodes_bi(self.nodes, 0, 2)
        sync_blocks(self.nodes)

        # Push all tx to node 3 from one node
        destName = "127.0.0.1:" + str(p2p_port(2))
        start = time.monotonic()
        self.nodes[0].pushtx(destName)
        mp = waitFor(120, lambda: [x.getmempoolinfo() for x in self.nodes]
                     if NTX - self.nodes[2].getmempoolinfo()["size"] < 30 else None)
        end = time.monotonic()
        logging.info("synced %d tx in %s seconds.  Speed %f tx/sec" % (NTX, end - start, float(NTX) / (end - start)))

        # Start up node 4
        self.nodes.append(start_node(3, self.options.tmpdir))
        connect_nodes_bi(self.nodes, 0, 3)
        connect_nodes_bi(self.nodes, 1, 3)
        connect_nodes_bi(self.nodes, 2, 3)
        sync_blocks(self.nodes)

        # Push all tx to node 4 from many nodes
        destName = "127.0.0.1:" + str(p2p_port(3))

        start = time.monotonic()
        self.nodes[0].pushtx(destName)
        self.nodes[1].pushtx(destName)
        self.nodes[2].pushtx(destName)
        mp = waitFor(120, lambda: [x.getmempoolinfo() for x in self.nodes]
                     if NTX - self.nodes[3].getmempoolinfo()["size"] < 30 else None)
        end = time.monotonic()
        logging.info("synced %d tx in %s seconds.  Speed %f tx/sec" % (NTX, end - start, float(NTX) / (end - start)))


if __name__ == '__main__':
    env = os.getenv("BITCOIND", None)
    path = None
    if env is None:
        for arg in sys.argv:
            if "srcdir" in arg:
                path = arg.split("=")[1]
                break
        if path is None:
            env = os.path.dirname(os.path.abspath(__file__))
            env = env + os.sep + ".." + os.sep + ".." + os.sep + "src" + os.sep + "bitcoind"
            env = os.path.abspath(env)
    if path is None:
        path = os.path.dirname(env)

    try:
        cashlib.init(path + os.sep + ".libs" + os.sep + "libbitcoincash.so")
        MyTest().main()
    except OSError as e:
        print("Issue loading shared library.  This is expected during cross compilation since the native python will not load the .so: %s" % str(e))


# Create a convenient function for an interactive python debugging session
def Test():
    t = MyTest(True)
    bitcoinConf = {
        "debug": ["blk", "mempool", "net", "req"],
        "blockprioritysize": 2000000,  # we don't want any transactions rejected due to insufficient fees...
        "net.ignoreTimeouts": 1,
        "logtimemicros": 1
    }

    # you may want these flags:
    flags = ["--nocleanup", "--noshutdown"]

    # Execution is much faster if a ramdisk is used, so use it if one exists in a typical location
    if os.path.isdir("/ramdisk/test"):
        flags.append("--tmpdir=/ramdisk/test/ma")

    # Out-of-source builds are awkward to start because they need an additional flag
    # automatically add this flag during testing for common out-of-source locations
    binpath = findBitcoind()
    flags.append("--srcdir=%s" % binpath)

    # load the cashlib.so from our build directory
    cashlib.init(binpath + os.sep + ".libs" + os.sep + "libbitcoincash.so")
    # start the test
    t.main(flags, bitcoinConf, None)
