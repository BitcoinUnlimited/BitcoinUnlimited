# Copyright (c) 2019 The Bitcoin Unlimited developers

from test_framework.loginit import logging
import socket
import json

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
