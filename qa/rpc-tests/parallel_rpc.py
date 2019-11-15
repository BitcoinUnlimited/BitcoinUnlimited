#!/usr/bin/env python3.8
# Copyright (c) 2015-2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
# This is a template to make creating new QA tests easy.
# You can also use this template to quickly start and connect a few regtest nodes.

import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

import asyncio


async def run(cmd):
    # print(cmd)
    proc = await asyncio.create_subprocess_shell(
        cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE)

    stdout, stderr = await proc.communicate()
    return (proc.returncode, stdout, stderr)


async def runabunch(*args):
    tasks = []
    for a in args:
        tasks.append(asyncio.create_task(run(a)))

    ret = []
    for t in tasks:
        ret.append(await t)
    return ret


class MyTest (BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        # pick this one to start from the cached 4 node 100 blocks mined configuration
        initialize_chain(self.options.tmpdir, bitcoinConfDict, wallets)
        # pick this one to start at 0 mined blocks
        # initialize_chain_clean(self.options.tmpdir, 2, bitcoinConfDict, wallets)
        # Number of nodes to initialize ----------> ^

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        # Nodes to start --------^
        # Note for this template I readied 4 nodes but only started 2

        # Now interconnect the nodes
        connect_nodes_full(self.nodes)
        # Let the framework know if the network is fully connected.
        # If not, the framework assumes this partition: (0,1) and (2,3)
        # For more complex partitions, you can't use the self.sync* member functions
        self.is_network_split=False
        self.sync_blocks()

    def run_test (self):

        # generate blocks so that nodes[0] has a enough balance
        self.sync_blocks()
        self.nodes[0].generate(10)
        self.sync_blocks()
        self.nodes[1].generate(10)

        binpath = findBitcoind() + os.sep

        cli = binpath + "bitcoin-cli -datadir=" + self.nodes[0].datadir

        logging.info("fill wallet with some UTXOs")
        addrs0 = [self.nodes[0].getnewaddress() for x in range(0,20)]
        addrs1 = [self.nodes[1].getnewaddress() for x in range(0,20)]
        for i in range(1,200):
            try:
                self.nodes[0].sendtoaddress(addrs1[random.randint(0,len(addrs1)-1)], round(random.random(),8))
                self.nodes[1].sendtoaddress(addrs0[random.randint(0,len(addrs0)-1)], round(random.random(),8))
            except JSONRPCException:
                self.nodes[0].generate(1)
            if random.randint(0,50)==1:
                self.nodes[0].generate(1)
            # print(i)

        logging.info("fill done")

        addr = self.nodes[0].getnewaddress()

        startBlockHash = self.nodes[0].getbestblockhash()

        for j in range(0,100):
          for i in range(1,15):
            print("Loop: ", i,j)
            # multiplying the list by a random number will cause the command to be included between 0 and i times
            cmds = []
            cmds += [cli + " getnewaddress"] * random.randint(0,i)
            cmds += [cli + " sendtoaddress " + addr + " 0.01"] * random.randint(0,i)
            cmds += [cli + " getinfo"] * random.randint(0,i)
            cmds += [cli + " listunspent"] * random.randint(0,i)
            cmds += [cli + " getaccount " + addr] * random.randint(0,i)
            cmds += [cli + " getrawchangeaddress"] * random.randint(0,i)
            cmds += [cli + " listaccounts"] * random.randint(0,i)
            cmds += [cli + ' getreceivedbyaccount ""'] * random.randint(0,i)
            cmds += [cli + ' getreceivedbyaddress ' + addr] * random.randint(0,i)
            cmds += [cli + " listtransactionsfrom"] * random.randint(0,i)
            cmds += [cli + ' sendfrom "" ' + addr + " 0.002"] * random.randint(0,i)
            cmds +=  [cli + " getbalance"] * random.randint(0,i)
            cmds +=  [cli + " getunconfirmedbalance"] * random.randint(0,i)
            cmds +=  [cli + " listsinceblock " + startBlockHash + " " + str(random.randint(1,5))] * random.randint(0,i)
            cmds += [cli + ' listtransactionsfrom "*" '] * random.randint(0,i)
            cmds += [cli + ' listreceivedbyaccount 0 true true'] * random.randint(0,i)
            cmds += [cli + ' listreceivedbyaddress 0 true true'] * random.randint(0,i)
            cmds += [cli + ' listaddressgroupings'] * random.randint(0,i)
            cmds += [cli + ' keypoolrefill'] * random.randint(0,i)
            cmds += [cli + ' getwalletinfo'] * random.randint(0,i)
            cmds += [cli + ' getaddressesbyaccount ""'] * random.randint(0,i)
            cmds += [cli + ' getaccountaddress ' + addr] * random.randint(0,i)
            cmds += [cli + ' getaccount ' + addr] * random.randint(0,i)

            # Work up a big sendmany command
            s = []
            for k in range(0,random.randint(1,len(addrs0))):
                s.append('\\"' + addrs0[k] + '\\":0.011')
            tmp = cli + ' sendmany "" "{' + ", ".join(s) + '}"'
            #tmp = cli + ' sendmany "" "{ \\"' + addrs0[0] + '\\":0.011, \\"' + addrs1[1] + '\\":0.012 }"'
            cmds += [tmp ] * random.randint(0,i) 

            # Mix up the order to try to shake out timing issues
            random.shuffle(cmds)
            # logging.info(cmds)

            # Also create some TX traffic that the node has to deal with
            for k in range(0,10):
                try:
                    self.nodes[1].sendtoaddress(addrs1[random.randint(0,len(addrs1)-1)], round((random.random()/100.0)+0.001,8))
                    self.nodes[1].sendtoaddress(addrs0[random.randint(0,len(addrs0)-1)], round((random.random()/100.0)+0.001,8))
                except JSONRPCException as e:
                    if len(self.nodes[1].listunspent()) == 0:
                        self.nodes[1].generate(1)
                    else:
                        logging.info(str(e))
                        # logging.info(self.nodes[1].listunspent())

            ret = asyncio.run(runabunch(*cmds))
            # logging.info("RESULT: ")
            cnt=0
            for r in ret:
                if len(r[2])>0:  # Only print this info is something went wrong
                    logging.info("Command: " + cmds[cnt])
                    logging.info("Return code: " + str(r[0]))
                    logging.info("stdout: " + r[1].decode())
                    if len(r[2])>0:
                        logging.info("stderr:" + r[2].decode())
                        # pdb.set_trace()
                cnt+=1

            if random.randint(0,10)==1:
                self.nodes[1].generate(1)

        logging.info("test complete")



if __name__ == '__main__':
    MyTest ().main (None, { "blockprioritysize": 2000000, "rpcworkqueue": 1024 })

# Create a convenient function for an interactive python debugging session
def Test():
    t = MyTest()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000,  # we don't want any transactions rejected due to insufficient fees...
        "rpcworkqueue": 1024
    }

    flags = standardFlags()
    flags.append("--tmpdir=/ramdisk/test/t")
    t.main(flags, bitcoinConf, None)
