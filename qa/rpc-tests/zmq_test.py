#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test ZMQ interface
#

import time
import test_framework.loginit
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import zmq
import struct

import http.client
import urllib.parse

def ZmqReceive(socket, timeout=30):
    start = time.time()
    while True:
        try:
            return socket.recv_multipart()
        except zmq.ZMQError as e:
            if e.errno != zmq.EAGAIN or time.time() - start >= timeout:
                raise
            time.sleep(0.05)



class ZMQTest (BitcoinTestFramework):

    port = 28340 # ZMQ ports of these test must be unique so multiple tests can be run simultaneously

    def setup_nodes(self):
        self.zmqContext = zmq.Context()
        self.zmqSubSocket = self.zmqContext.socket(zmq.SUB)
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashblock")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashtx")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashds")
        self.zmqSubSocket.RCVTIMEO = 30000
        self.zmqSubSocket.linger = 500
        self.zmqSubSocket.connect("tcp://127.0.0.1:%i" % self.port)
        return start_nodes(4, self.options.tmpdir, extra_args=[
            ['-zmqpubhashtx=tcp://127.0.0.1:'+str(self.port), '-zmqpubhashblock=tcp://127.0.0.1:'+str(self.port), '-zmqpubhashds=tcp://127.0.0.1:'+str(self.port), '-debug=respend', '-debug=dsproof', '-debug=mempool', '-debug=net', '-debug=zmq'],
            ['-debug=respend', '-debug=dsproof', '-debug=mempool', '-debug=net', '-debug=zmq'],
            ['-debug=respend', '-debug=dsproof', '-debug=mempool', '-debug=net', '-debug=zmq'],
            []
            ])

    def run_test(self):
        try:
            self.sync_all()

            genhashes = self.nodes[0].generate(1)
            self.sync_all()

            print("listen...")
            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]

            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]
            if topic == b"hashblock":
                blkhash = bytes_to_hex_str(body)
            assert_equal(genhashes[0], blkhash) #blockhash from generate must be equal to the hash received over zmq

            n = 10
            genhashes = self.nodes[1].generate(n)
            self.sync_all()

            zmqHashes = []
            for x in range(0,n*2):
                msg = ZmqReceive(self.zmqSubSocket)
                topic = msg[0]
                body = msg[1]
                if topic == b"hashblock":
                    zmqHashes.append(bytes_to_hex_str(body))

            for x in range(0,n):
                assert_equal(genhashes[x], zmqHashes[x]) #blockhash from generate must be equal to the hash received over zmq

            #test tx from a second node
            hashRPC = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), 1.0)
            self.sync_all()

            # now we should receive a zmq msg because the tx was broadcast
            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]
            hashZMQ = ""
            if topic == b"hashtx":
                hashZMQ = bytes_to_hex_str(body)
            assert_equal(hashRPC, hashZMQ) #tx hash from generate must be equal to the hash received over zmq

            # Send all coins to a single new address so that we can be sure that we
            # try double spending a p2pkh output in the subsequent step.
            wallet = self.nodes[0].listunspent()
            inputs = []
            num_coins = 0
            for t in wallet:
                inputs.append({ "txid" : t["txid"], "vout" : t["vout"]})
                num_coins += 1
            outputs = { self.nodes[0].getnewaddress() : num_coins * COINBASE_REWARD-Decimal("0.05") }
            rawtx   = self.nodes[0].createrawtransaction(inputs, outputs)
            rawtx   = self.nodes[0].signrawtransaction(rawtx)
            try:
                hashRPC   = self.nodes[0].sendrawtransaction(rawtx['hex'])
            except JSONRPCException as e:
                print(e.error['message'])
                assert(False)
            self.sync_all()

            #check we received zmq notification
            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]
            hashZMQ = ""
            if topic == b"hashtx":
                hashZMQ = bytes_to_hex_str(body)
            assert_equal(hashRPC, hashZMQ) #tx hash from generate must be equal to the hash received over zmq

            hashRPC = self.nodes[1].generate(1)
            self.sync_all()

            #check we received zmq notification
            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]
            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]
            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]
            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]
            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]

            hashZMQ = ""
            if topic == b"hashblock":
                hashZMQ = bytes_to_hex_str(body)
            assert_equal(hashRPC[0], hashZMQ) #blockhash from generate must be equal to the hash received over zmq

            # Send 2 transactions that double spend each another
            wallet = self.nodes[0].listunspent()
            walletp2pkh = list(filter(lambda x : len(x["scriptPubKey"]) != 70, wallet))  # Find an input that is not P2PK
            t = walletp2pkh.pop()
            inputs = []
            inputs.append({ "txid" : t["txid"], "vout" : t["vout"]})
            outputs = { self.nodes[1].getnewaddress() : t["amount"] }

            rawtx   = self.nodes[0].createrawtransaction(inputs, outputs)
            rawtx   = self.nodes[0].signrawtransaction(rawtx)
            try:
                hashTxToDoubleSpend   = self.nodes[1].sendrawtransaction(rawtx['hex'])
            except JSONRPCException as e:
                print(e.error['message'])
                assert(False)
            self.sync_all()

            #check we received zmq notification
            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]
            hashZMQ = ""
            if topic == b"hashtx":
                hashZMQ = bytes_to_hex_str(body)
            assert_equal(hashTxToDoubleSpend, hashZMQ) #tx hash from generate must be equal to the hash received over zmq

            outputs = { self.nodes[1].getnewaddress() : t["amount"] }
            rawtx   = self.nodes[0].createrawtransaction(inputs, outputs)
            rawtx   = self.nodes[0].signrawtransaction(rawtx)
            try:
                hashtx   = self.nodes[0].sendrawtransaction(rawtx['hex'])
            except JSONRPCException as e:
                assert("txn-mempool-conflict" in e.error['message'])
            else:
                assert(False)
            self.sync_all()

            # now we should receive a zmq ds msg because the tx was broadcast
            msg = ZmqReceive(self.zmqSubSocket)
            topic = msg[0]
            body = msg[1]
            hashZMQ = ""
            if topic == b"hashds":
                hashZMQ = bytes_to_hex_str(body)
            assert_equal(hashTxToDoubleSpend, hashZMQ) #double spent tx hash from generate must be equal to the hash received over zmq

        finally:
            self.zmqSubSocket.close()
            self.zmqSubSocket = None
            self.zmqContext.destroy()
            self.zmqContext = None


if __name__ == '__main__':
    ZMQTest ().main ()

def Test():
    flags = standardFlags()
    t = ZMQTest()
    t.drop_to_pdb = True
    t.main(flags)
 
