#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from datetime import datetime
import math
import multiprocessing
import re
import sys
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from threading import Thread

NUM_PROCS = multiprocessing.cpu_count()
BEGIN_EPOCH = datetime(year=1970, month=1, day=1)
CURRENT_SECS = (datetime.now() - BEGIN_EPOCH).total_seconds()
OUT_FILE = '/tmp/compare_block_compression_%d.csv' % CURRENT_SECS
MEMPOOL_INFO_SIZE = 8


# running main will force system exit otherwise
sys.exit = lambda x: None


class BlockTest(BitcoinTestFramework):

    def __init__(self, block_type, n_txs, n_nodes=NUM_PROCS+1):
        self.block_type = block_type
        self.n_txs = n_txs
        self.n_nodes = n_nodes
        self.rep = False
        self.stats = {}

        BitcoinTestFramework.__init__(self)


    def setup_chain(self):
        print ("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, self.n_nodes)

    def setup_network(self, split=False):
        node_opts = [
            "-rpcservertimeout=0",
            "-excessiveblocksize=6000000",
            "-blockprioritysize=6000000",
            "-blockmaxsize=6000000"]

        if self.block_type == 'graphene':
            node_opts.append("-debug=graphene")
            node_opts.append("-use-grapheneblocks=1")
            node_opts.append("-use-thinblocks=0")
        elif self.block_type == 'thin':
            node_opts.append("-debug=thin")
            node_opts.append("-use-grapheneblocks=0")
            node_opts.append("-use-thinblocks=1")

        self.nodes = [start_node(i, self.options.tmpdir, node_opts) for i in range(self.n_nodes)]

        interconnect_nodes(self.nodes)
        self.is_network_split = False
        self.sync_all()

    def extract_stats(self, node):
        gni = node.getnetworkinfo()

        if self.block_type == 'graphene':
            assert "grapheneblockstats" in gni

            tbs = gni["grapheneblockstats"]
        elif self.block_type == 'thin':
            assert "thinblockstats" in gni

            tbs = gni["thinblockstats"]

        return tbs

    def extract_bytes(self, raw_result):
        unit_byte_map = {'B': 1, 'KB': 2**10, 'MB': 2**20, 'GB': 2**30, 'TB': 2**40}

        m = re.match('.*?: ([\d]+.[\d]+)([A-Z]+)', raw_result)

        if m is None:
            raise RuntimeError('Numerical value could not be extracted from: %s' % raw_result)

        value = float(m.group(1))
        units = m.group(2)

        if units not in unit_byte_map:
            raise KeyError('Unknown unit type: %s' % units)

        return int(value * unit_byte_map[units])

    def node_sends(self, node_id, addr, n_txs):
        for i in range(n_txs):
            self.nodes[node_id].sendtoaddress(addr, Decimal("0.001"))

    def run_test(self):
        tx_inc = max(1, int(math.ceil(self.n_txs / (self.n_nodes-1))))

        self.nodes[0].generate(max(1, self.n_txs // 100))
        self.sync_all()

        # age new coins
        self.nodes[0].generate(101)
        self.sync_all()

        # send a small amount to a lot of addresses in a single tx
        remaining_txs = self.n_txs - 1
        for i in range(1,self.n_nodes):
            send_to = {}
            n_txs = min(remaining_txs, tx_inc)

            if n_txs < 1:
                break

            remaining_txs -= n_txs

            for j in range(n_txs):
                addr = self.nodes[i].getnewaddress()
                send_to[addr] = Decimal("0.01")

            self.nodes[0].sendmany("", send_to)
            self.sync_all()

        self.nodes[0].generate(1)
        self.sync_all()

        for node in self.nodes:
            node.clearblockstats()

        # return to node[0] as multiple txs
        addr = self.nodes[0].getnewaddress()
        threads = []
        remaining_txs = self.n_txs - 1
        for i in range(1,self.n_nodes):
            n_txs = min(remaining_txs, tx_inc)

            if n_txs < 1:
                break

            remaining_txs -= n_txs

            t = Thread(target=self.node_sends, args=(i,addr,n_txs))
            t.start()
            threads.append(t)

        for i in range(1,self.n_nodes):
            t.join()

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        if self.block_type == 'graphene':
            self.stats['block_size'] = self.extract_bytes(self.extract_stats(self.nodes[0])['graphene_block_size'])
            self.stats['mempool_info_size'] = MEMPOOL_INFO_SIZE
            self.stats['rank'] = self.extract_bytes(self.extract_stats(self.nodes[0])['rank'])
            self.stats['filter'] = self.extract_bytes(self.extract_stats(self.nodes[0])['filter'])
            self.stats['iblt'] = self.extract_bytes(self.extract_stats(self.nodes[0])['iblt'])
            self.stats['full_tx_size'] = self.extract_bytes(self.extract_stats(self.nodes[0])['graphene_additional_tx_size'])

            self.stats['total_size'] = self.stats['block_size'] + self.stats['mempool_info_size'] - self.stats['full_tx_size']
        elif self.block_type == 'thin':
            self.stats['block_size'] = self.extract_bytes(self.extract_stats(self.nodes[0])['thin_block_size'])
            self.stats['filter_size'] = self.extract_bytes(self.extract_stats(self.nodes[0])['inbound_bloom_filters'])
            self.stats['full_tx_size'] = self.extract_bytes(self.extract_stats(self.nodes[0])['thin_full_tx'])

            self.stats['total_size'] = self.stats['block_size'] + self.stats['filter_size'] - self.stats['full_tx_size']


if __name__ == '__main__':
    n_txs_list = [100, 200, 400, 800, 1600]

    fd = open(OUT_FILE, 'w')
    print('Test results being written to: %s' % OUT_FILE)
    fd.write('n_txs total_gr block_gr mempool_gr filter_gr iblt_gr rank_gr tx_gr total_tn block_tn filter_tn tx_tn\n')

    for n_txs in n_txs_list:
        try:
            graphene_test = BlockTest('graphene', n_txs)
            graphene_test.main()
            graphene_stats = graphene_test.stats

            thin_test = BlockTest('thin', n_txs)
            thin_test.main()
            thin_stats = thin_test.stats
            
            stat_set = (n_txs, 
                        graphene_stats['total_size'], 
                        graphene_stats['block_size'], 
                        graphene_stats['mempool_info_size'], 
                        graphene_stats['filter'], 
                        graphene_stats['iblt'], 
                        graphene_stats['rank'], 
                        graphene_stats['full_tx_size'], 
                        thin_stats['total_size'],
                        thin_stats['block_size'],
                        thin_stats['filter_size'],
                        thin_stats['full_tx_size'])
            results_str = ' '.join([repr(stat) for stat in stat_set])
            fd.write(results_str + '\n')
            fd.flush()
        except:
            continue

    fd.close()
