#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test ZMQ interface
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import zmq
import struct

import http.client
import urllib.parse

class ZMQTest (BitcoinTestFramework):

    port = 28332

    def setup_nodes(self):
        self.zmqContext = zmq.Context()
        self.zmqSubSocket = self.zmqContext.socket(zmq.SUB)
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashblock")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashtx")
        self.zmqSubSocket.linger = 500
        self.zmqSubSocket.connect("tcp://127.0.0.1:%i" % self.port)
        return start_nodes(4, self.options.tmpdir, extra_args=[
            ['-zmqpubhashtx=tcp://127.0.0.1:'+str(self.port), '-zmqpubhashblock=tcp://127.0.0.1:'+str(self.port)],
            [],
            [],
            []
            ])

    def run_test(self):
        try:
            self.sync_all()

            genhashes = self.nodes[0].generate(1)
            self.sync_all()

            print("listen...")
            msg = self.zmqSubSocket.recv_multipart()
            topic = msg[0]
            body = msg[1]

            msg = self.zmqSubSocket.recv_multipart()
            topic = msg[0]
            body = msg[1]
            blkhash = bytes_to_hex_str(body)

            assert_equal(genhashes[0], blkhash) #blockhash from generate must be equal to the hash received over zmq

            n = 10
            genhashes = self.nodes[1].generate(n)
            self.sync_all()

            zmqHashes = []
            for x in range(0,n*2):
                msg = self.zmqSubSocket.recv_multipart()
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
            msg = self.zmqSubSocket.recv_multipart()
            topic = msg[0]
            body = msg[1]
            hashZMQ = ""
            if topic == b"hashtx":
                hashZMQ = bytes_to_hex_str(body)

            assert_equal(hashRPC, hashZMQ) #blockhash from generate must be equal to the hash received over zmq
        finally:
            self.zmqSubSocket.close()
            self.zmqSubSocket = None
            self.zmqContext.destroy()
            self.zmqContext = None


if __name__ == '__main__':
    ZMQTest ().main ()

def Test():
    ZMQTest ().main ()
 
