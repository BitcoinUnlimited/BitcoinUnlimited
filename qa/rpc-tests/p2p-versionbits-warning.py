#!/usr/bin/env python3
# Copyright (c) 2016 The Bitcoin Core developers
# Copyright (c) 2016-2017 The Bitcoin Unlimited developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import test_framework.loginit
from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import re
import time
from test_framework.blocktools import create_block, create_coinbase

"""Test version bits warning system.

Generate chains with block versions that appear to be signalling unknown
forks, and test that warning alerts are generated.
"""

# bip135 begin
# modified from 108/144 to 51/100 for new unknown versions algo
VB_PERIOD = 100 # unknown versionbits period length
VB_THRESHOLD = 51 # unknown versionbits warning level
WARN_UNKNOWN_RULES_MINED = "Unknown block versions being mined! It's possible unknown rules are in effect"
# the warning echo'd by alertnotify is sanitized
WARN_UNKNOWN_RULES_MINED_SANITIZED = re.compile("^Warning: Unknown block versions being mined Its possible unknown rules are in effect")
# After BIP135, a client cannot know whether an unknown version bit has gone ACTIVE
# since the activation threshold of unknown bits is ... unknown.
VB_PATTERN = re.compile("^Warning.*versionbit")
# bip135 end
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
        with open(self.alert_filename, 'w', encoding='utf8') as _:
            pass
        self.node_options = ["-debug", "-logtimemicros=1", "-whitelist=127.0.0.1",
                             "-alertnotify=echo %s >> \"" + self.alert_filename + "\""]
        self.nodes.append(start_node(0, self.options.tmpdir, self.node_options))

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
        with open(self.alert_filename, 'r', encoding='utf8') as f:
            alert_text = f.read()
        assert(WARN_UNKNOWN_RULES_MINED_SANITIZED.match(alert_text))

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
        assert(self.nodes[0].getblockcount() ==  VB_PERIOD)

        # 2. Now build one period of blocks on the tip, with < VB_THRESHOLD
        # blocks signaling some unknown bit.
        nVersion = VB_TOP_BITS | (1<<VB_UNKNOWN_BIT)
        for i in range(VB_THRESHOLD - 1):
            self.send_blocks_with_version(test_node, 1, nVersion)
            test_node.sync_with_ping()
        assert(self.nodes[0].getblockcount() ==  VB_PERIOD + VB_THRESHOLD - 1)

        # Fill rest of period with regular version blocks
        self.nodes[0].generate(VB_PERIOD - VB_THRESHOLD + 1)
        # Check that we're not getting any versionbit-related errors in
        # get*info()
        assert(not VB_PATTERN.match(self.nodes[0].getinfo()["errors"]))
        assert(not VB_PATTERN.match(self.nodes[0].getmininginfo()["errors"]))
        assert(not VB_PATTERN.match(self.nodes[0].getnetworkinfo()["warnings"]))
        assert(self.nodes[0].getblockcount() ==  VB_PERIOD * 2)

        # 3. Now build one period of blocks with > VB_THRESHOLD blocks signaling
        # some unknown bit
        for i in range(VB_THRESHOLD):
            self.send_blocks_with_version(test_node, 1, nVersion)
            test_node.sync_with_ping()
        assert(self.nodes[0].getblockcount() ==  VB_PERIOD * 2 + VB_THRESHOLD)
        self.nodes[0].generate(VB_PERIOD - VB_THRESHOLD)
        # Might not get a versionbits-related alert yet, as we should
        # have gotten a different alert due to more than 50/100 blocks
        # being of unexpected version.
        # Check that get*info() shows some kind of error.
        assert(WARN_UNKNOWN_RULES_MINED in self.nodes[0].getinfo()["errors"])
        assert(WARN_UNKNOWN_RULES_MINED in self.nodes[0].getmininginfo()["errors"])
        assert(WARN_UNKNOWN_RULES_MINED in self.nodes[0].getnetworkinfo()["warnings"])
        assert(len(self.nodes[0].getinfo()["errors"]) != 0)
        self.test_versionbits_in_alert_file()

        assert(self.nodes[0].getblockcount() ==  VB_PERIOD * 3)

        # Mine a period worth of expected blocks so the generic block-version warning
        # is cleared, and restart the node.
        # OBSOLETE: This should no longer move the versionbit state to ACTIVE.
        # State transitions are NOT tracked for unconfigured bits in bip135,
        # since we do not have sufficient information to assess those reliably.
        self.nodes[0].generate(VB_PERIOD)
        stop_node(self.nodes[0], 0)
        wait_bitcoinds()
        # Empty out the alert file
        with open(self.alert_filename, 'w', encoding='utf8') as f:
            pass
        self.nodes[0] = start_node(0, self.options.tmpdir, ["-debug", "-logtimemicros=1",
                                   "-alertnotify=echo %s >> \"" + self.alert_filename + "\""])

        # Since there are no unknown versionbits exceeding threshold in last period,
        # no error will be generated.
        self.nodes[0].generate(1)
        assert(len(self.nodes[0].getinfo()["errors"]) == 0)
        assert(self.nodes[0].getblockcount() ==  VB_PERIOD * 4 + 1)
        stop_node(self.nodes[0], 0)
        wait_bitcoinds()

        # Test framework expects the node to still be running...
        self.nodes[0] = start_node(0, self.options.tmpdir, ["-debug", "-logtimemicros=1",
                                   "-alertnotify=echo %s >> \"" + self.alert_filename + "\""])


if __name__ == '__main__':
    VersionBitsWarningTest().main()
