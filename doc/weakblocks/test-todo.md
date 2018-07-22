TODO tests to be written for weak blocks
----------------------------------------

This is a list of test ideas to hammer the weakblocks implementation
to help make sure it is as stable as possible.

Unit testing
============
 - basic functionality of API in weakblock.h

 - test storing and retrieving weak blocks, delta block detection

 - test weak block height calculation

 - test that the purgeOldWeakblocks(..) purges completely (all data
   structures empty) when told to do so


Regression network tests
========================
- Test that nodes that are configured to have no weakblocks support behave
  as expected. This includes:
     - nodes that submit weakblocks even though the service flag isn't set
       should be banned.
     - submitblock does not accept weakblock solutions.
     - mining code does not generate any weak blocks.

- Weak block enabling:
  - test that weak blocks can be submitted to another node that supports
    the weakblocks service flag without banning

  - test that weak blocks can be mined

  - test that weak blocks can be submitted through RPC

  - test for longest weak block chain of work. Create multiple chains
  of weak blocks and test that the longest and oldest chain is taken
  for further consideration and mining on top

  - test that changing the consideration POW ratio does work as expected

  - test that submitting below the min POW ratio causes a node ban, even
    with service flag enabled (flooding prevention)

  - test that weak blocks are transmitted efficiently as deltablocks between
    nodes.

Fuzzing
=======
- Write a fuzzer that exercises the exposed API of weakblock.h and
ensures that there's no crashes and no memleaks after destruction
of the remaining weakblocks before program exit.

Coverage
========
Ensure full coverage of all code changed (especially main.cpp) as well as
the newly written weakblocks code.


Other TODO
==========
Allow to set names for weakblocks for easier testing.

Send multiple weakblocks blocks in chronological order.

Allow receival of weakblocks out of order and still build correct chains, maybe?

Check enable/disable logic thoroughly
