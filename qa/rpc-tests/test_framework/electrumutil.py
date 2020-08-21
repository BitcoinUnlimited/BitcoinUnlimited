# Copyright (c) 2019 The Bitcoin Unlimited developers

from test_framework.loginit import logging
import socket
import json
import asyncio
from . import cashaddr
from .script import *
from test_framework.connectrum.client import StratumClient
from test_framework.connectrum.svr_info import ServerInfo
from test_framework.util import waitFor

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
            "-electrum.monitoring.port=" + str(random.randint(40000, 60000)),
            "-electrum.rawarg=--cashaccount-activation-height=1"]

class ElectrumConnection:
    def __init__(self,):
        self.cli = StratumClient()

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


    async def call(self, method, *args):
        return await self.cli.RPC(method, *args)

    async def subscribe(self, method, *args):
        future, queue = self.cli.subscribe(method, *args)
        result = await future
        return result, queue

def script_to_scripthash(script):
    import hashlib
    scripthash = hashlib.sha256(script).digest()

    # Electrum wants little endian
    scripthash = bytearray(scripthash)
    scripthash.reverse()

    return scripthash.hex()


# To look up an address with the electrum protocol, you need the hash
# of the locking script (scriptpubkey). Assumes P2PKH.
def address_to_scripthash(addr):
    _, _, hash160 = cashaddr.decode(addr)
    script = CScript([OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG])
    return script_to_scripthash(script)

def sync_electrum_height(node, timeout = 10):
    waitFor(timeout, lambda: compare(node, "index_height", node.getblockcount()))

def wait_for_electrum_mempool(node, *, count, timeout = 10):
    try:
        waitFor(timeout, lambda: compare(node, "mempool_count", count, True))
    except Exception as e:
        print("Waited for {} txs, had {}".format(count, node.getelectruminfo()['debuginfo']['electrscash_mempool_count']))
        raise
