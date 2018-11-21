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
from test_framework.bunode import BasicBUCashNode, BUProtocolHandler

class NodeProtoHandler(BUProtocolHandler):
    def __init__(self):
        self.remote_xversion = None
        self.pong_received = 0
        BUProtocolHandler.__init__(self)

    def on_version(self, conn, message):
        self.show_debug_msg("version received\n")
        self.remoteVersion = message.nVersion

    def on_verack(self, conn, message):
        self.show_debug_msg("verack received\n")
        self.verack_received = True

    def on_xverack(self, conn, message):
        self.show_debug_msg("xverack received\n")
        self.xverack_received = True

    def on_xversion(self, conn, message):
        self.show_debug_msg("xversion received\n")
        self.remote_xversion = message

    def on_pong(self, conn, message):
        self.show_debug_msg("pong received\n")
        self.pong_received += 1


class MyTest(BitcoinTestFramework):
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

        self.hndlr = pynode.connect(0, '127.0.0.1', p2p_port(0), self.nodes[0],
                       protohandler =NodeProtoHandler(),
                       send_initial_version = send_initial_version)
        self.hndlr.allow0Checksum = True

        return pynode.cnxns[0]

    def network_and_finish(self):
        nt = NetworkThread()
        nt.start()
        nt.join()

    def run_test(self):
        logging.info("Testing xversion handling")

        # test regular set up including xversion
        conn = self.restart_node()
        conn.allow0Checksum = True
        nt = NetworkThread()
        nt.start()

        conn.wait_for_verack()
        conn.send_message(msg_verack())

        # now it is time for xversion
        conn.send_message(msg_xversion({0x00020002 : 1}))

        # send extra buversion and buverack, shouldn't harm
        conn.send_message(msg_buversion(addrFromPort = 12345))
        conn.send_message(msg_buverack())
        conn.wait_for(lambda : conn.remote_xversion)

        conn.send_message(msg_ping())
        conn.wait_for(lambda : conn.pong_received)
        # check that we are getting 0-value checksums from the BU node
        assert(self.hndlr.num0Checksums > 0)

        conn.connection.disconnect_node()
        nt.join()

if __name__ == '__main__':
    xvt = MyTest()
    xvt.main()

# Create a convenient function for an interactive python debugging session
def Test():
    t = MyTest()
    bitcoinConf = {
        "debug": ["blk", "mempool", "net", "req"],
        "blockprioritysize": 2000000,  # we don't want any transactions rejected due to insufficient fees...
        "net.ignoreTimeouts": 1,
        "logtimemicros": 1
    }

    # you may want these flags:
    flags = ["--nocleanup", "--noshutdown"]

    # Execution is much faster if a ramdisk is used, so use it if one exists in a typical location
    if os.path.isdir("/ramdisk/test"):
        flags.append("--tmpdir=/ramdisk/test/ma")

    # Out-of-source builds are awkward to start because they need an additional flag
    # automatically add this flag during testing for common out-of-source locations
    binpath = findBitcoind()
    flags.append("--srcdir=%s" % binpath)
    # start the test
    t.main(flags, bitcoinConf, None)
