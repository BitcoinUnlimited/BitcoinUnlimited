#!/usr/bin/env python3
# Copyright (c) 2016 The Bitcoin Core developers
# Copyright (c) 2016-2017 The Bitcoin Unlimited developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import time
from test_framework.blocktools import create_block, create_coinbase

'''
Test version bits' warning system.

Generate chains with block versions that appear to be signalling unknown
soft-forks, and test that warning alerts are generated.
'''

VB_PERIOD = 144 # versionbits period length for regtest
VB_THRESHOLD = 108 # versionbits activation threshold for regtest
VB_TOP_BITS = 0x20000000
VB_UNKNOWN_BIT = 27 # Choose a bit unassigned to any deployment

# TestNode: bare-bones "peer".  Used mostly as a conduit for a test to sending
# p2p messages to a node, generating the messages in the main testing logic.
class TestNode(NodeConnCB):
    def __init__(self):
        NodeConnCB.__init__(self)
        self.connection = None
        self.ping_counter = 1
        self.last_pong = msg_pong()

    def add_connection(self, conn):
        self.connection = conn

    def on_inv(self, conn, message):
        pass

    # Wrapper for the NodeConn's send_message function
    def send_message(self, message):
        self.connection.send_message(message)

    def on_pong(self, conn, message):
        self.last_pong = message

    # Sync up with the node after delivery of a block
    def sync_with_ping(self, timeout=30):
        self.connection.send_message(msg_ping(nonce=self.ping_counter))
        received_pong = False
        sleep_time = 0.05
        while not received_pong and timeout > 0:
            time.sleep(sleep_time)
            timeout -= sleep_time
            with mininode_lock:
                if self.last_pong.nonce == self.ping_counter:
                    received_pong = True
        self.ping_counter += 1
        return received_pong


class VersionBitsWarningTest(BitcoinTestFramework):
    def setup_chain(self):
        initialize_chain_clean(self.options.tmpdir, 1)

    def setup_network(self):
        self.nodes = []
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        # Open and close to create zero-length file
        with open(self.alert_filename, 'w') as f:
            pass
        self.node_options = ["-debug", "-logtimemicros=1", "-whitelist=127.0.0.1", "-alertnotify=echo %s >> \"" + self.alert_filename + "\""]
        self.nodes.append(start_node(0, self.options.tmpdir, self.node_options))

        import re
        self.vb_pattern = re.compile("^Warning.*versionbit")

    # Send numblocks blocks via peer with nVersionToUse set.
    def send_blocks_with_version(self, peer, numblocks, nVersionToUse):
        tip = self.nodes[0].getbestblockhash()
        height = self.nodes[0].getblockcount()
        block_time = self.nodes[0].getblockheader(tip)["time"]+1
        tip = int(tip, 16)

        for i in range(numblocks):
            block = create_block(tip, create_coinbase(height+1), block_time)
            block.nVersion = nVersionToUse
            block.solve()
            peer.send_message(msg_block(block))
            block_time += 1
            height += 1
            tip = block.sha256
        peer.sync_with_ping()

    def test_versionbits_in_alert_file(self):
        with open(self.alert_filename, 'r') as f:
            alert_text = f.read()
        assert(self.vb_pattern.match(alert_text))

    def run_test(self):
        # Setup the p2p connection and start up the network thread.
        test_node = TestNode()

        connections = []
        connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node))
        test_node.add_connection(connections[0])

        NetworkThread().start() # Start up network handling in another thread

        # Test logic begins here
        test_node.wait_for_verack()

        # 1. Have the node mine one period worth of blocks
        self.nodes[0].generate(VB_PERIOD)
        assert(self.nodes[0].getblockcount() ==  144)

        # 2. Now build one period of blocks on the tip, with < VB_THRESHOLD
        # blocks signaling some unknown bit.
        nVersion = VB_TOP_BITS | (1<<VB_UNKNOWN_BIT)
        for i in range(VB_THRESHOLD-1):
            self.send_blocks_with_version(test_node, 1, nVersion)
            test_node.sync_with_ping()
        assert(self.nodes[0].getblockcount() ==  251)

        # Fill rest of period with regular version blocks
        self.nodes[0].generate(VB_PERIOD - VB_THRESHOLD + 1)
        # Check that we're not getting any versionbit-related errors in
        # getinfo()
        assert(not self.vb_pattern.match(self.nodes[0].getinfo()["errors"]))
        assert(self.nodes[0].getblockcount() ==  288)
 
        # 3. Now build one period of blocks with >= VB_THRESHOLD blocks signaling
        # some unknown bit
        for i in range(VB_THRESHOLD):
            self.send_blocks_with_version(test_node, 1, nVersion)
            test_node.sync_with_ping()
           # time.sleep(0.05)
        assert(self.nodes[0].getblockcount() ==  396)
        self.nodes[0].generate(VB_PERIOD - VB_THRESHOLD)
        # Might not get a versionbits-related alert yet, as we should
        # have gotten a different alert due to more than 51/100 blocks
        # being of unexpected version.
        # Check that getinfo() shows some kind of error.
        assert(len(self.nodes[0].getinfo()["errors"]) != 0)
        assert(self.nodes[0].getblockcount() ==  432)

        # Mine a period worth of expected blocks so the generic block-version warning
        # is cleared, and restart the node. This should move the versionbit state
        # to ACTIVE.
        self.nodes[0].generate(VB_PERIOD)
        stop_node(self.nodes[0], 0)
        wait_bitcoinds()
        # Empty out the alert file
        with open(self.alert_filename, 'w') as f:
            pass
        self.nodes[0] = start_node(0, self.options.tmpdir, ["-debug", "-logtimemicros=1", "-alertnotify=echo %s >> \"" + self.alert_filename + "\""])

        # Connecting one block should be enough to generate an error.
        self.nodes[0].generate(1)
        assert(len(self.nodes[0].getinfo()["errors"]) != 0)
        assert(self.nodes[0].getblockcount() ==  577)
        stop_node(self.nodes[0], 0)
        wait_bitcoinds()
        self.test_versionbits_in_alert_file()

        # Test framework expects the node to still be running...
        self.nodes[0] = start_node(0, self.options.tmpdir, ["-debug", "-logtimemicros=1", "-alertnotify=echo %s >> \"" + self.alert_filename + "\""])


if __name__ == '__main__':
    VersionBitsWarningTest().main()
