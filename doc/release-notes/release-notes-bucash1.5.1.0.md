Release Notes for Bitcoin Unlimited Cash Edition 1.5.1.0
=========================================================

Bitcoin Unlimited Cash Edition  version 1.5.1.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a main release candidate of Bitcoin Unlimited compatible
with the Bitcoin Cash specifications you could find here:

- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/uahf-technical-spec.md (Aug 1st '17 Protocol Upgrade, bucash 1.1.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17 Protocol Upgrade, bucash 1.1.2.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/may-2018-hardfork.md (May 15th '18 Protocol Upgrade, bucash 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18 Protocol Upgrade, bucash 1.5.0.0, 1.5.0.1, 1.5.0.2)

This release is compatible with both [Bitcoin Cash](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md) and [SV](https://github.com/bitcoin-sv/bitcoin-sv/blob/master/doc/release-notes.md) changes to the consensus rules.
SV features set is **disabled by default**, the default policy is to activate the set of changes as defined by the bitcoincash.org.

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

If you are upgrading from a release older than 1.1.2.0, your UTXO database will be converted
to a new format. This step could take a variable amount of time that will depend
on the performance of the hardware you are using.

Other than that upgrading from a version lower than 1.5.0.1 your client is probably stuck
on a minority chain and need some manual intervention to make so that it follow the majority
chain after the upgrade. For more detail please look at `reconsidermostworkchain` RPC commands.

Main Changes in 1.5.1
---------------------

- 10x transaction processing performance
- Extended version message: [xversion](https://github.com/BitcoinUnlimited/BitcoinUnlimited/blob/release/doc/xversionmessage.md)
- Adding a checkpoint at height 556767 both for the SV and the BCH chain

Bug Fixed since 1.5.1.0-rc2
------------------------

- Documentation fixes
- Make it possible to disable graphene and xthin preferential timer
- Switch ctor on earlier in the init process if we are on the BCH chain
- Improve Graphene decode rates by padding IBLT
- Improve xversion QA testing
- General improvements and fixes to the QA test framework

Commit details
-------

- `77050bb2d` It seems we are ready to release final BUcash 1.5.1 (#1535) (Andrea Suisani)
- `119e8cb00` ppa update (#1529) (Søren Bredlund Caspersen)
- `867735f3b` Add a new parameter to define the duration of Graphene and Thinblock preferential timer. (#1531) (Andrea Suisani)
- `00324de78` Switch ctor on earlier in the init process if we are on the BCH chain (Andrea Suisani)
- `284176229` Remove a useless assert in PruneBlockIndexCandidates() (Andrea Suisani)
- `7c8e7220f` Add Graphene specification v1.0 (#1522) (Andrea Suisani)
- `960e00c2d` Improve Graphene decode rates by padding IBLT (#1426) (bissias)
- `7e0f47a7a` Extend CTransactionRef into a few more areas (#1472) (Peter Tschipper)
- `caed20d60` Proper multi-threaded multi-line logging and no more empty lines (#1441) (awemany)
- `941fbf39b` Some cleanups of xversion handshake testing (#1505) (awemany)
- `02b12e3ac` switch from a scoped_lock to a lock_guard in UpdateTransactionsPerSecond() (#1507) (Peter Tschipper)
- `40d22ddbf` Test framework improvements (#2) (#1461) (awemany)
- `ac12fa4b6` http -> https (#1497) (Dimitris Apostolou)
- `3d2e7570b` Avoid installing Berkeley DB via apt if NO_WALLET=1 (#1437) (Andrea Suisani)
- `5dcb4ef2d` Fix 1.5.1.0rc2 release notes layout (Andrea Suisani)
- `4c58e57ab` Fix typos: Add a bug fix to the list of changes. (Andrea Suisani)
- `40e9d7bbd` Add 1.5.1.0rc2 release notes (Andrea Suisani)
- `4699c035f` Add rc tag to BU version if rc is higher than 0 (#1519) (Andrea Suisani)
- `3918ad3ea` Allow early pings and include a functional test for that (#1517) (awemany)
- `068467377` Set EB and max script ops for the SV chain on startup. (#1518) (Peter Tschipper)
- `2ab8b61d0` Add more log info to "missing or spent" error condition in ConnectBlockDependencyOrdering (#1513) (Andrea Suisani)
- `ded3d6258` Remove old version code (#1486) (Greg Griffith)
- `7e93e1242` Integration tests: Factor out some common boilerplate code (#1512) (awemany)
- `705d745c6` .gitignore: Add output from running txPerf (#1508) (awemany)
- `5664663ad` Making xversion more permissive (#1515) (awemany)
- `cdb1eb498` Reduce preferential Graphene/Xthin timers to 1 second each (#1511) (Jonathan Toomim)
- `c891c1478` Make checkpoints modifiable and modify them for the BCH/SV chains (#1509) (Peter Tschipper)
- `f11092713` add missing DB_LAST_BLOCK write in WriteBatchSync (#1504) (Greg Griffith)
- `fb330abb1` Replace FORCE rule in src/Makefile.am (#1500) (awemany)
- `8dd66ed4d` Simplify RPC tests by cleaning up connect_nodes_bi calls (#1506) (awemany)
- `dd819e58c` Move LoadMempool() and DumpMempool() to txmempool.cpp/.h (Peter Tschipper)
- `e32353ba4` Create a default constructor for CTxInputData and use it (Peter Tschipper)
- `f25f5cfff` fix format (Peter Tschipper)
- `5ed7834c9` Control mempool persistence using a command line parameter. (John Newbery)
- `6fe423b1d` Add savemempool RPC (Lawrence Nahum)
- `4db9b7954` Add return value to DumpMempool (Lawrence Nahum)
- `ebe3af4ec` Read/write mempool.dat as a binary. (Pavel Janík)
- `6977fb50b` [Qt] Do proper shutdown (Jonas Schnelli)
- `d57439041` Allow shutdown during LoadMempool, dump only when necessary (Jonas Schnelli)
- `9071de193` enqueue transactions when loading the mempool (Peter Tschipper)
- `c852f94b3` Add DumpMempool and LoadMempool (Pieter Wuille)
- `3b803db3a` Add mempool.dat to doc/files.md (Pieter Wuille)
- `1c774653c` BUCash release notes for version 1.5.1.0 Release Candidate 1 (Andrea Suisani)
- `9f9b2a019` bump version to 1.5.1 (Andrew Stone)
- `f0a7354a9` Refactor p2p messaging (#1483) (Greg Griffith)
- `a53d1f1d7` add checkpoints for BCH and BSV chains (#1478) (Greg Griffith)
- `44307cc45` optimization 3 (#1487) (Andrew Stone)
- `ef6bbfd0a` Optimization 2 (#1485) (Andrew Stone)
- `e78c216db` if parent data structure is corrupt, assert in debug code, and clear it out and continue in release code (Andrew Stone)
- `1952c26df` Need to add FORCE when building the xversionkeys.h in Makefile.am (Peter Tschipper)
- `b5068568f` add a small test for zero checksums, tolerate them in the mininode (if configured), and simplify handling of common xversion integer value (Andrew Stone)
- `23de6af99` fix makefile to properly compile xversionkeys.h on windows (Greg Griffith)
- `57824c815` fix the way transactions from committed blocks are removed from the mempool (Andrew Stone)
- `6c6059b7f` fix review nitpicks (Andrew Stone)
- `3c5d41f71` Change settings in leveldb to prevent write pauses during reindex or … (#1459) (Peter Tschipper)
- `99ac7c54d` Temporarily disable win32 cross compilation and unit tests on Travis (Andrea Suisani)
- `7c11bcddc` Allow BU nodes to pass xversion config to drop checksums from passed messages.  These checksums inefficient and are not necessary for several reasons -- TCP already has message checksums, for one (Andrew Stone)
- `72759ccd0` Improve performance of wallet-hd.py (#1431) (Peter Tschipper)
- `5dfcf187a` Log message for REJECT_WAITING was not printing out reason code (#1475) (Peter Tschipper)
- `93675012c` Extended version message (xversion) (#1236) (awemany)
- `2a1afafef` fix qa tests for fork changes (Andrew Stone)
- `3de9bd56a` LTOR fixes for unit tests (#1479) (Greg Griffith)
- `91037d1e0` Remove DS_Store WindowBounds bytes object (Jonas Schnelli)
- `948d333f7` BUCash 1.5.0.2 release notes (Andrea Suisani)
- `617fd9998` Bump BUCash version to 1.5.0.2 (#1471) (Andrea Suisani)
- `d693dfc40` very basic docker instructions and files (#1448) (Greg Griffith)
- `268a41fc6` [TEST ONLY] Script fuzzer (#1460) (Andrew Stone)
- `73a3dda28` In GBT, match fees and sigops with the correct tx (#1470) (Andrew Stone)
- `8f8509ff9` Test framework improvements (part 1) (#1456) (awemany)
- `102e7185d` Improve propagatation of non-final and too-long-mempool-chain (#1419) (Peter Tschipper)
- `5cdec3c53` remove a few cs_main locks in QT that are no longer needed (#1469) (Peter Tschipper)
- `fbcd53fbe` Fix a couple of potential log  errors in thinblocks and graphene. (#1466) (Peter Tschipper)
- `5b2440582` Add missing cs_vNodes lock in requestmanager_tests.cpp (#1468) (Peter Tschipper)
- `3a8853125` Remove excessiveBlockSize and Acceptance Depth from QT (Peter Tschipper)
- `5f7c242ac` Don't print rpcuser or rpcpassword to the logfile. (Peter Tschipper)
- `faccfbe06` Remove ban if merkle root incorrect for graphene block (Peter Tschipper)
- `d63ceda7d` 'signdata' RPC call (#1458) (Andrew Stone)
- `dee3a88b6` Use SV upgrade specifications rather than SV 0.1.0 release notes (#1454) (Andrea Suisani)
- `2e0859704` Add instructions on how to activate SV upgrade (#1455) (Andrea Suisani)
- `4ccef23b0` depends: biplist 1.0.3 (fanquake)
- `786c1d18c` rsvg binaries are needed to convert svg to png while producing dmg on macos (#1453) (Andrea Suisani)
- `c5755e5ac` BUcash 1.5.0.1 release notes (#1452) (Andrea Suisani)
- `420e7d875` Bump BUcash version to 1.5.0.1 (Andrea Suisani)
- `94a9647f0` patch check that look at pointer address of labelPublic instead of size (Greg Griffith)
- `13d647e9b` Add lock on vNodes when checking size() in rpc getinfo() (Peter Tschipper)
- `b62f437b2` fix linker error (Andrew Stone)
- `b5984be52` Fix bug in dmg builder so that it actually reads in the configuration file (Don Patterson)
- `b9ded0885` build: Fix 'make deploy' for OSX (Cory Fields)
- `2178b1cfa` depends: mac deploy Py3 compatibility (Wladimir J. van der Laan)
- `b4ff18aec` Add RPC call 'logline' to add lines to debug.log (Awemany)
- `b3ef45ed9` Fix requestmanager_tests.cpp (Peter Tschipper)
- `7a84f29d2` Add Bitcoin SV seeders only if SV fork is activated (Andrea Suisani)
- `057295cc7` Turn miningSvForkTime and minigForkTime into CTweakRef (Andrea Suisani)
- `a85fe1650` fix format (Peter Tschipper)
- `3c4c5f166` Fix getminingcandidate (Peter Tschipper)
- `d63fee097` Reduce Transaction re-requests under heavy load (#1430) (Peter Tschipper)
- `472f47990` Some cleanup and also extend CTransactionRef into a few more areas (#1429) (Peter Tschipper)
- `7f16a286c` Fix ctor.py from failing (Peter Tschipper)
- `c64d44b74` clean up merge of new opcodes, add SV config and implement max instruction increase.  Add a config validator for the forks.  To do so, it was necessary to extend the CTweak to support validators like CTweakRef currently does.  Shorten QA test time (Andrew Stone)
- `a669fe85b` from SV: Added OP_INVERT functionality and tests (shaunOK)
- `724076254` from SV: Add OP_MUL functionality and tests (shaunOK)
- `7194b0b79` Merge from SV: Added LSHIFT and RSHIFT opcode functionality and test (shaunOK)
- `06abba7a1` Fix for #1416: can't compile without wallet unless bdb4.8 is installed (#1427) (Peter Tschipper)
- `760e6b271` Add getraworphanpool rpc (#1421) (Peter Tschipper)
- `ace8ee652` Turn graphene on by default (#1418) (Peter Tschipper)
- `83e3b7b7b` Remove run-bitcoind-for-test.sh.in (#1424) (awemany)
- `05688d3a7` Define NODE_CF service bit (#1422) (Chris Pacia)
- `e59a724d2` Fix BIP 135 voting guide URL (#1417) (Andrea Suisani)
- `41cb9be0e` build: python 3 compatibility (Wladimir J. van der Laan)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Awemany
- George Bissias
- Chris Pacia
- Dimitris Apostolou
- Jonathan Toomim
- Greg Griffith
- Peter Tschipper
- Søren Bredlund Caspersen

We have backported an amount of changes from other projects, namely Bitcoin Core and Bitcoin SV.

Following all the indirect contributors whose work has been imported via the above backports:

- Cory Fields
- Don Patterson
- fanquake
- Jonas Schnelli
- shaunOK
