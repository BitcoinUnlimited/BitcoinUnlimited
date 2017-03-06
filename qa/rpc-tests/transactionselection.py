#!/usr/bin/env python3
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import sys
if sys.version_info[0] != 3:
    print("This script requires Python version 3")
    sys.exit(1)
    
from decimal import Decimal
import decimal
# decimal.getcontext().prec = 6
import time
import random
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.util import *
from test_framework.blocktools import *
import test_framework.script as script
import pdb
import sys
import logging
logging.basicConfig(format='%(asctime)s.%(levelname)s: %(message)s', level=logging.INFO)
import traceback

def info(type, value, tb):
   if hasattr(sys, 'ps1') or not sys.stderr.isatty():
      # we are in interactive mode or we don't have a tty-like
      # device, so we call the default hook
      sys.__excepthook__(type, value, tb)
   else:
      import traceback, pdb
      # we are NOT in interactive mode, print the exception...
      traceback.print_exception(type, value, tb)
      print
      # ...then start the debugger in post-mortem mode.
      pdb.pm()

# sys.excepthook = info

def walletContainsTx(wallet, tx):
   """Returns true if this wallet contains this transaction"""
   for t in wallet:
      if t['txid'] == tx['txid']:
         return True
   return False

def expect_exception(lam, exception, excMsg=None):
   try:
      lam()
   except exception as e:
      if excMsg is not None:
        assert(excMsg in e.error['message'])
   else:
      assert(False)

class TransactionSelectionTest (BitcoinTestFramework):
    def __init__(self,extended=False):
      self.extended = extended
      BitcoinTestFramework.__init__(self)

    def setup_chain(self,bitcoinConfDict=None,wallets=None):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir,2,bitcoinConfDict,wallets)

    def setup_network(self, split=False):
        self.nodes = []
        self.nodes.append(start_node(0,self.options.tmpdir, ["-rpcservertimeout=0"], timewait=60*10))
        self.nodes.append(start_node(1,self.options.tmpdir, ["-rpcservertimeout=0"], timewait=60*10))
        interconnect_nodes(self.nodes)
        self.is_network_split=False
        self.sync_all()

    def commitTx(self):
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_all()

    def randomUseTest(self, count=10):
        self.nodes[0].generate(20)
        self.sync_all()
        self.nodes[1].generate(120)
        self.sync_all()
        print("random use test")
        self.nodes[1].set("wallet.txFeeOverpay=20000")
        self.nodes[0].set("wallet.txFeeOverpay=1000")
        for j in range(1,count):
            a = [x.getnewaddress() for x in self.nodes ]
            for i in range(0,20):
               for n in self.nodes:
                  amt = Decimal(random.uniform(.01,20.0))
                  amt = amt.quantize(Decimal(10) ** -8)
                  amt = amt.normalize()
                  to = a[random.randint(0,len(a)-1)]
                  print ("pay %s to %s" % (str(amt),to))
                  n.sendtoaddress(to,amt)

            self.commitTx()
            for n in self.nodes:      
                wallet = n.listunspent()
                wallet.sort(key=lambda x: x["amount"],reverse=True)
                print("%s unspent: %d" % (n.url, len(wallet)))
                amts = [ w["amount"] for w in wallet]
                print([ format(w, "02.8f") for w in amts])
                # print(wallet)
        
        
    def run_test(self):
        if repTest:
           self.randomUseTest(10000)
           return
        
        self.nodes[0].generate(101)  # mine enough BTC to get 1 spendable block
        a0 = self.nodes[0].getnewaddress()
        a1 = self.nodes[1].getnewaddress()
        print ("Node 0 BTC address", a0)
        print ("Node 1 BTC address", a1)
        # send money with 0 balance
        expect_exception(lambda: self.nodes[1].sendtoaddress(a0,1), JSONRPCException, "Insufficient funds")
        # send money with insufficient funds
        expect_exception(lambda: self.nodes[0].sendtoaddress(a0,100), JSONRPCException, "Insufficient funds")
        # send money with insufficient funds for fee
        expect_exception(lambda: self.nodes[0].sendtoaddress(a0,50.1), JSONRPCException, "Insufficient funds")

        self.nodes[0].sendtoaddress(a1,10) # Move working amount to node1
        self.nodes[0].generate(1)
        self.sync_all()
        
        wallet = self.nodes[1].listunspent()
        assert(len(wallet)==1)

        # send should make 1 change address
        self.nodes[1].sendtoaddress(a0,1)        
        wallet = self.nodes[1].listunspent()
        assert(len(wallet)==0)
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_all()
        wallet = self.nodes[1].listunspent()
        assert(len(wallet)==1)
        tx = wallet[0]

        # wallet should spend the closer amount
        self.nodes[0].sendtoaddress(a1,2) # Now we have two outputs, ~9 and 2
        self.nodes[0].generate(1)
        self.sync_all()

        self.nodes[1].sendtoaddress(a0,1) # the 2 output should be chosen
        self.sync_mempools()
        self.nodes[0].generate(1)

        wallet1 = self.nodes[1].listunspent()
        assert(walletContainsTx(wallet1, tx))

        # Dust is used for fees
        self.nodes[0].sendtoaddress(a1,.005) # add some dust
        self.commitTx()
        wallet1 = self.nodes[1].listunspent()
        wallet1.sort(key=lambda x: x["amount"],reverse=True)
        
        r = self.nodes[1].sendtoaddress(a0,wallet1[1]["amount"]+Decimal(0.00001)) # spend the full amount out of a UTXO to force one of the other txos to be chosen as the fee
        print(r)
        self.commitTx()

        wallet2 = self.nodes[1].listunspent()

        self.randomUseTest()

        pdb.set_trace()
        print("done")
        
bitcoinConf = {
    "debug":["wallet"], # "lck"
    "blockprioritysize":2000000  # we don't want any transactions rejected due to insufficient fees...
    }
    
if __name__ == '__main__':  
    if "--rep" in sys.argv:
      repTest=True
      sys.argv.remove("--rep")
      logging.info("Running repetitive tests")
    else:
      repTest=False

    if "--extended" in sys.argv:
      longTest=True
      sys.argv.remove("--extended")
      logging.info("Running extended tests")
    else:
      longTest=False

    TransactionSelectionTest(longTest).main (sys.argv,bitcoinConf)


def Test():
  t = TransactionSelectionTest(True)

# "--tmpdir=/ramdisk/test"
  t.main(["--tmpdir=/ramdisk/test", "--nocleanup","--noshutdown"],bitcoinConf,None) # , "--tracerpc"])
