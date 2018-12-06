#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
import os
import os.path
import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging
from binascii import unhexlify
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import NetworkThread
from test_framework.nodemessages import *
from test_framework.bumessages import *
from test_framework.bunode import BasicBUCashNode,  VersionlessProtoHandler

class PingEarlyTest(BitcoinTestFramework):
    def __init__(self):
        self.nodes = []
        BitcoinTestFramework.__init__(self)

    def setup_chain(self):
        pass

    def setup_network(self, split=False):
        pass

    def restart_node(self, send_initial_version = True):
        # remove any potential banlist
        banlist_fn = os.path.join(
            node_regtest_dir(self.options.tmpdir, 0),
            "banlist.dat")
        print("Banlist file name:", banlist_fn)
        try:
            os.remove(banlist_fn)
            print("Removed old banlist %s.")
        except:
                pass
        stop_nodes(self.nodes)
        wait_bitcoinds()
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 1)
        self.nodes = [ start_node(0, self.options.tmpdir, ["-debug"]) ]
        self.pynode = pynode = BasicBUCashNode()

        pynode.connect(0, '127.0.0.1', p2p_port(0), self.nodes[0],
                       protohandler = VersionlessProtoHandler(),
                       send_initial_version = send_initial_version)
        return pynode.cnxns[0]

    def run_test(self):
        logging.info("Testing early ping replies")

        conn = self.restart_node(send_initial_version = False)
        conn.send_message(msg_ping(), pushbuf=True)
        nt = NetworkThread()
        nt.start()
        conn.wait_for(lambda : conn.pong_counter)
        conn.connection.disconnect_node()
        nt.join()

if __name__ == '__main__':
    xvt = PingEarlyTest()
    xvt.main()
