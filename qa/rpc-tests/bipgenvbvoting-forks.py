#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Copyright (c) 2017 The Bitcoin developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
from test_framework.util import sync_blocks
from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import *
from test_framework.mininode import CTransaction, NetworkThread
from test_framework.blocktools import create_coinbase, create_block
from test_framework.comptool import TestInstance, TestManager
from test_framework.script import CScript, OP_1NEGATE, OP_CHECKSEQUENCEVERIFY, OP_DROP
from io import BytesIO
import time
import itertools
import tempfile

'''
This test is meant to exercise BIP forks
It is adapted from bip9-softforks.

Test description:
It tests fork deployment activation (the state machine), in the two aspects:
1. thresholds
2. grace period conditions

It uses a single node with custom forks.csv file.
The node is occasionally reset between tests.
'''


class BIPGenVBVotingForksTest(ComparisonTestFramework):

    def __init__(self):
        self.num_nodes = 1

        self.defined_forks = [ "genvbtest%d" % i for i in range(0,22) ]

    def setup_network(self):
        '''
        sets up with a custom forks.csv to test threshold / grace conditions
        also recomputes deployment start times since node is shut down and
        restarted between main test sections.
        '''
        # write a custom forks.csv file containing test deployments.
        # blocks are 1 second apart by default in this regtest
        fork_csv_filename = os.path.join(self.options.tmpdir, "forks.csv")
        # forks.csv fields:
        # network,bit,name,starttime,timeout,windowsize,threshold,minlockedblocks,minlockedtime,gbtforce
        with open(fork_csv_filename, 'wt') as fh:
            # use current time to compute offset starttimes
            self.init_time = int(time.time())
            # starttimes are offset by 30 seconds from init_time
            self.fork_starttime = self.init_time + 30
            fh.write(
            "# deployment info for network 'regtest':\n" +

            ########## THRESHOLD TESTING BITS (1-6) ############

            # bit 0: test 'csv' fork renaming/reparameterization
            "regtest,0,genvbtest0,%d,999999999999,144,108,0,0,true\n" % (self.fork_starttime) +

            # bit 1: test minimum threshold
            "regtest,1,genvbtest1,%d,999999999999,100,1,0,0,true\n" % (self.fork_starttime) +

            # bit 2: small threshold
            "regtest,2,genvbtest2,%d,999999999999,100,10,0,0,true\n" % (self.fork_starttime) +

            # bit 3: supermajority threshold
            "regtest,3,genvbtest3,%d,999999999999,100,75,0,0,true\n" % (self.fork_starttime) +

            # bit 4: high threshold
            "regtest,4,genvbtest4,%d,999999999999,100,95,0,0,true\n" % (self.fork_starttime) +

            # bit 5: max-but-one threshold
            "regtest,5,genvbtest5,%d,999999999999,100,99,0,0,true\n" % (self.fork_starttime) +

            # bit 6: max threshold
            "regtest,6,genvbtest6,%d,999999999999,100,100,0,0,true\n" % (self.fork_starttime) +

            ########## GRACE PERIOD TESTING BITS (7-21) ############

            # bit 7: one minlockedblock
            "regtest,7,genvbtest7,%d,999999999999,10,9,1,0,true\n" % (self.fork_starttime) +

            # bit 8: half a window of minlockedblocks
            "regtest,8,genvbtest8,%d,999999999999,10,9,5,0,true\n" % (self.fork_starttime) +

            # bit 9: full window of minlockedblocks
            "regtest,9,genvbtest9,%d,999999999999,10,9,10,0,true\n" % (self.fork_starttime) +

            # bit 10: just over full window of minlockedblocks
            "regtest,10,genvbtest10,%d,999999999999,10,9,11,0,true\n" % (self.fork_starttime) +

            # bit 11: one second minlockedtime
            "regtest,11,genvbtest11,%d,999999999999,10,9,0,1,true\n" % (self.fork_starttime) +

            # bit 12: half window of minlockedtime
            "regtest,12,genvbtest12,%d,999999999999,10,9,0,%d,true\n" % (self.fork_starttime, 5) +

            # bit 13: just under one full window of minlockedtime
            "regtest,13,genvbtest13,%d,999999999999,10,9,0,%d,true\n" % (self.fork_starttime, 9) +

            # bit 14: exactly one window of minlockedtime
            "regtest,14,genvbtest14,%d,999999999999,10,9,0,%d,true\n" % (self.fork_starttime, 10) +

            # bit 15: just over one window of minlockedtime
            "regtest,15,genvbtest15,%d,999999999999,10,9,0,%d,true\n" % (self.fork_starttime, 11) +

            # bit 16: one and a half window of minlockedtime
            "regtest,16,genvbtest16,%d,999999999999,10,9,0,%d,true\n" % (self.fork_starttime, 15) +

            # bit 17: one window of minblockedblocks plus one window of minlockedtime
            "regtest,17,genvbtest17,%d,999999999999,10,9,10,%d,true\n" % (self.fork_starttime, 10) +

            # bit 18: one window of minblockedblocks plus just under two windows of minlockedtime
            "regtest,18,genvbtest18,%d,999999999999,10,9,10,%d,true\n" % (self.fork_starttime, 19) +

            # bit 19: one window of minblockedblocks plus two windows of minlockedtime
            "regtest,19,genvbtest19,%d,999999999999,10,9,10,%d,true\n" % (self.fork_starttime, 20) +

            # bit 20: two windows of minblockedblocks plus two windows of minlockedtime
            "regtest,20,genvbtest20,%d,999999999999,10,9,20,%d,true\n" % (self.fork_starttime, 21) +

            # bit 21: just over two windows of minblockedblocks plus two windows of minlockedtime
            "regtest,21,genvbtest21,%d,999999999999,10,9,21,%d,true\n" % (self.fork_starttime, 20) +

            ########## NOT USED SO FAR ############
            "regtest,22,genvbtest22,%d,999999999999,100,9,0,0,true\n" % (self.fork_starttime)
            )

        self.nodes = start_nodes(1, self.options.tmpdir,
                                 extra_args=[['-debug', '-whitelist=127.0.0.1',
                                              "-forks=%s" % fork_csv_filename]],
                                 binary=[self.options.testbinary])

    def run_test(self):
        self.test = TestManager(self, self.options.tmpdir)
        self.test.add_all_connections(self.nodes)
        NetworkThread().start() # Start up network handling in another thread
        self.test.run()

    def create_transaction(self, node, coinbase, to_address, amount):
        from_txid = node.getblock(coinbase)['tx'][0]
        inputs = [{ "txid" : from_txid, "vout" : 0}]
        outputs = { to_address : amount }
        rawtx = node.createrawtransaction(inputs, outputs)
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(rawtx))
        tx.deserialize(f)
        tx.nVersion = 2
        return tx

    def sign_transaction(self, node, tx):
        signresult = node.signrawtransaction(bytes_to_hex_str(tx.serialize()))
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(signresult['hex']))
        tx.deserialize(f)
        return tx

    def generate_blocks(self, number, version, test_blocks = []):
        for i in range(number):
            print ("generate_blocks: creating block on tip %x, height %d, time %d" % (self.tip, self.height, self.last_block_time + 1))
            block = create_block(self.tip, create_coinbase(self.height), self.last_block_time + 1)
            block.nVersion = version
            block.rehash()
            block.solve()
            test_blocks.append([block, True])
            self.last_block_time += 1
            self.tip = block.sha256
            self.height += 1
        return test_blocks

    def get_bipgenvbvoting_status(self, key):
        info = self.nodes[0].getblockchaininfo()
        return info['bipgenvb_forks'][key]

    def print_rpc_status(self):
        for f in self.defined_forks:
            info = self.nodes[0].getblockchaininfo()
            print(info['bipgenvb_forks'][f])

    def test_BIP9_CSV(self, bipName, activated_version, invalidate, invalidatePostSignature, bitno):
        # generate some coins for later
        self.coinbase_blocks = self.nodes[0].generate(2)

        self.height = 3  # height of the next block to build
        self.tip = int("0x" + self.nodes[0].getbestblockhash(), 0)
        self.nodeaddress = self.nodes[0].getnewaddress()
        self.last_block_time = int(time.time())

        assert_equal(self.get_bipgenvbvoting_status(bipName)['status'], 'defined')
        tmpl = self.nodes[0].getblocktemplate({})
        assert(bipName not in tmpl['rules'])
        assert(bipName not in tmpl['vbavailable'])
        assert_equal(tmpl['vbrequired'], 0)
        assert_equal(tmpl['version'], 0x20000000)

        # Test 1
        # Advance from DEFINED to STARTED
        test_blocks = self.generate_blocks(141, 4)
        yield TestInstance(test_blocks, sync_every_block=False)

        assert_equal(self.get_bipgenvbvoting_status(bipName)['status'], 'started')
        tmpl = self.nodes[0].getblocktemplate({})
        assert(bipName not in tmpl['rules'])
        assert_equal(tmpl['vbavailable'][bipName], bitno)
        assert_equal(tmpl['vbrequired'], 0)
        assert(tmpl['version'] & activated_version)

        # Test 2
        # Fail to achieve LOCKED_IN 100 out of 144 signal bit 1
        # using a variety of bits to simulate multiple parallel softforks
        test_blocks = self.generate_blocks(50, activated_version) # 0x20000001 (signalling ready)
        test_blocks = self.generate_blocks(20, 4, test_blocks) # 0x00000004 (signalling not)
        test_blocks = self.generate_blocks(50, activated_version, test_blocks) # 0x20000101 (signalling ready)
        test_blocks = self.generate_blocks(24, 4, test_blocks) # 0x20010000 (signalling not)
        yield TestInstance(test_blocks, sync_every_block=False)

        assert_equal(self.get_bipgenvbvoting_status(bipName)['status'], 'started')
        tmpl = self.nodes[0].getblocktemplate({})
        assert(bipName not in tmpl['rules'])
        assert_equal(tmpl['vbavailable'][bipName], bitno)
        assert_equal(tmpl['vbrequired'], 0)
        assert(tmpl['version'] & activated_version)

        # Test 3
        # 108 out of 144 signal bit 1 to achieve LOCKED_IN
        # using a variety of bits to simulate multiple parallel softforks
        test_blocks = self.generate_blocks(58, activated_version) # 0x20000001 (signalling ready)
        test_blocks = self.generate_blocks(26, 4, test_blocks) # 0x00000004 (signalling not)
        test_blocks = self.generate_blocks(50, activated_version, test_blocks) # 0x20000101 (signalling ready)
        test_blocks = self.generate_blocks(10, 4, test_blocks) # 0x20010000 (signalling not)
        yield TestInstance(test_blocks, sync_every_block=False)

        assert_equal(self.get_bipgenvbvoting_status(bipName)['status'], 'locked_in')
        tmpl = self.nodes[0].getblocktemplate({})
        assert(bipName not in tmpl['rules'])

        # Test 4
        # 143 more version 536870913 blocks (waiting period-1)
        test_blocks = self.generate_blocks(143, 4)
        yield TestInstance(test_blocks, sync_every_block=False)

        assert_equal(self.get_bipgenvbvoting_status(bipName)['status'], 'locked_in')
        tmpl = self.nodes[0].getblocktemplate({})
        assert(bipName not in tmpl['rules'])

        # Test 5
        # Check that the new rule is enforced
        spendtx = self.create_transaction(self.nodes[0],
                self.coinbase_blocks[0], self.nodeaddress, 1.0)
        invalidate(spendtx)
        spendtx = self.sign_transaction(self.nodes[0], spendtx)
        spendtx.rehash()
        invalidatePostSignature(spendtx)
        spendtx.rehash()
        block = create_block(self.tip, create_coinbase(self.height), self.last_block_time + 1)
        block.nVersion = activated_version
        block.vtx.append(spendtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.rehash()
        block.solve()

        self.last_block_time += 1
        self.tip = block.sha256
        self.height += 1
        yield TestInstance([[block, True]])

        assert_equal(self.get_bipgenvbvoting_status(bipName)['status'], 'active')
        tmpl = self.nodes[0].getblocktemplate({})
        assert(bipName in tmpl['rules'])
        assert(bipName not in tmpl['vbavailable'])
        assert_equal(tmpl['vbrequired'], 0)
        assert(not (tmpl['version'] & (1 << bitno)))

        # Test 6
        # Check that the new sequence lock rules are enforced
        spendtx = self.create_transaction(self.nodes[0],
                self.coinbase_blocks[1], self.nodeaddress, 1.0)
        invalidate(spendtx)
        spendtx = self.sign_transaction(self.nodes[0], spendtx)
        spendtx.rehash()
        invalidatePostSignature(spendtx)
        spendtx.rehash()

        block = create_block(self.tip, create_coinbase(self.height), self.last_block_time + 1)
        block.nVersion = 5
        block.vtx.append(spendtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.rehash()
        block.solve()
        self.last_block_time += 1
        yield TestInstance([[block, False]])
        # Restart all
        stop_nodes(self.nodes)
        wait_bitcoinds()
        shutil.rmtree(self.options.tmpdir)
        self.setup_chain()
        self.setup_network()
        self.test.clear_all_connections()
        self.test.add_all_connections(self.nodes)
        NetworkThread().start() # Start up network handling in another thread

    def test_BIPGenVBGraceConditions(self):

        # the fork bits used to check grace period conditions
        gracebits = self.defined_forks[7:22]

        print("begin test_BIPGenVBGraceConditions test")
        node = self.nodes[0]
        self.tip = int("0x" + node.getbestblockhash(), 0)
        header = node.getblockheader("0x%x" % self.tip)
        assert_equal(header['height'], 0)

        # Test 1
        # generate a block, seems necessary to get RPC working
        self.coinbase_blocks = self.nodes[0].generate(1)
        self.height = 2
        self.last_block_time = int(time.time())
        self.tip = int("0x" + node.getbestblockhash(), 0)
        test_blocks = self.generate_blocks(1, 0x20000000)  # do not set bit 0 yet
        yield TestInstance(test_blocks, sync_every_block=False)

        bcinfo = self.nodes[0].getblockchaininfo()
        # check bits 7-15 , they should be in DEFINED
        for f in gracebits:
            assert_equal(bcinfo['bipgenvb_forks'][f]['bit'], int(f[9:]))
            assert_equal(bcinfo['bipgenvb_forks'][f]['status'], 'defined')

        # move to starttime
        moved_to_started = False
        while self.last_block_time < self.fork_starttime or self.height % 10:
            test_blocks = self.generate_blocks(1, 0x20000000)
            yield TestInstance(test_blocks, sync_every_block=False)
            time.sleep(3)  # need to actually give daemon a little time to change the state
            bcinfo = self.nodes[0].getblockchaininfo()
            for f in gracebits:
                # transition to STARTED must occur if this is true
                if self.last_block_time >= self.fork_starttime and self.height % 10 == 0:
                    moved_to_started = True
                    time.sleep(3)  # need to actually give daemon a little time to change the state

                if moved_to_started:
                    assert_equal(bcinfo['bipgenvb_forks'][f]['status'], 'started')
                else:
                    assert_equal(bcinfo['bipgenvb_forks'][f]['status'], 'defined')

        # lock all of them them in by producing 9 signaling blocks out of 10
        test_blocks = self.generate_blocks(9, 0x203fff80)
        # tenth block doesn't need to signal
        test_blocks = self.generate_blocks(1, 0x20000000, test_blocks)
        yield TestInstance(test_blocks, sync_every_block=False)
        # check bits 7-15 , they should all be in LOCKED_IN
        bcinfo = self.nodes[0].getblockchaininfo()
        print("checking all grace period forks are locked in")
        for f in gracebits:
            assert_equal(bcinfo['bipgenvb_forks'][f]['status'], 'locked_in')

        # now we just check that they turn ACTIVE only when their configured
        # conditions are all met. Reminder: window size is 10 blocks, inter-
        # block time is 1 sec for the synthesized chain.
        #
        # Grace conditions for the bits 7-21:
        # -----------------------------------
        # bit 7:  minlockedblocks= 1, minlockedtime= 0  -> activate next sync
        # bit 8:  minlockedblocks= 5, minlockedtime= 0  -> activate next sync
        # bit 9:  minlockedblocks=10, minlockedtime= 0  -> activate next sync
        # bit 10: minlockedblocks=11, minlockedtime= 0  -> activate next plus one sync
        # bit 11: minlockedblocks= 0, minlockedtime= 1  -> activate next sync
        # bit 12: minlockedblocks= 0, minlockedtime= 5  -> activate next sync
        # bit 13: minlockedblocks= 0, minlockedtime= 9  -> activate next sync
        # bit 14: minlockedblocks= 0, minlockedtime=10  -> activate next sync
        # bit 15: minlockedblocks= 0, minlockedtime=11  -> activate next plus one sync
        # bit 16: minlockedblocks= 0, minlockedtime=15  -> activate next plus one sync
        # bit 17: minlockedblocks=10, minlockedtime=10  -> activate next sync
        # bit 18: minlockedblocks=10, minlockedtime=19  -> activate next plus one sync
        # bit 19: minlockedblocks=10, minlockedtime=20  -> activate next plus one sync
        # bit 20: minlockedblocks=20, minlockedtime=21  -> activate next plus two sync
        # bit 21: minlockedblocks=21, minlockedtime=20  -> activate next plus two sync

        # check the forks supposed to activate just one period after lock-in ("at next sync")

        test_blocks = self.generate_blocks(10, 0x20000000)
        yield TestInstance(test_blocks, sync_every_block=False)
        bcinfo = self.nodes[0].getblockchaininfo()
        activation_states = [ bcinfo['bipgenvb_forks'][f]['status'] for f in gracebits ]
        assert_equal(activation_states, ['active',
                                         'active',
                                         'active',
                                         'locked_in',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'locked_in',
                                         'locked_in',
                                         'active',
                                         'locked_in',
                                         'locked_in',
                                         'locked_in',
                                         'locked_in'
                                         ])

        # check the ones supposed to activate at next+1 sync
        test_blocks = self.generate_blocks(10, 0x20000000)
        yield TestInstance(test_blocks, sync_every_block=False)
        bcinfo = self.nodes[0].getblockchaininfo()
        activation_states = [ bcinfo['bipgenvb_forks'][f]['status'] for f in gracebits ]
        assert_equal(activation_states, ['active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'locked_in',
                                         'locked_in'
                                         ])

        # check the ones supposed to activate at next+2 period
        test_blocks = self.generate_blocks(10, 0x20000000)
        yield TestInstance(test_blocks, sync_every_block=False)
        bcinfo = self.nodes[0].getblockchaininfo()
        activation_states = [ bcinfo['bipgenvb_forks'][f]['status'] for f in gracebits ]
        assert_equal(activation_states, ['active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active'  # locked_in
                                         ])

        # check the ones supposed to activate at next+2 period
        test_blocks = self.generate_blocks(10, 0x20000000)
        yield TestInstance(test_blocks, sync_every_block=False)
        bcinfo = self.nodes[0].getblockchaininfo()
        activation_states = [ bcinfo['bipgenvb_forks'][f]['status'] for f in gracebits ]
        assert_equal(activation_states, ['active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active',
                                         'active'
                                         ])

        # Restart all
        stop_nodes(self.nodes)
        wait_bitcoinds()

        # disabled this since we don't require to run repetitively

        shutil.rmtree(self.options.tmpdir)
        self.setup_chain()
        self.setup_network()
        self.test.clear_all_connections()
        self.test.add_all_connections(self.nodes)
        NetworkThread().start() # Start up network handling in another thread

    def test_BIPGenVBThresholds(self):

        print("test_BIPGenVBThresholds: begin")
        node = self.nodes[0]
        self.tip = int("0x" + node.getbestblockhash(), 0)
        header = node.getblockheader("0x%x" % self.tip)
        assert_equal(header['height'], 0)

        # Test 1
        # generate a block, seems necessary to get RPC working
        self.coinbase_blocks = self.nodes[0].generate(1)
        self.height = 2
        self.last_block_time = int(time.time())
        self.tip = int("0x" + node.getbestblockhash(), 0)
        test_blocks = self.generate_blocks(1, 0x20000000)  # do not set bit 0 yet
        yield TestInstance(test_blocks, sync_every_block=False)

        # Test 2
        # check initial DEFINED state
        # check initial forks status and getblocktemplate
        print("begin test 2")
        tmpl = self.nodes[0].getblocktemplate({})
        assert_equal(tmpl['vbrequired'], 0)
        assert_equal(tmpl['version'], 0x20000000)
        print("initial getblocktemplate:\n%s" % tmpl)

        test_blocks = []
        while self.last_block_time < self.fork_starttime or self.height < 100:
            test_blocks = self.generate_blocks(1, 0x20000001, test_blocks)
            for f in self.defined_forks:
                assert_equal(self.get_bipgenvbvoting_status(f)['bit'], int(f[9:]))
                assert_equal(self.get_bipgenvbvoting_status(f)['status'], 'defined')
                assert(f not in tmpl['rules'])
                assert(f not in tmpl['vbavailable'])

        yield TestInstance(test_blocks, sync_every_block=False)

        # Test 3
        # Advance from DEFINED to STARTED
        print("begin test 3")
        test_blocks = self.generate_blocks(1, 0x20000001)  # do not set bit 0 yet
        for f in self.defined_forks:
            if int(f[9:]) > 0:
                assert_equal(self.get_bipgenvbvoting_status(f)['status'], 'started')
                assert(f not in tmpl['rules'])
                assert(f not in tmpl['vbavailable'])
            else: # bit 0 only becomes started at height 144
                assert_equal(self.get_bipgenvbvoting_status(f)['status'], 'defined')

        yield TestInstance(test_blocks, sync_every_block=False)

        # Test 4
        # Advance from DEFINED to STARTED
        print("begin test 4")
        test_blocks = self.generate_blocks(1, 0x20000001)  # do not set bit 0 yet
        yield TestInstance(test_blocks, sync_every_block=False)

        for f in self.defined_forks:
            info = node.getblockchaininfo()
            print(info['bipgenvb_forks'][f])

        # Test 5..?
        # Advance from DEFINED to STARTED
        print("begin test 5 .. x - move to height 144 for bit 0 start")
        # we are not yet at height 144, so bit 0 is still defined
        assert_equal(self.get_bipgenvbvoting_status(self.defined_forks[0])['status'], 'defined')
        # move up until it starts
        while self.height < 144:
            print("last block time has not reached fork_starttime, difference: %d" % (self.fork_starttime - self.last_block_time))
            #self.nodes[0].generate(1)
            test_blocks = self.generate_blocks(1, 0x20000001)
            yield TestInstance(test_blocks, sync_every_block=False)
            if int(f[9:]) > 0:
                assert_equal(self.get_bipgenvbvoting_status(f)['status'], 'started')
                assert(f not in tmpl['rules'])
                assert(f not in tmpl['vbavailable'])
            else: # bit 0 only becomes started at height 144
                assert_equal(self.get_bipgenvbvoting_status(f)['status'], 'defined')

        # generate block 144
        #test_blocks = self.generate_blocks(1, 0x20000001, test_blocks)
        test_blocks = []
        # now it should be started

        yield TestInstance(test_blocks, sync_every_block=False)
        assert_equal(self.get_bipgenvbvoting_status(self.defined_forks[0])['status'], 'started')

        tmpl = self.nodes[0].getblocktemplate({})
        assert(self.defined_forks[0] not in tmpl['rules'])
        assert_equal(tmpl['vbavailable'][self.defined_forks[0]], 0)
        assert_equal(tmpl['vbrequired'], 0)
        assert(tmpl['version'] & 0x20000000 + 2**0)

        # move to start of new 100 block window
        # start signaling for bit 6 so we can get a full 100 block window for it
        # over the next period
        test_blocks = self.generate_blocks(100 - (self.height % 100), 0x20000040)
        yield TestInstance(test_blocks, sync_every_block=False)
        assert(self.height % 100 == 0)

        # generate enough of bits 1-6 in next 100 blocks to lock in fork bits 1-6
        # bit 0 will only be locked in at next multiple of 144
        test_blocks = []
        # 1 block total for bit 1
        test_blocks = self.generate_blocks(1, 0x2000007F)
        yield TestInstance(test_blocks, sync_every_block=False)
        # check still STARTED until we get to multiple of window size
        assert_equal(self.get_bipgenvbvoting_status(self.defined_forks[1])['status'], 'started')

        # 10 blocks total for bit 2
        test_blocks = self.generate_blocks(9, 0x2000007D)
        yield TestInstance(test_blocks, sync_every_block=False)
        assert_equal(self.get_bipgenvbvoting_status(self.defined_forks[2])['status'], 'started')

        # 75 blocks total for bit 3
        test_blocks = self.generate_blocks(65, 0x20000079)
        yield TestInstance(test_blocks, sync_every_block=False)
        assert_equal(self.get_bipgenvbvoting_status(self.defined_forks[3])['status'], 'started')

        # 95 blocks total for bit 4
        test_blocks = self.generate_blocks(20, 0x20000071)
        yield TestInstance(test_blocks, sync_every_block=False)
        assert_equal(self.get_bipgenvbvoting_status(self.defined_forks[4])['status'], 'started')

        # 99 blocks total for bit 5
        test_blocks = self.generate_blocks(4, 0x20000061)
        yield TestInstance(test_blocks, sync_every_block=False)
        assert_equal(self.get_bipgenvbvoting_status(self.defined_forks[5])['status'], 'started')

        # 100 blocks total for bit 6
        test_blocks = self.generate_blocks(1, 0x20000041)
        yield TestInstance(test_blocks, sync_every_block=False)
        assert(self.height % 100 == 0)

        # debug trace
        for f in self.defined_forks[1:7]:
            info = node.getblockchaininfo()
            print(info['bipgenvb_forks'][f])
            assert_equal(self.get_bipgenvbvoting_status(f)['status'], 'locked_in')


        assert_equal(self.get_bipgenvbvoting_status(self.defined_forks[0])['status'], 'started')
        # move until it bit 0 locks in
        one_hundreds_active = False  # to count the 100-block bits 1-6 going active after 100 more
        while self.height % 144 != 0:
            test_blocks = self.generate_blocks(1, 0x20000001)
            yield TestInstance(test_blocks, sync_every_block=False)

            bcinfo = self.nodes[0].getblockchaininfo()
            # check bit 0 - it is locked in after this loop exits
            if self.height % 144:
                assert_equal(bcinfo['bipgenvb_forks'][self.defined_forks[0]]['status'], 'started')
            else:
                assert_equal(bcinfo['bipgenvb_forks'][self.defined_forks[0]]['status'], 'locked_in')
            # bits 1-6 should remain LOCKED_IN
            for f in self.defined_forks[1:7]:
                if self.height % 100:
                    if not one_hundreds_active:
                        assert_equal(bcinfo['bipgenvb_forks'][f]['status'], 'locked_in')
                    else:
                        assert_equal(bcinfo['bipgenvb_forks'][f]['status'], 'active')
                else:
                    # mark them expected active henceforth
                    one_hundreds_active = True
                    assert_equal(bcinfo['bipgenvb_forks'][f]['status'], 'active')

        assert_equal(self.get_bipgenvbvoting_status(self.defined_forks[0])['status'], 'locked_in')

        # Restart all
        stop_nodes(self.nodes)
        wait_bitcoinds()

        # disabled this since we don't require to run repetitively

        shutil.rmtree(self.options.tmpdir)
        self.setup_chain()
        self.setup_network()
        self.test.clear_all_connections()
        self.test.add_all_connections(self.nodes)
        NetworkThread().start() # Start up network handling in another thread


    def get_tests(self):
        '''
        run various tests
        '''
        # CSV (bit 0) for backward compatibility with BIP9
        for test in itertools.chain(
                self.test_BIPGenVBGraceConditions(), # test grace periods
                self.test_BIPGenVBThresholds(),  # test thresholds on other bits
        ):
            yield test

    def donothing(self, tx):
        ''' used by CSV tests '''
        return

    def csv_invalidate(self, tx):
        '''
        Used by CSV tests:
        Modifies the signature in vin 0 of the tx to fail CSV
        Prepends -1 CSV DROP in the scriptSig itself.
        '''
        tx.vin[0].scriptSig = CScript([OP_1NEGATE, OP_CHECKSEQUENCEVERIFY, OP_DROP] +
                                      list(CScript(tx.vin[0].scriptSig)))

    def sequence_lock_invalidate(self, tx):
        '''
        Used by CSV tests:
        Modify the nSequence to make it fails once sequence lock rule is activated (high timespan)
        '''
        tx.vin[0].nSequence = 0x00FFFFFF
        tx.nLockTime = 0

    def mtp_invalidate(self, tx):
        '''
        Used by CSV tests:
        Modify the nLockTime to make it fails once MTP rule is activated
        '''
        # Disable Sequence lock, Activate nLockTime
        tx.vin[0].nSequence = 0x90FFFFFF
        tx.nLockTime = self.last_block_time


if __name__ == '__main__':
    BIPGenVBVotingForksTest().main()
