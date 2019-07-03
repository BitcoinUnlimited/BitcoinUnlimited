# Copyright (c) 2019 The Bitcoin Unlimited developers

from test_framework.loginit import logging
import socket
import json
from . import cashaddr
from .script import *

ELECTRUM_PORT = None

def compare(node, key, expected, is_debug_data = False):
    info = node.getelectruminfo()
    if is_debug_data:
        info = info['debuginfo']
        key = "electrs_" + key
    logging.debug("expecting %s == %s from %s", key, expected, info)
    if key not in info:
        return False
    return info[key] == expected

def bitcoind_electrum_args():
    import random
    global ELECTRUM_PORT
    ELECTRUM_PORT = random.randint(40000, 60000)
    return ["-electrum=1", "-debug=electrum", "-debug=rpc",
            "-electrum.port=" + str(ELECTRUM_PORT),
            "-electrum.monitoring.port=" + str(random.randint(40000, 60000))]

class ElectrumConnection:
    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", ELECTRUM_PORT))
        self.f = self.s.makefile('r')
        self.id = 0

    def call(self, method, *args):
        req = {
            'id': self.id,
            'method': method,
            'params': list(args),
        }
        msg = json.dumps(req) + '\n'
        self.s.sendall(msg.encode('ascii'))
        res = self.f.readline()
        return json.loads(res)

# Helper function to attempt several times to connect to electrum server.
# At startup, it may take a while before the server accepts connections.
def create_electrum_connection(timeout = 30):
    import time
    start = time.time()
    err = None
    while time.time() < (start + timeout):
        try:
            return ElectrumConnection()
        except Exception as e:
            err = e
            time.sleep(1)

    raise Exception("Failed to connect to electrum server. Error '%s'" % err)

# To look up an address with the electrum protocol, you need the hash
# of the locking script (scriptpubkey)
def address_to_scripthash(addr):
    _, _, hash160 = cashaddr.decode(addr)
    script = CScript([OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG])

    import hashlib
    scripthash = hashlib.sha256(script).digest()

    # Electrum wants little endian
    scripthash = bytearray(scripthash)
    scripthash.reverse()

    return scripthash.hex()

