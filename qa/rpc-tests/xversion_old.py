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

class XVersionTest(BitcoinTestFramework):
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
        logging.info("Banlist file name: " + str(banlist_fn))
        try:
            os.remove(banlist_fn)
            logging.info("Removed old banlist %s.")
        except:
                pass
        stop_nodes(self.nodes)
        wait_bitcoinds()
        logging.info("Initializing test directory " + str(self.options.tmpdir))
        initialize_chain_clean(self.options.tmpdir, 1)
        self.nodes = [ start_node(0, self.options.tmpdir, ["-debug=net", "-use-xversion=0"]) ]
        self.pynode = pynode = BasicBUCashNode()

        pynode.connect(0, '127.0.0.1', p2p_port(0), self.nodes[0],
                       protohandler = VersionlessProtoHandler(),
                       send_initial_version = send_initial_version)
        return pynode.cnxns[0]

    def network_and_finish(self):
        nt = NetworkThread()
        nt.start()
        nt.join()

    def run_test(self):
        logging.info("Testing xversion handling")

        ex1_xver = { 1 : b"2", 3 : b"4"}

        def test_too_early(msg):
            """ Test that the given message if it comes right after start up will
            lead to rejection / banning as it comes too early. """
            logging.info("Testing that an an early %s fails." % msg)
            conn = self.restart_node(send_initial_version = False)
            conn.send_message(msg, pushbuf = True)
            self.network_and_finish()
            assert conn.disconnected

        # test failure due to early receipt
        if 0:
            for msg in [msg_xversion_old(), msg_xverack_old(),
                        msg_xversion_old({1:b"2",3:b"45"})]:
                test_too_early(msg)

        # test regular set up including xversion
        conn = self.restart_node()
        nt = NetworkThread()
        nt.start()

        conn.wait_for_verack()
        conn.send_message(msg_verack())

        # now it is time for xversion
        conn.wait_for(lambda : conn.remote_xversion)
        conn.send_message(msg_xversion_old({1000 : b"test string"}))

        conn.wait_for_xverack_old()
        conn.send_message(msg_xverack_old())

        # make sure xversion has actually been received properly

        # test that it contains the BU_LISTEN_PORT (replacement for buversion message)
        # FIXME: use proper constant
        assert 1<<17 in conn.remote_xversion.xver.keys()

        # Likewise, check that the remote end got our message
        node = self.nodes[0]

        peer_info = node.getpeerinfo()
        assert len(peer_info) == 1
        assert "xversion_map" in peer_info[0]
        xv_map = peer_info[0]["xversion_map"]

        assert len(xv_map) == 1
        assert unhexlify(list(xv_map.values())[0]) == b"test string"

        # send xupdate to test what would happen if someone tries to update non-chaneable key
        conn.send_message(msg_xupdate({1000 : b"test string changed"}))
        # some arbitrary sleep time
        time.sleep(3);

        # nothing should have changed, 1000 is not listed as a changeable key
        node = self.nodes[0]
        peer_info = node.getpeerinfo()
        assert len(peer_info) == 1
        assert "xversion_map" in peer_info[0]
        xv_map = peer_info[0]["xversion_map"]
        assert len(xv_map) == 1
        assert unhexlify(list(xv_map.values())[0]) == b"test string"

        # send xupdate to test what would happen if someone tries to update a non-existant key
        conn.send_message(msg_xupdate({1111 : b"bad string"}))
        # some arbitrary sleep time
        time.sleep(3);
        # nothing should have changed, 1111 is not listed as a known key
        node = self.nodes[0]
        peer_info = node.getpeerinfo()
        assert len(peer_info) == 1
        assert "xversion_map" in peer_info[0]
        xv_map = peer_info[0]["xversion_map"]
        assert len(xv_map) == 1

        # TODO appent to this test to test a changeable key once one has been implemented in the node

        conn.connection.disconnect_node()
        nt.join()

if __name__ == '__main__':
    xvt = XVersionTest()
    xvt.main()
