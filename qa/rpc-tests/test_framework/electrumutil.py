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
from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import (
    P2PDataStore,
    NodeConn,
    NetworkThread,
)
from test_framework.util import assert_equal, p2p_port
from test_framework.blocktools import create_coinbase, create_block, \
    create_transaction, pad_tx
import time

ELECTRUM_PORT = None

class ElectrumTestFramework(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [bitcoind_electrum_args()]

    def bootstrap_p2p(self):
        """Add a P2P connection to the node."""
        self.p2p = P2PDataStore()
        self.connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], self.p2p)
        self.p2p.add_connection(self.connection)
        NetworkThread().start()
        self.p2p.wait_for_verack()
        assert(self.p2p.connection.state == "connected")

    def mine_blocks(self, n, num_blocks, txns = None):
        """
        Mine a block without using bitcoind
        """
        prev = n.getblockheader(n.getbestblockhash())
        prev_height = prev['height']
        prev_hash = prev['hash']
        prev_time = max(prev['time'] + 1, int(time.time()))
        blocks = [ ]
        for i in range(num_blocks):
            coinbase = create_coinbase(prev_height + 1)
            b = create_block(
                    hashprev = prev_hash,
                    coinbase = coinbase,
                    txns = txns,
                    nTime = prev_time + 1)
            txns = None
            b.solve()
            blocks.append(b)

            prev_time = b.nTime
            prev_height += 1
            prev_hash = b.hash

        self.p2p.send_blocks_and_test(blocks, n)
        assert_equal(blocks[-1].hash, n.getbestblockhash())

        # Return coinbases for spending later
        return [b.vtx[0] for b in blocks]

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
            "-electrum.rawarg=--cashaccount-activation-height=1",
            "-electrum.rawarg=--wait-duration-secs=1"]

class TestClient(StratumClient):
    is_connected = False

    def connection_lost(self, protocol):
        self.is_connected = False
        super().connection_lost(protocol)

class ElectrumConnection:
    def __init__(self, loop = None):
        self.cli = TestClient(loop)

    async def connect(self):
        connect_timeout = 30
        import time
        start = time.time()
        while True:
            try:
                await self.cli.connect(ServerInfo(None,
                    ip_addr = "127.0.0.1", ports = ELECTRUM_PORT))
                self.cli.is_connected = True
                break

            except Exception as e:
                if time.time() >= (start + connect_timeout):
                    raise Exception("Failed to connect to electrum server. Error '{}'".format(e))

            time.sleep(1)

    def disconnect(self):
        self.cli.close()

    async def call(self, method, *args):
        if not self.cli.is_connected:
            raise Exception("not connected")
        return await self.cli.RPC(method, *args)

    async def subscribe(self, method, *args):
        if not self.cli.is_connected:
            raise Exception("not connected")
        future, queue = self.cli.subscribe(method, *args)
        result = await future
        return result, queue

    def is_connected(self):
        return self.cli.is_connected

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

"""
Asserts that function call throw a electrum error, optionally testing for
the contents of the error.
"""
async def assert_response_error(call,
        error_code = None, error_string = None):
    from test_framework.connectrum.exc import ElectrumErrorResponse
    try:
        await call()
        raise AssertionError("assert_electrum_error: Error was not thrown.")
    except ElectrumErrorResponse as exception:
        res = exception.response

        if error_code is not None:
            if not 'code' in res:
                raise AssertionError(
                    "assert_response_error: Error code is missing in response")

            if res['code'] != error_code:
                raise AssertionError((
                    "assert_response_error: Expected error code {}, "
                    "got {} (Full response: {})".format(
                    error_code, res['code'], str(exception))))

        if error_string is not None:
            if not 'message' in res:
                raise AssertionError(
                    "assert_response_error: Error message is missing in response")

            if error_string not in res['message']:
                raise AssertionError((
                    "assert_response_error: Expected error string '{}', "
                    "not found in '{}' (Full response: {})").format(
                    error_string, res['message'], str(exception)))
