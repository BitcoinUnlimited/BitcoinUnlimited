#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
#
# Test pruning code
# ********
# WARNING:
# This test uses 4GB of disk space.
# This test takes 30 mins or more (up to 2 hours)
# ********

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.blocktools import *
import time
import os


def calc_usage(blockdir):
    return sum(os.path.getsize(blockdir+f) for f in os.listdir(blockdir) if os.path.isfile(blockdir+f)) / (1024. * 1024.)

class PruneTest(BitcoinTestFramework):

    def __init__(self):
        self.utxo = []
        self.address = ["",""]
        self.txouts = gen_return_txouts()
        # index n lines up with useblockdb=<n>, so 0 is sequential, 1 is leveldb and so on
        self.prunedirs = []
        self.mainchainheights = []
        self.mainchainhashes = []

    def setup_chain(self, bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 5, bitcoinConfDict, wallets)

        # Cache for utxos, as the listunspent may take a long time later in the test
        self.utxo_cache_0 = []
        self.utxo_cache_1 = []

    def setup_network(self):
        self.nodes = []
        self.is_network_split = False

        # Create nodes 0 and 1 to mine
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug","-rpcservertimeout=0", "-maxreceivebuffer=20000","-blockmaxsize=999000", "-checkblocks=5"], timewait=900))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug","-rpcservertimeout=0", "-maxreceivebuffer=20000","-blockmaxsize=999000", "-checkblocks=5"], timewait=900))

        # Create node 2 to test pruning on sequential files
        self.nodes.append(start_node(2, self.options.tmpdir, ["-debug","-rpcservertimeout=0", "-maxreceivebuffer=20000","-prune=550", "-useblockdb=0"], timewait=900))
        self.prunedirs.append(self.options.tmpdir+"/node2/regtest/blocks/")
        # Create node 3 to test pruning on leveldb
        self.nodes.append(start_node(3, self.options.tmpdir, ["-debug","-rpcservertimeout=0", "-maxreceivebuffer=20000","-prune=550", "-useblockdb=1"], timewait=900))
        self.prunedirs.append(self.options.tmpdir+"/node3/regtest/blockdb/")


        self.address[0] = self.nodes[0].getaddressforms(self.nodes[0].getnewaddress())["legacy"]
        self.address[1] = self.nodes[1].getaddressforms(self.nodes[1].getnewaddress())["legacy"]

        # Determine default relay fee
        self.relayfee = self.nodes[0].getnetworkinfo()["relayfee"]

        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[1], 2)
        connect_nodes(self.nodes[2], 0)
        connect_nodes(self.nodes[0], 3)
        connect_nodes(self.nodes[1], 3)
        connect_nodes(self.nodes[2], 3)
        sync_blocks(self.nodes[0:4])

    def mine_large_block(node, utxos=None):
        # generate a 66k transaction,
        # and 14 of them is close to the 1MB block limit
        num = 14
        txouts = gen_return_txouts()
        utxos = utxos if utxos is not None else []
        if len(utxos) < num:
            utxos.clear()
            utxos.extend(node.listunspent())
        fee = 100 * node.getnetworkinfo()["relayfee"]
        create_lots_of_big_transactions(node, txouts, utxos, num, fee=fee)
        blkhash = node.generate(1)
        print("block: %s\n" % str(blkhash))

    def create_big_chain(self):
        # Start by creating some coinbases we can spend later
        self.nodes[1].generate(200)
        sync_blocks(self.nodes[0:2])
        self.nodes[0].generate(150)
        sync_blocks(self.nodes[0:2])
        # Then mine enough full blocks to create more than 550MiB of data
        addrs = [ self.nodes[0].getnewaddress() for x in range(0,20)]
        addrs = [ self.nodes[0].getaddressforms(x)["legacy"] for x in addrs]
        wallet = []
        for i in range(645):  # 645
            print("block %d" % i)
            generateBlock(self.nodes[0], 1024*1024, addrs, wallet)

        sync_blocks(self.nodes[0:4])

    def test_height_min(self, index):
        # we only check for block files in sequential mode (0)
        if index == 0:
            if not os.path.isfile(self.prunedirs[index]+"blk00000.dat"):
                raise AssertionError("blk00000.dat is missing, pruning too early")
        print("Success")
        print("Though we're already using more than 550MiB, current usage:", calc_usage(self.prunedirs[index]))
        print("Mining 25 more blocks should cause the first block file to be pruned")
        # Pruning doesn't run until we're allocating another chunk, 20 full blocks past the height cutoff will ensure this
        wallet = []
        for i in range(25):
            counts = [ x.getblockcount() for x in self.nodes ]
            print(counts)
            generateBlock(self.nodes[0], 1024*1024, [self.address[0]], wallet)

        # only check for block files in sequential mode (0)
        waitstart = time.time()
        if index == 0:
            while os.path.isfile(self.prunedirs[index]+"blk00000.dat"):
                time.sleep(0.1)
                if time.time() - waitstart > 10:
                    raise AssertionError("blk00000.dat not pruned when it should be")

        print("Success")
        usage = calc_usage(self.prunedirs[index])
        print("Usage should be below target:", usage)
        if (usage > 550):
            raise AssertionError("Pruning target not being met")

    def test_height_after_sync(self):
        self.nodes.append(start_node(4, self.options.tmpdir, ["-debug","-rpcservertimeout=0", "-maxreceivebuffer=20000","-blockmaxsize=999000", "-checkblocks=5"], timewait=900))
        self.prunedir = self.options.tmpdir+"/node4/regtest/blocks/"
        connect_nodes(self.nodes[4], 1)
        # wait for the first blocks to arrive on node3 before mining the next
        # blocks.  We have to make sure the first block file has a starting height
        # before doing any mining.
        while self.nodes[4].getblockcount() <= 0:
            time.sleep(0.1)

        # Mine several new blocks while the chain on node 3 is syncing.  This
        # should not allow new blocks to get into the block files until we
        # are within 144 blocks of the chain tip.  If new blocks do get into the
        # first block file then we won't be able to prune it and the test will fail.
        for i in range(20):
            print ("generate a block")
            self.nodes[1].generate(1)
            counts = [ x.getblockcount() for x in self.nodes ]
            print(counts)
            time.sleep(0.5)
        sync_blocks(self.nodes)

        #check that first block file was pruned.
        waitstart = time.time()
        while os.path.isfile(self.prunedir+"blk00000.dat"):
            time.sleep(0.1)
            if time.time() - waitstart > 10:
                raise AssertionError("blk00000.dat not pruned when it should be")

        print("Success")
        usage = calc_usage(self.prunedir)
        print("Usage should be below target:", usage)
        if (usage > 550):
            raise AssertionError("Pruning target not being met")


    def create_chain_with_staleblocks(self):
        # Create stale blocks in manageable sized chunks
        print("Mine 24 (stale) blocks on Node 1, followed by 25 (main chain) block reorg from Node 0, for 12 rounds")

        for j in range(12):
            # Disconnect node 0 so it can mine a longer reorg chain without knowing about node 1's soon-to-be-stale chain
            # Node 2 stays connected, so it hears about the stale blocks and then reorg's when node0 reconnects
            # Stopping node 0 also clears its mempool, so it doesn't have node1's transactions to accidentally mine
            stop_node(self.nodes[0],0)
            self.nodes[0]=start_node(0, self.options.tmpdir, ["-debug","-rpcservertimeout=0", "-maxreceivebuffer=20000","-blockmaxsize=999000", "-checkblocks=5"], timewait=900)
            # Mine 24 blocks in node 1
            wallet = []
            for i in range(24):
                if j == 0:
                    generateBlock(self.nodes[1], 1024*1024, [self.address[1]], wallet)
                else:
                    self.nodes[1].generate(1) #tx's already in mempool from previous disconnects

            # Reorg back with 25 block chain from node 0
            self.utxo = self.nodes[0].listunspent()
            for i in range(25):
                mine_large_block(self.nodes[0], self.utxo_cache_0)

            # Create connections in the order so both nodes can see the reorg at the same time
            connect_nodes(self.nodes[1], 0)
            connect_nodes(self.nodes[2], 0)
            sync_blocks(self.nodes[0:4])

#        print("Usage can be over target because of high stale rate:", calc_usage(self.prunedir))

    def reorg_test(self):
        # Node 1 will mine a 300 block chain starting 287 blocks back from Node 0 and Node 2's tip
        # This will cause Node 2 to do a reorg requiring 288 blocks of undo data to the reorg_test chain
        # Reboot node 1 to clear its mempool (hopefully make the invalidate faster)
        # Lower the block max size so we don't keep mining all our big mempool transactions (from disconnected blocks)
        stop_node(self.nodes[1],1)
        self.nodes[1]=start_node(1, self.options.tmpdir, ["-debug","-rpcservertimeout=0", "-maxreceivebuffer=20000","-blockmaxsize=5000", "-checkblocks=5", "-disablesafemode"], timewait=900)

        height = self.nodes[1].getblockcount()
        print("Current block height:", height)

        invalidheight = height-287
        badhash = self.nodes[1].getblockhash(invalidheight)
        print("Invalidating block at height:",invalidheight,badhash)
        self.nodes[1].invalidateblock(badhash)

        # We've now switched to our previously mined-24 block fork on node 1, but thats not what we want
        # So invalidate that fork as well, until we're on the same chain as node 0/2 (but at an ancestor 288 blocks ago)
        mainchainhash = self.nodes[0].getblockhash(invalidheight - 1)
        curhash = self.nodes[1].getblockhash(invalidheight - 1)
        while curhash != mainchainhash:
            self.nodes[1].invalidateblock(curhash)
            curhash = self.nodes[1].getblockhash(invalidheight - 1)

        assert(self.nodes[1].getblockcount() == invalidheight - 1)
        print("New best height", self.nodes[1].getblockcount())

        # Reboot node1 to clear those giant tx's from mempool
        stop_node(self.nodes[1],1)
        self.nodes[1]=start_node(1, self.options.tmpdir, ["-debug","-rpcservertimeout=0", "-maxreceivebuffer=20000","-blockmaxsize=5000", "-checkblocks=5", "-disablesafemode"], timewait=900)

        print("Generating new longer chain of 300 more blocks")
        self.nodes[1].generate(300)

        print("Reconnect nodes")
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[2], 1)
        connect_nodes(self.nodes[3], 1)
        sync_blocks(self.nodes[0:4])

        print("Verify height on node 2(sequential):",self.nodes[2].getblockcount())
        print("Usage possibly still high bc of stale blocks in block files(sequential):", calc_usage(self.prunedirs[0]))
        print("Verify height on node 3(leveldb):",self.nodes[3].getblockcount())
        print("Usage possibly still high bc of stale blocks in block files(leveldb):", calc_usage(self.prunedirs[1]))

        #top_node(self.nodes[1],1)
        print("Mine 220 more blocks so we have requisite history (some blocks will be big and cause pruning of previous chain)")
        for i in range(220):
            self.nodes[0].generate(1)
            sync_blocks(self.nodes[0:4])

        index = 0
        while index < len(self.prunedirs):
            usage = calc_usage(self.prunedirs[index])
            print("Usage for index:", index, " should be below target:", usage)
            if (usage > 550):
                raise AssertionError("Pruning target not being met")
            index = index + 1

        return invalidheight,badhash

    def reorg_back(self, index):
        nodeindex = index + 2 # we add 2 to index to get node index because nodes 0 and 1 are mining nodes
        # Verify that a block on the old main chain fork has been pruned away
        try:
            self.nodes[nodeindex].getblock(self.forkhash)
            raise AssertionError("Old block wasn't pruned so can't test redownload")
        except JSONRPCException as e:
            print("Will need to redownload block",self.forkheight)

        # Verify that we have enough history to reorg back to the fork point
        # Although this is more than 288 blocks, because this chain was written more recently
        # and only its other 299 small and 220 large block are in the block files after it,
        # its expected to still be retained
        self.nodes[nodeindex].getblock(self.nodes[nodeindex].getblockhash(self.forkheight))

        first_reorg_height = self.nodes[nodeindex].getblockcount()
        curchainhash = self.nodes[nodeindex].getblockhash(self.mainchainheights[index])
        self.nodes[nodeindex].invalidateblock(curchainhash)
        goalbestheight = self.mainchainheights[index]
        goalbesthash = self.mainchainhashes[index]

        # As of 0.10 the current block download logic is not able to reorg to the original chain created in
        # create_chain_with_stale_blocks because it doesn't know of any peer thats on that chain from which to
        # redownload its missing blocks.
        # Invalidate the reorg_test chain in node 0 as well, it can successfully switch to the original chain
        # because it has all the block data.
        # However it must mine enough blocks to have a more work chain than the reorg_test chain in order
        # to trigger node 2's block download logic.
        # At this point node 2 is within 288 blocks of the fork point so it will preserve its ability to reorg
        if index == 0:
            # only mine when index = 0 because we can test the other storage modes with the same blocks
            if self.nodes[nodeindex].getblockcount() < self.mainchainheights[index]:
                blocks_to_mine = first_reorg_height + 1 - self.mainchainheights[index]
                print("Rewind node 0 to prev main chain to mine longer chain to trigger redownload. Blocks needed:", blocks_to_mine)
                self.nodes[0].invalidateblock(curchainhash)
                assert(self.nodes[0].getblockcount() == self.mainchainheight)
                assert(self.nodes[0].getbestblockhash() == self.mainchainhash2)
                goalbesthash = self.nodes[0].generate(blocks_to_mine)[-1]
                goalbestheight = first_reorg_height + 1

        print("Verify node ", nodeindex, " reorged back to the main chain, some blocks of which it had to redownload")
        waitstart = time.time()
        while self.nodes[nodeindex].getblockcount() < goalbestheight:
            time.sleep(0.1)
            if time.time() - waitstart > 900:
                raise AssertionError("Node ", nodeindex ," didn't reorg to proper height")
        assert(self.nodes[nodeindex].getbestblockhash() == goalbesthash)
        # Verify we can now have the data for a block previously pruned
        assert(self.nodes[nodeindex].getblock(self.forkhash)["height"] == self.forkheight)

    def run_test(self):
        print("Warning! This test requires 4GB of disk space and takes over 30 mins (up to 2 hours)")
        print("Mining a big blockchain of 995 blocks")
        self.create_big_chain()
        # Chain diagram key:
        # *   blocks on main chain
        # +,&,$,@ blocks on other forks
        # X   invalidated block
        # N1  Node 1
        #
        # Start by mining a simple chain that all nodes have
        # N0=N1=N2 **...*(995)

        print("Check that we haven't started pruning yet because we're below PruneAfterHeight")
        index = 0
        while index < len(self.prunedirs):
            self.test_height_min(index)
            index = index + 1
        # Extend this chain past the PruneAfterHeight
        # N0=N1=N2 **...*(1020)

        print("Check that block files are pruned after a sync that has also mined new blocks")
        # When new blocks are mined while a node is syncing the chain from the beginning,
        # thos newly mined blocks should not get included in a block file until the chain is almost
        # sync'd. If this were to be allowed to happen then those early block files may not be
        # prunable because they contain newer blocks.
        #self.test_height_after_sync() TODO:  comment out for now until we can fix the "regtest" issue with IBD and new blocks

        print("Check that we'll exceed disk space target if we have a very high stale block rate")
        self.create_chain_with_staleblocks()
        # Disconnect N0
        # And mine a 24 block chain on N1 and a separate 25 block chain on N0
        # N1=N2 **...*+...+(1044)
        # N0    **...**...**(1045)
        #
        # reconnect nodes causing reorg on N1 and N2
        # N1=N2 **...*(1020) *...**(1045)
        #                   \
        #                    +...+(1044)
        #
        # repeat this process until you have 12 stale forks hanging off the
        # main chain on N1 and N2
        # N0    *************************...***************************(1320)
        #
        # N1=N2 **...*(1020) *...**(1045) *..         ..**(1295) *...**(1320)
        #                   \            \                      \
        #                    +...+(1044)  &..                    $...$(1319)

        # Save some current chain state for later use
        self.mainchainheights.append(self.nodes[2].getblockcount())   #1320
        self.mainchainheights.append(self.nodes[3].getblockcount())   #1320
        self.mainchainhashes.append(self.nodes[2].getblockhash(self.mainchainheights[0]))
        self.mainchainhashes.append(self.nodes[3].getblockhash(self.mainchainheights[1]))

        print("Check that we can survive a 288 block reorg still")
        (self.forkheight,self.forkhash) = self.reorg_test() #(1033, )
        # Now create a 288 block reorg by mining a longer chain on N1
        # First disconnect N1
        # Then invalidate 1033 on main chain and 1032 on fork so height is 1032 on main chain
        # N1   **...*(1020) **...**(1032)X..
        #                  \
        #                   ++...+(1031)X..
        #
        # Now mine 300 more blocks on N1
        # N1    **...*(1020) **...**(1032) @@...@(1332)
        #                 \               \
        #                  \               X...
        #                   \                 \
        #                    ++...+(1031)X..   ..
        #
        # Reconnect nodes and mine 220 more blocks on N1
        # N1    **...*(1020) **...**(1032) @@...@@@(1552)
        #                 \               \
        #                  \               X...
        #                   \                 \
        #                    ++...+(1031)X..   ..
        #
        # N2    **...*(1020) **...**(1032) @@...@@@(1552)
        #                 \               \
        #                  \               *...**(1320)
        #                   \                 \
        #                    ++...++(1044)     ..
        #
        # N0    ********************(1032) @@...@@@(1552)
        #                                 \
        #                                  *...**(1320)

        print("Test that we can rerequest a block we previously pruned if needed for a reorg")
        index = 0
        while index < len(self.prunedirs):
            self.reorg_back(index)
        # Verify that N2 still has block 1033 on current chain (@), but not on main chain (*)
        # Invalidate 1033 on current chain (@) on N2 and we should be able to reorg to
        # original main chain (*), but will require redownload of some blocks
        # In order to have a peer we think we can download from, must also perform this invalidation
        # on N0 and mine a new longest chain to trigger.
        # Final result:
        # N0    ********************(1032) **...****(1553)
        #                                 \
        #                                  X@...@@@(1552)
        #
        # N2    **...*(1020) **...**(1032) **...****(1553)
        #                 \               \
        #                  \               X@...@@@(1552)
        #                   \
        #                    +..
        #
        # N1 doesn't change because 1033 on main chain (*) is invalid

        print("Done")

if __name__ == '__main__':
    PruneTest().main()


# Create a convenient function for an interactive python debugging session
def Test():
    t = PruneTest()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"]
    }


    flags = []
    # you may want these additional flags:
    # flags.append("--nocleanup")
    # flags.append("--noshutdown")

    # Execution is much faster if a ramdisk is used, so use it if one exists in a typical location
    if os.path.isdir("/blockchains/test"):
        flags.append("--tmpdir=/blockchains/test")

    # Out-of-source builds are awkward to start because they need an additional flag
    # automatically add this flag during testing for common out-of-source locations
    here = os.path.dirname(os.path.abspath(__file__))
    if not os.path.exists(os.path.abspath(here + "/../../src/bitcoind")):
        dbg = os.path.abspath(here + "/../../debug/src/bitcoind")
        rel = os.path.abspath(here + "/../../release/src/bitcoind")
        if os.path.exists(dbg):
            print("Running from the debug directory (%s)" % dbg)
            flags.append("--srcdir=%s" % os.path.dirname(dbg))
        elif os.path.exists(rel):
            print("Running from the release directory (%s)" % rel)
            flags.append("--srcdir=%s" % os.path.dirname(rel))

    t.main(flags, bitcoinConf, None)
