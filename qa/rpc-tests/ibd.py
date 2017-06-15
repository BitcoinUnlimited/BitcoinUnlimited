#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2016 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class IBDTest (BitcoinTestFramework):
    def __init__(self):
      self.rep = False
      BitcoinTestFramework.__init__(self)

    def setup_chain(self):
        print ("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug="]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug="]))
        self.nodes.append(start_node(2, self.options.tmpdir, ["-debug=", "-prune=1000"]))
        interconnect_nodes(self.nodes)
        self.is_network_split=False
        self.sync_all()

                    
    def run_test (self):
        
        # Mine a 500 blocks chain.
        print ("Mining blocks...")
        self.nodes[0].generate(500)

        # Stop nodes
        stop_nodes(self.nodes)
        wait_bitcoinds()

        ######################################################################
        # Verify that pruned and non-pruned nodes can sync from a regular node
        ######################################################################

        # Start the first node and mine 10 blocks
        print ("Mining 10 more blocks...")
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug="]))
        self.nodes[0].generate(10)

        # Connect the first NETWORK_NODE - all nodes should sync
        print ("Connect NETWORK_NODE...")
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug="]))
        connect_nodes(self.nodes[1],0)
        self.sync_all()

        # Connect the second node as non pruned  node (not a network node)- all nodes should sync
        print ("Connect Pruned node...")
        self.nodes.append(start_node(2, self.options.tmpdir, ["-debug=", "-prune=1000"]))
        connect_nodes(self.nodes[1],2)
        self.sync_all()
        
        #stop nodes
        stop_nodes(self.nodes)
        wait_bitcoinds()


        ######################################################################
        # Verify that NETWORK_NODE will not sync from pruned nodes
        ######################################################################

        # Mine blocks on node 0 and sync to the pruned node 2
        print ("Mining 10 more blocks...")
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug="]))
        self.nodes.append(start_node(2, self.options.tmpdir, ["-debug=", "-prune=1000"]))
        connect_nodes(self.nodes[0],2)
        self.nodes[0].generate(10)

        # Connect node1 to pruned node 2 only.  They should not sync.
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug="]))
        connect_nodes(self.nodes[1],2)
        time.sleep(5);
        counts = [ x.getblockcount() for x in self.nodes ]
        assert_equal(counts, [520, 520, 510])  

        # Now connect the two network nodes 0 and 1 together and they should sync
        print ("Connecting network nodes...")
        connect_nodes(self.nodes[0],1)
        self.sync_all()
         
        #stop nodes
        stop_nodes(self.nodes)
        wait_bitcoinds()



if __name__ == '__main__':
    IBDTest ().main ()

    
