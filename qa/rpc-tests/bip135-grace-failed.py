#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Copyright (c) 2017 The Bitcoin developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
import test_framework.loginit
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
This test exercises BIP135 fork grace periods that check the activation timeout.
It uses a single node with custom forks.csv file.

It is originally derived from bip9-softforks.
'''


class BIP135ForksTest(ComparisonTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.defined_forks = [ "bip135test%d" % i for i in range(7,9) ]

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

            ########## GRACE PERIOD TESTING BITS (7-8) ############

            # bit 7: Test threshold not reached and timeout exceeded. Should result in failure to activate
            "regtest,7,bip135test7,%d,%d,10,9,5,0,true\n" % (self.fork_starttime, self.fork_starttime + 50) +

            # bit 8: Test threshold reached and timeout exceeded. Should result in successful activation
            "regtest,8,bip135test8,%d,%d,10,8,5,0,true\n" % (self.fork_starttime, self.fork_starttime + 50) +

            ########## NOT USED SO FAR ############
            "regtest,9,bip135test9,%d,999999999999,100,9,0,0,true\n" % (self.fork_starttime)

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
            #logging.info ("generate_blocks: creating block on tip %x, height %d, time %d" % (self.tip, self.height, self.last_block_time + 1))
            block = create_block(self.tip, create_coinbase(self.height), self.last_block_time + 1)
            block.nVersion = version
            block.rehash()
            block.solve()
            test_blocks.append([block, True])
            self.last_block_time += 1
            self.tip = block.sha256
            self.height += 1
        return test_blocks

    def get_bip135_status(self, key):
        info = self.nodes[0].getblockchaininfo()
        return info['bip135_forks'][key]

    def print_rpc_status(self):
        for f in self.defined_forks:
            info = self.nodes[0].getblockchaininfo()
            logging.info(info['bip135_forks'][f])

    def test_BIP135GraceConditions(self):
        logging.info("begin test_BIP135GraceConditions test")
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
        for f in self.defined_forks:
            assert_equal(bcinfo['bip135_forks'][f]['bit'], int(f[10:]))
            assert_equal(bcinfo['bip135_forks'][f]['status'], 'defined')

        # move to starttime
        moved_to_started = False
        bcinfo = self.nodes[0].getblockchaininfo()
        tip_mediantime = int(bcinfo['mediantime'])
        while tip_mediantime < self.fork_starttime or self.height % 10:
            test_blocks = self.generate_blocks(1, 0x20000000)
            yield TestInstance(test_blocks, sync_every_block=False)
            bcinfo = self.nodes[0].getblockchaininfo()
            tip_mediantime = int(bcinfo['mediantime'])
            for f in self.defined_forks:
                # transition to STARTED must occur if this is true
                if tip_mediantime >= self.fork_starttime and self.height % 10 == 0:
                    moved_to_started = True

                if moved_to_started:
                    assert_equal(bcinfo['bip135_forks'][f]['status'], 'started')
                else:
                    assert_equal(bcinfo['bip135_forks'][f]['status'], 'defined')

        # Lock one of them them in by producing 8 signaling blocks out of 10.
        # The one that is not locked in will be one block away from lock in.
        test_blocks = self.generate_blocks(8, 0x203fff80)
        # last two blocks don't need to signal
        test_blocks = self.generate_blocks(2, 0x20000000, test_blocks)
        yield TestInstance(test_blocks, sync_every_block=False)
        # check bits 7-8 , only 8 should be LOCKED_IN
        bcinfo = self.nodes[0].getblockchaininfo()
        logging.info("checking all grace period forks are locked in")
        activation_states = [ bcinfo['bip135_forks'][f]['status'] for f in self.defined_forks ]
        assert_equal(activation_states, ['started',
                                         'locked_in'
                                         ])

        # now we just check that they turn ACTIVE only when their configured
        # conditions are all met. Reminder: window size is 10 blocks, inter-
        # block time is 1 sec for the synthesized chain.
        #
        # Grace conditions for the bits 7-8:
        # -----------------------------------
        # bit 7:  minlockedblocks= 5, minlockedtime= 0  -> started next sync
        # bit 8:  minlockedblocks= 5, minlockedtime= 0  -> activate next sync

        # check the forks supposed to activate just one period after lock-in ("at next sync")
        # and move the time to just before timeout. Bit 8 should become active and neither should fail.
        # Set the last block time to 6 seconds before the timeout...since blocks get mined one second
        # apart this will put the MTP at 1 second behind the timeout, and thus the activation will not fail.
        self.last_block_time = self.fork_starttime + 50 - 6

        test_blocks = self.generate_blocks(10, 0x20000000)
        yield TestInstance(test_blocks, sync_every_block=False)
        bcinfo = self.nodes[0].getblockchaininfo()
        activation_states = [ bcinfo['bip135_forks'][f]['status'] for f in self.defined_forks ]
        assert_equal(activation_states, ['started',
                                         'active'
                                         ])


        # Move the time to the timeout. Bit 7 never locked in and should be 'failed',
        # whereas, Bit 8 should still be active.
        test_blocks = self.generate_blocks(10, 0x20000000)
        yield TestInstance(test_blocks, sync_every_block=False)
        bcinfo = self.nodes[0].getblockchaininfo()
        activation_states = [ bcinfo['bip135_forks'][f]['status'] for f in self.defined_forks ]
        assert_equal(activation_states, ['failed',
                                         'active'
                                         ])

    def get_tests(self):
        '''
        run various tests
        '''
        # CSV (bit 0) for backward compatibility with BIP9
        for test in itertools.chain(
                self.test_BIP135GraceConditions(), # test grace periods
        ):
            yield test



if __name__ == '__main__':
    BIP135ForksTest().main()
