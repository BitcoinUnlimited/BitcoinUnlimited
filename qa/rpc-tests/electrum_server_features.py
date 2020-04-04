#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Unlimited developers
from test_framework.util import assert_equal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.loginit import logging
from test_framework.electrumutil import bitcoind_electrum_args, \
    create_electrum_connection

def versiontuple(v):
    v = tuple(map(int, (v.split("."))))
    if len(v) == 2:
        v = v + (0,)
    return v

class ElectrumBasicTests(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [bitcoind_electrum_args()]

    def run_test(self):
        n = self.nodes[0]

        # Bump out of IBD
        n.generate(1)

        electrum_client = create_electrum_connection()
        res = electrum_client.call("server.features")

        # Keys that the server MUST support
        assert_equal(n.getblockhash(0), res['genesis_hash'])
        assert_equal("sha256", res['hash_function'])
        assert(versiontuple(res['protocol_min']) >= versiontuple("1.4"))
        assert(versiontuple(res['protocol_max']) >= versiontuple("1.4"))
        assert(len(res['server_version']))

if __name__ == '__main__':
    ElectrumBasicTests().main()
