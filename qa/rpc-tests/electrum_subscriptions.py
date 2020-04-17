#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Unlimited developers

import asyncio
import time
from test_framework.util import assert_equal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging
from test_framework.electrumutil import (ElectrumConnection,
    address_to_scripthash, bitcoind_electrum_args, sync_electrum_height)

class ElectrumSubscriptionsTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [bitcoind_electrum_args()]

    def run_test(self):
        n = self.nodes[0]
        n.generate(1)
        sync_electrum_height(n)

        cli = ElectrumConnection()
        self.test_subscribe_headers(n, cli)
        self.test_subscribe_scripthash(n, cli)

    def test_subscribe_scripthash(self, n, cli):
        logging.info("Testing scripthash subscription")
        n.generate(100)
        addr = n.getnewaddress()
        scripthash = address_to_scripthash(addr)
        statushash, queue = cli.subscribe('blockchain.scripthash.subscribe', scripthash)

        logging.info("Unused address should not have a statushash")
        assert_equal(None, statushash)

        logging.info("Check notification on receiving coins")
        n.sendtoaddress(addr, 10)
        sh, new_statushash1 = cli.loop.run_until_complete(
                asyncio.wait_for(queue.get(), timeout = 10))
        assert_equal(scripthash, sh)
        assert(new_statushash1 != None and len(new_statushash1) == 64)

        logging.info("Check notification on block confirmation")
        assert(len(n.getrawmempool()) == 1)
        n.generate(1)
        assert(len(n.getrawmempool()) == 0)
        sh, new_statushash2 = cli.loop.run_until_complete(
                asyncio.wait_for(queue.get(), timeout = 10))
        assert_equal(scripthash, sh)
        assert(new_statushash2 != new_statushash1)
        assert(new_statushash2 != None)

        logging.info("Check that we get notification when spending funds from address")
        n.sendtoaddress(n.getnewaddress(), n.getbalance(), "", "", True)
        sh, new_statushash3 = cli.loop.run_until_complete(
                asyncio.wait_for(queue.get(), timeout = 10))
        assert_equal(scripthash, sh)
        assert(new_statushash3 != new_statushash2)
        assert(new_statushash3 != None)

    def test_subscribe_headers(self, n, cli):
        headers = []

        logging.info("Calling subscribe should return the current best block header")
        result, queue = cli.subscribe('blockchain.headers.subscribe')
        assert_equal(
                n.getblockheader(n.getbestblockhash(), False),
                result['hex'])

        logging.info("Now generate 10 blocks, check that these are pushed to us.")
        async def test():
            for _ in range(10):
                blockhashes = n.generate(1)
                header_hex = n.getblockheader(blockhashes.pop(), False)
                notified = await asyncio.wait_for(queue.get(), timeout = 10)
                assert_equal(header_hex, notified.pop()['hex'])

        start = time.time()
        cli.loop.run_until_complete(test())
        logging.info("Getting 10 block notifications took {} seconds".format(time.time() - start))

if __name__ == '__main__':
    ElectrumSubscriptionsTest().main()
