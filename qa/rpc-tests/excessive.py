#!/usr/bin/env python2
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2016 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Exercise the getchaintips API.  We introduce a network split, work
# on chains of different lengths, and join the network together again.
# This gives us two tips, verify that it works.
import time
import random
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.util import *
import pdb

class ExcessiveBlockTest (BitcoinTestFramework):

    def run_test (self):
        BitcoinTestFramework.run_test (self)
        tips = self.nodes[0].getchaintips ()
        assert_equal (len (tips), 1)
        assert_equal (tips[0]['branchlen'], 0)
        assert_equal (tips[0]['height'], 200)
        assert_equal (tips[0]['status'], 'active')
        # get spendable coins
        if 0:
          for n in self.nodes:
            n.generate(1)
            self.sync_all()
          self.nodes[0].generate(100)
          self.sync_all()
        
 	# Set the accept depth at 1, 2, and 3 and watch each nodes resist the chain for that long
        self.nodes[1].setminingmaxblock(1000)
        self.nodes[2].setminingmaxblock(1000)
        self.nodes[3].setminingmaxblock(1000)

        self.nodes[1].setexcessiveblock(1000, 1)
        self.nodes[2].setexcessiveblock(1000, 2)
        self.nodes[3].setexcessiveblock(1000, 3)

        addr = self.nodes[3].getnewaddress()
        for i in range(0,20):
          self.nodes[0].sendtoaddress(addr, 1.0)
        self.nodes[0].generate(1)
        time.sleep(2) #give blocks a chance to fully propagate
        counts = [ x.getblockcount() for x in self.nodes ]
        assert_equal(counts, [201,200,200,200])

        self.nodes[0].generate(1)
        time.sleep(2) #give blocks a chance to fully propagate
        sync_blocks(self.nodes[0:2])
        counts = [ x.getblockcount() for x in self.nodes ]
        assert_equal(counts, [202,202,200,200])

        self.nodes[0].generate(1)
        time.sleep(2) #give blocks a chance to fully propagate
        sync_blocks(self.nodes[0:3])
        counts = [ x.getblockcount() for x in self.nodes ]
        assert_equal(counts, [203,203,203,200])

        self.nodes[0].generate(1)
        time.sleep(2) #give blocks a chance to fully propagate
        self.sync_all()
        counts = [ x.getblockcount() for x in self.nodes ]
        assert_equal(counts, [204,204,204,204])

        # Mine 4 excessive blocks back-to-back.
        for i in range(0,20):
          self.nodes[0].sendtoaddress(addr, 1.0)
        self.nodes[0].generate(1)
        for i in range(0,20):
          self.nodes[0].sendtoaddress(addr, 1.0)
        self.nodes[0].generate(1)
        for i in range(0,20):
          self.nodes[0].sendtoaddress(addr, 1.0)
        self.nodes[0].generate(1)
        for i in range(0,20):
          self.nodes[0].sendtoaddress(addr, 1.0)
        self.nodes[0].generate(1)
        time.sleep(2) #give blocks a chance to fully propagate
        counts = [ x.getblockcount() for x in self.nodes ]
        assert_equal(counts, [208,204,204,204])

        # Mine empty blocks and watch nodes begin to accept the chain
        self.nodes[0].generate(1)
        time.sleep(2) #give blocks a chance to fully propagate
        sync_blocks(self.nodes[0:2])
        counts = [ x.getblockcount() for x in self.nodes ]
        assert_equal(counts, [209,209,204,204])

        self.nodes[0].generate(1)
        time.sleep(2) #give blocks a chance to fully propagate
        sync_blocks(self.nodes[0:3])
        counts = [ x.getblockcount() for x in self.nodes ]
        assert_equal(counts, [210,210,210,204])

        # Another EB. Node 3 is still on block 204.
        for i in range(0,20):
          self.nodes[0].sendtoaddress(addr, 1.0)
        self.nodes[0].generate(1)
        time.sleep(2) #give blocks a chance to fully propagate
        counts = [ x.getblockcount() for x in self.nodes ]
        assert_equal(counts, [211,210,210,204])

        self.nodes[0].generate(4)  # Reset AD windows
        self.sync_all()

        self.nodes[1].setminingmaxblock(100000)  # not sure how big the txns will be but smaller than this 
        self.nodes[1].setexcessiveblock(100000, 1)  # not sure how big the txns will be but smaller than this 
        for i in range(0,40):
          self.nodes[0].sendtoaddress(addr, 1.0)
        self.sync_all()
        self.nodes[0].generate(1)
        time.sleep(2) #give blocks a chance to fully propagate
        sync_blocks(self.nodes[0:2])
        counts = [ x.getblockcount() for x in self.nodes ]
        assert_equal(counts, [216,216,215,215])

        print "Random test"
        # random seed is initialized and printed by test framework
        for i in range(0,2):
          print "round ", i,
          self.nodes[0].generate(11)  # Reset AD windows
          self.sync_all()
          for n in self.nodes:
            size = random.randint(1,1000)*1000
            n.setminingmaxblock(size)
            n.setexcessiveblock(size, random.randint(0,10))
          addrs = [x.getnewaddress() for x in self.nodes]
          ntxs=0
          for i in range(0,random.randint(1,1000)):
            try:
              self.nodes[random.randint(0,3)].sendtoaddress(addrs[random.randint(0,3)], .1)
              ntxs += 1
            except JSONRPCException: # could be spent all the txouts
              pass
          print ntxs, " transactions"
          time.sleep(1)
          self.nodes[random.randint(0,3)].generate(1)
          time.sleep(1)


if __name__ == '__main__':
    ExcessiveBlockTest ().main ()
