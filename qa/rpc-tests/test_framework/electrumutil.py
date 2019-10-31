# Copyright (c) 2019 The Bitcoin Unlimited developers

from test_framework.loginit import logging
import socket
import json
import asyncio
from . import cashaddr
from .script import *
from test_framework.connectrum.client import StratumClient
from test_framework.connectrum.svr_info import ServerInfo

ELECTRUM_PORT = None

def compare(node, key, expected, is_debug_data = False):
    info = node.getelectruminfo()
    if is_debug_data:
        info = info['debuginfo']
        key = "electrscash_" + key
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
        self.cli = StratumClient()
        self.loop = asyncio.get_event_loop()

        self.loop.run_until_complete(self.connect())

    async def connect(self):
        connect_timeout = 30
        import time
        start = time.time()
        while True:
            try:
                await self.cli.connect(ServerInfo(None,
                    ip_addr = "127.0.0.1", ports = ELECTRUM_PORT))
                break

            except Exception as e:
                if time.time() >= (start + connect_timeout):
                    raise Exception("Failed to connect to electrum server. Error '{}'".format(e))

            time.sleep(1)


    def call(self, method, *args):
        return self.loop.run_until_complete(self.cli.RPC(method, *args))

def create_electrum_connection():
    return ElectrumConnection()

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

