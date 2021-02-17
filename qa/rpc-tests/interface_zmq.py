#!/usr/bin/env python3
# Copyright (c) 2015-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the ZMQ notification interface."""
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging
import struct
from io import BytesIO

from test_framework.test_framework import BitcoinTestFramework
from test_framework.nodemessages import CTransaction
from test_framework.util import *

try:
    import zmq
except ModuleNotFoundError:
    print("zmq module not found")
    print("you need to install it to run this test: 'sudo pip3 install zmq'")
    sys.exit(-1)

class ZMQSubscriber:
    def __init__(self, socket, topic):
        self.sequence = 0
        self.socket = socket
        self.subscribe(topic)

    def subscribe(self, topic = None):
        if not topic is None:
            self.topic = topic
        self.socket.setsockopt(zmq.SUBSCRIBE, self.topic)

    def unsubscribe(self):
        self.socket.setsockopt(zmq.UNSUBSCRIBE, self.topic)

    def receive(self, timeout=30):
        start = time.time()
        tmp = None
        while True:
            try:
                tmp = self.socket.recv_multipart()
                break
            except zmq.ZMQError as e:
                if e.errno != zmq.EAGAIN or time.time() - start >= timeout:
                    raise
                time.sleep(0.25)
        topic = tmp[0]
        body = tmp[1]
        # Topic should match the subscriber topic.
        assert_equal(topic, self.topic)

        if len(tmp) >= 3:
            # Sequence should be incremental.
            seq = tmp[2]
            assert_equal(struct.unpack('<I', seq)[-1], self.sequence)
            self.sequence += 1
        return body


class ZMQTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2

    def setup_nodes(self):
        # Initialize ZMQ context and socket.
        # All messages are received in the same socket which means that this
        # test fails if the publishing order changes.
        # Note that the publishing order is not defined in the documentation and
        # is subject to change.
        address = "tcp://127.0.0.1:28342" # ZMQ ports of these test must be unique so multiple tests can be run simultaneously
        self.zmq_context = zmq.Context()
        socket = self.zmq_context.socket(zmq.SUB)
        socket.set(zmq.RCVTIMEO, 60000)
        socket.connect(address)

        # Subscribe to all available topics.
        self.hashblock = ZMQSubscriber(socket, b"hashblock")
        self.hashtx = ZMQSubscriber(socket, b"hashtx")
        self.rawblock = ZMQSubscriber(socket, b"rawblock")
        self.rawtx = ZMQSubscriber(socket, b"rawtx")

        self.hashds = ZMQSubscriber(socket, b"hashds")
        self.rawds = ZMQSubscriber(socket, b"rawds")

        self.extra_args = [["-zmqpub{}={}".format(sub.topic.decode(), address) for sub in [
            self.hashblock, self.hashtx, self.rawblock, self.rawtx, self.hashds, self.rawds]], []]
        self.extra_args[0].append("-debug=dsproof")
        self.extra_args[0].append("-debug=zmq")
        ret  = start_nodes(self.num_nodes, self.options.tmpdir, self.extra_args)
        return ret

    def run_test(self):
        try:
            self._zmq_test()
        finally:
            # Destroy the ZMQ context.
            logging.debug("Destroying ZMQ context")
            self.zmq_context.destroy(linger=None)

    def _zmq_test(self):
        """Note that this function is very picky about the exact order of generated ZMQ announcements.
           However, this order does not actually matter.  So bitcoind code changes my break this test.
        """
        num_blocks = 5
        logging.info(
            "Generate {0} blocks (and {0} coinbase txes)".format(num_blocks))

        # DS does not support P2PK so make sure there's a P2PKH in the wallet
        addr = self.nodes[0].getnewaddress()
        fundTx = self.nodes[0].sendtoaddress(addr, 10)
        # Notify of new tx
        zmqNotif = self.hashtx.receive().hex()
        assert fundTx == zmqNotif
        zmqNotif = self.rawtx.receive()

        genhashes = self.nodes[0].generate(1)
        # notify tx 1
        zmqNotif1 = self.hashtx.receive().hex()
        assert fundTx == zmqNotif1
        zmqNotif1r = self.rawtx.receive()
        # notify coinbase
        zmqNotif2 = self.hashtx.receive().hex()
        zmqNotif2r = self.rawtx.receive()
        assert b"/EB32/AD12" in zmqNotif2r
        # notify tx 1 again
        zmqNotif = self.hashtx.receive().hex()
        assert fundTx == zmqNotif
        zmqNotif = self.rawtx.receive()

        # notify the block
        h = self.hashblock.receive().hex()
        b = self.rawblock.receive()

        genhashes = self.nodes[0].generate(num_blocks)

        self.sync_all()

        for x in range(num_blocks):
            # Should receive the coinbase txid.
            txid = self.hashtx.receive()
            # Should receive the coinbase raw transaction.
            hex = self.rawtx.receive()
            tx = CTransaction()
            tx.deserialize(BytesIO(hex))
            tx.calc_sha256()
            assert_equal(tx.hash, txid.hex())


            # Should receive the generated block hash.
            hash = self.hashblock.receive().hex()
            assert_equal(genhashes[x], hash)
            # The block should only have the coinbase txid.
            assert_equal([txid.hex()], self.nodes[1].getblock(hash)["tx"])

            # Should receive the generated raw block.
            block = self.rawblock.receive()
            assert_equal(genhashes[x], hash256(block[:80])[::-1].hex())

        logging.info("Wait for tx from second node")
        payment_txid = self.nodes[1].sendtoaddress(
            self.nodes[0].getnewaddress(), 1.0)
        self.sync_all()

        # Should receive the broadcasted txid.
        txid = self.hashtx.receive()
        assert_equal(payment_txid, txid.hex())

        # Should receive the broadcasted raw transaction.
        hex = self.rawtx.receive()
        assert_equal(payment_txid, hash256(hex)[::-1].hex())

        if 1: # Send 2 transactions that double spend each other

            # If these unsubscribes fail, then you will get an assertion that a zmq topic is not correct
            self.hashtx.unsubscribe()
            self.rawtx.unsubscribe()

            wallet = self.nodes[0].listunspent()
            walletp2pkh = list(filter(lambda x : len(x["scriptPubKey"]) != 70, wallet)) # Find an input that is not P2PK
            t = walletp2pkh.pop()
            inputs = []
            inputs.append({ "txid" : t["txid"], "vout" : t["vout"]})
            outputs = { self.nodes[1].getnewaddress() : t["amount"] }

            rawtx   = self.nodes[0].createrawtransaction(inputs, outputs)
            rawtx   = self.nodes[0].signrawtransaction(rawtx)
            mpTx = self.nodes[0].getmempoolinfo()["size"]
            try:
                hashTxToDoubleSpend   = self.nodes[1].sendrawtransaction(rawtx['hex'])
            except JSONRPCException as e:
                print(e.error['message'])
                assert False
                self.sync_all()

            outputs = { self.nodes[1].getnewaddress() : t["amount"] }
            rawtx   = self.nodes[0].createrawtransaction(inputs, outputs)
            rawtx   = self.nodes[0].signrawtransaction(rawtx)
            waitFor(30, lambda: self.nodes[0].getmempoolinfo()["size"] > mpTx)  # make sure the original tx propagated in time
            try:
                hashtx   = self.nodes[0].sendrawtransaction(rawtx['hex'])
            except JSONRPCException as e:
                assert("txn-mempool-conflict" in e.error['message'])
            else:
                assert(False)
                self.sync_all()

            # since I unsubscribed from these I don't need to load them
            #self.hashtx.receive()
            #self.rawtx.receive()

            # Should receive hash of a double spend proof.
            dsTxHash = self.hashds.receive()
            assert hashTxToDoubleSpend == dsTxHash.hex()
            ds  = self.rawds.receive()
            assert len(ds) > 0


if __name__ == '__main__':
    ZMQTest().main()

def Test():
    flags = standardFlags()
    t = ZMQTest()
    t.drop_to_pdb = True
    t.main(flags)
    print("completed!")
