#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
#
# Test node handling
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

import http.client
import urllib.parse

class NodeHandlingTest (BitcoinTestFramework):
    def run_test(self):
        ###########################
        # setban/listbanned tests #
        ###########################
        assert_equal(len(self.nodes[2].getpeerinfo()), 4) #we should have 4 nodes at this point
        self.nodes[2].setban("127.0.0.1", "add")
        time.sleep(3) #wait till the nodes are disconected
        assert_equal(len(self.nodes[2].getpeerinfo()), 0) #all nodes must be disconnected at this point
        assert_equal(len(self.nodes[2].listbanned()), 1)
        self.nodes[2].clearbanned()
        assert_equal(len(self.nodes[2].listbanned()), 0)
        self.nodes[2].setban("127.0.0.0/24", "add")
        assert_equal(len(self.nodes[2].listbanned()), 1)
        try:
            self.nodes[2].setban("127.0.0.1", "add") #throws exception because 127.0.0.1 is within range 127.0.0.0/24
        except:
            pass
        assert_equal(len(self.nodes[2].listbanned()), 1) #still only one banned ip because 127.0.0.1 is within the range of 127.0.0.0/24
        try:
            self.nodes[2].setban("127.0.0.1", "remove")
        except:
            pass
        assert_equal(len(self.nodes[2].listbanned()), 1)
        self.nodes[2].setban("127.0.0.0/24", "remove")
        assert_equal(len(self.nodes[2].listbanned()), 0)
        self.nodes[2].clearbanned()
        assert_equal(len(self.nodes[2].listbanned()), 0)

        ##test persisted banlist
        self.nodes[2].setban("127.0.0.0/32", "add")
        self.nodes[2].setban("127.0.0.0/24", "add")
        self.nodes[2].setban("192.168.0.1", "add", 1) #ban for 1 seconds
        self.nodes[2].setban("2001:4d48:ac57:400:cacf:e9ff:fe1d:9c63/19", "add", 1000) #ban for 1000 seconds
        listBeforeShutdown = self.nodes[2].listbanned()
        assert_equal("192.168.0.1/32", listBeforeShutdown[2]['address']) #must be here
        time.sleep(2) #make 100% sure we expired 192.168.0.1 node time

        #stop node
        stop_node(self.nodes[2], 2)

        self.nodes[2] = start_node(2, self.options.tmpdir)
        listAfterShutdown = self.nodes[2].listbanned()
        assert_equal("127.0.0.0/24", listAfterShutdown[0]['address'])
        assert_equal("127.0.0.0/32", listAfterShutdown[1]['address'])
        assert_equal("/19" in listAfterShutdown[2]['address'], True)

        ###########################
        # RPC disconnectnode test #
        ###########################
        url = urllib.parse.urlparse(self.nodes[1].url)
        self.nodes[0].disconnectnode(url.hostname+":"+str(p2p_port(1)))
        time.sleep(2) #disconnecting a node needs a little bit of time
        for node in self.nodes[0].getpeerinfo():
            assert(node['addr'] != url.hostname+":"+str(p2p_port(1)))
        connect_nodes_bi(self.nodes,0,1) #reconnect the node
        found = False
        for node in self.nodes[0].getpeerinfo():
            if node['addr'] == url.hostname+":"+str(p2p_port(1)):
                found = True
        assert(found)


        #############################
        # Test thintype peer tracking
        #############################
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.node_args = [['-debug=net'], ['-debug=net'], ['-debug=net'], ['-debug=net']]
        self.nodes = start_nodes(3, self.options.tmpdir, self.node_args)
        for node in self.nodes:
            node.clearbanned();
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[0], 2)
        connect_nodes(self.nodes[1], 2)
        # Each node should have two each of xthin/graph/cmpct peers connected
        for node in self.nodes:
            waitFor(10, lambda: node.getinfo()["peers_graphene"] == 2)
            waitFor(10, lambda: node.getinfo()["peers_xthinblock"] == 2)
            waitFor(10, lambda: node.getinfo()["peers_cmpctblock"] == 2)
        
        disconnect_nodes(self.nodes[0], 1)
        # node0 and node1 should now only have 1 each of xthin/graph/cmpct peers but node2 should have 2 of each.
        waitFor(10, lambda: self.nodes[0].getinfo()["peers_graphene"] == 1)
        waitFor(10, lambda: self.nodes[0].getinfo()["peers_xthinblock"] == 1)
        waitFor(10, lambda: self.nodes[0].getinfo()["peers_cmpctblock"] == 1)
        waitFor(10, lambda: self.nodes[1].getinfo()["peers_graphene"] == 1)
        waitFor(10, lambda: self.nodes[1].getinfo()["peers_xthinblock"] == 1)
        waitFor(10, lambda: self.nodes[1].getinfo()["peers_cmpctblock"] == 1)
        waitFor(10, lambda: self.nodes[2].getinfo()["peers_graphene"] == 2)
        waitFor(10, lambda: self.nodes[2].getinfo()["peers_xthinblock"] == 2)
        waitFor(10, lambda: self.nodes[2].getinfo()["peers_cmpctblock"] == 2)

        #############################
        # Test peer eviction
        #############################
        stop_nodes(self.nodes)
        wait_bitcoinds()
        # Node 3 is the one we will use to evict a connection. It is set to maxconnection=3 however
        # it will only allow 2 inbound connections because "1" outbound feeler connection is assumed
        # making the total connections possible equal to three.
        self.node_args = [['-debug=net', '-maxconnections=1', '-maxoutconnections=1'],
                          ['-debug=net', '-maxconnections=1', '-maxoutconnections=1'],
                          ['-debug=net', '-maxconnections=1', '-maxoutconnections=1'],
                          ['-debug=net', '-maxconnections=3', '-maxoutconnections=0', '-debug=evict']]
        self.nodes = start_nodes(4, self.options.tmpdir, self.node_args)
        for node in self.nodes:
            node.clearbanned();
        connect_nodes(self.nodes[0], 3)
        waitFor(10, lambda: self.nodes[0].getinfo()["connections"] == 1)
        waitFor(10, lambda: self.nodes[3].getinfo()["connections"] == 1)
        connect_nodes(self.nodes[1], 3)
        waitFor(10, lambda: self.nodes[0].getinfo()["connections"] == 1)
        waitFor(10, lambda: self.nodes[1].getinfo()["connections"] == 1)
        waitFor(10, lambda: self.nodes[3].getinfo()["connections"] == 2)

        # Connecting one more than the max which causes the a peer to be evicted
        connect_nodes(self.nodes[2], 3)
        # one of these 2 nodes should be evicted.  Which one depends on ping times and activity levels
        waitFor(10, lambda: self.nodes[0].getinfo()["connections"] + self.nodes[1].getinfo()["connections"] == 1)
        waitFor(10, lambda: self.nodes[2].getinfo()["connections"] == 1)
        waitFor(10, lambda: self.nodes[3].getinfo()["connections"] == 2)

        #############################
        # Test startup after invalidating part of the chain on one peer and extending the chain on another
        # : This simulates what can happen after a hardfork and a node operator failed to upgrade
        #   before the fork.
        #############################

        stop_nodes(self.nodes)
        wait_bitcoinds()

        self.node_args = [['-debug=net'],
                          ['-debug=net'],
                          ['-debug=net'],
                          ['-debug=net']]
        self.nodes = start_nodes(4, self.options.tmpdir, self.node_args)
        for node in self.nodes:
            node.clearbanned();
   
        # Before reconnecting the peers, extend the chain on one peer while
        # at the same time invalidating a few blocks on another
        self.nodes[0].generate(5);
        height = self.nodes[1].getblockcount()
        self.nodes[1].invalidateblock(self.nodes[1].getblockhash(height))
        self.nodes[1].invalidateblock(self.nodes[1].getblockhash(height-1))
        self.nodes[1].invalidateblock(self.nodes[1].getblockhash(height-2))

        stop_nodes(self.nodes)
        wait_bitcoinds()

        self.node_args = [['-debug=net'],
                          ['-debug=net'],
                          ['-debug=net'],
                          ['-debug=net']]
        self.nodes = start_nodes(4, self.options.tmpdir, self.node_args)

        interconnect_nodes(self.nodes);
        sync_blocks(self.nodes);


if __name__ == '__main__':
    NodeHandlingTest ().main ()


# Create a convenient function for an interactive python debugging session
def Test():
    t = NodeHandlingTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }
    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
