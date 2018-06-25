#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
#
# Test -alertnotify -walletnotify and -blocknotify
#
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class NotifyTest(BitcoinTestFramework):

    alert_filename = None  # Set by setup_network

    def setup_network(self):
        self.nodes = []
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w') as f:
            pass  # Just open then close to create zero-length file
        self.nodes.append(start_node(0, self.options.tmpdir,
                            ["-blockversion=2", "-alertnotify=echo %s >> \"" + self.alert_filename + "\""]))
        # Node1 mines block.version=211 blocks
        self.nodes.append(start_node(1, self.options.tmpdir,
                                ["-blockversion=211"]))
        connect_nodes(self.nodes[1], 0)

        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        ########################################
        # Run blocknotify and alertnotify tests.
        ########################################
        # Mine 51 up-version blocks
        self.nodes[1].generate(51)
        self.sync_all()
        # -alertnotify should trigger on the 51'st,
        # but mine and sync another to give
        # -alertnotify time to write
        self.nodes[1].generate(1)
        self.sync_all()

        with open(self.alert_filename, 'r') as f:
            alert_text = f.read()

        if len(alert_text) == 0:
            raise AssertionError("-alertnotify did not warn of up-version blocks")

        # Mine more up-version blocks, should not get more alerts:
        self.nodes[1].generate(1)
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()

        with open(self.alert_filename, 'r') as f:
            alert_text2 = f.read()

        if alert_text != alert_text2:
            raise AssertionError("-alertnotify excessive warning of up-version blocks")


        stop_nodes(self.nodes)
        wait_bitcoinds()

        ####################################################################
        # Run blocknotify and walletnotify tests.
        # Create new files when a block is received or wallet event happens.
        ####################################################################
        self.touch_filename1 = os.path.join(self.options.tmpdir, "newfile1")
        self.touch_filename2 = os.path.join(self.options.tmpdir, "newfile2")
        self.touch_filename3 = os.path.join(self.options.tmpdir, "newfile3")
        self.touch_filename4 = os.path.join(self.options.tmpdir, "newfile4")
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, ["-blocknotify=touch " + self.touch_filename1, "-walletnotify=touch " + self.touch_filename2]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-blocknotify=touch " + self.touch_filename3, "-walletnotify=touch " + self.touch_filename4]))
        connect_nodes(self.nodes[1], 0)
        self.is_network_split = False

        # check files not created after startup
        if os.path.isfile(self.touch_filename1):
            raise AssertionError(self.touch_filename1 + "exists")
        if os.path.isfile(self.touch_filename2):
            raise AssertionError(self.touch_filename2 + "exists")
        if os.path.isfile(self.touch_filename3):
            raise AssertionError(self.touch_filename3 + "exists")
        if os.path.isfile(self.touch_filename4):
            raise AssertionError(self.touch_filename4 + "exists")

        # mine a block. Both nodes should have created a file: newfile1 and newfile3.
        self.nodes[1].generate(1)
        self.sync_all()

        # check blocknotify - both nodes should have run the blocknotify command.
        if not os.path.isfile(self.touch_filename1):
            raise AssertionError(self.touch_filename1 + "does not exist")
        if not os.path.isfile(self.touch_filename3):
            raise AssertionError(self.touch_filename3 + "does not exist")

        # check walletnotify - send a transaction from node1 to node0. Only node1 should have run
        # the walletnotify command.
        self.nodes[1].generate(100)
        self.sync_all()
        address = self.nodes[0].getnewaddress("test")
        txid = self.nodes[1].sendtoaddress(address, 1, "", "", True)
        if os.path.isfile(self.touch_filename2):
            raise AssertionError(self.touch_filename2 + "exists")
        if not os.path.isfile(self.touch_filename4):
            raise AssertionError(self.touch_filename4 + "does not exist")

if __name__ == '__main__':
    NotifyTest().main()
