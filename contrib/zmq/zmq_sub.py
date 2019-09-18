#!/usr/bin/env python3

import array
import binascii
import zmq

port = 28332

zmqContext = zmq.Context()
zmqSubSocket = zmqContext.socket(zmq.SUB)
zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashblock")
zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashtx")
zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"rawblock")
zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"rawtx")
zmqSubSocket.connect("tcp://127.0.0.1:%i" % port)

try:
    while True:
        msg = zmqSubSocket.recv_multipart()
        topic = msg[0]
        body = msg[1]
        if topic == b"hashblock":
            print("- HASH BLOCK -")
            print(binascii.hexlify(body).decode())
        elif topic == b"hashtx":
            print('- HASH TX -')
            print(binascii.hexlify(body).decode())
        elif topic == b"rawblock":
            print("- RAW BLOCK HEADER -")
            print(binascii.hexlify(body[:80]).decode())
        elif topic == b"rawtx":
            print('- RAW TX -')
            print(binascii.hexlify(body).decode())
        else:
            print("unknown topic: ", topic)

except KeyboardInterrupt:
    zmqContext.destroy()
