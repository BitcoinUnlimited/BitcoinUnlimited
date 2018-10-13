Release Notes for Bitcoin Unlimited Cash Edition 1.5.0.0
=========================================================

Bitcoin Unlimited Cash Edition version 1.5.0.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a major release version based of Bitcoin Unlimited compatible
with the Bitcoin Cash specifications you could find here:


https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/uahf-technical-spec.md (Aug 1st '17 Protocol Upgrade, bucash 1.1.0.0)
https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17 Protocol Upgrade, bucash 1.1.2.0)
https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/may-2018-hardfork.md (May 15th '18 Protocol Upgrade, bucash 1.3.0.0, 1.3.0.1, 1.4.0.0)
https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18 Protocol Upgradem, bucash 1.5.0.0)

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

If you are upgrading from a release older than 1.1.2.0, your UTXO database will be converted
to a new format. This step could take a variable amount of time that will depend
on the performance of the hardware you are using.

Other than that upgrading from a version lower than 1.3.0.0 your client is probably stuck
on a minority chain and need some manual intervention to make so that it follow the majority
chain after the upgrade. For more detail please look at `reconsidermostworkchain` RPC commands.

Downgrade
---------

In case you decide to downgrade from BUcash 1.2.0.1, or greater, to a version older than 1.1.2.0
will need to run the old release using `-reindex` option so that the
UTXO will be rebuild using the previous format. Mind you that downgrading to version
lower than 1.1.2.0 you will be split from the rest of the network that are following
the rules activated Nov 13th 2017, May 15th 2018 and Nov 15th 2018 protocol upgrades.

Main Changes
------------

- Implementation of November 2018 upgrades feature (see the [specification](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md) for more details)
- CTOR, CDSV, CLEAN_STACK, FORCE_PUSH, 100 byte TXN SIZE (as a sublist)
- Add configuration parameters to allow miners to specify their [BIP135](https://github.com/bitcoin/bips/blob/master/bip-0135.mediawiki) votes. See this [guide](https://github.com/BitcoinUnlimited/BitcoinUnlimited/blob/master/doc/bip135-guide.md) from more details
- Multithreaded transaction admission to the mempool (ATMP)
- Parallelize message processing
- Fastfilters: a faster than Bloom Filter probabilistic data structure
- Various improvements to the Request Manager
- Add tracking of ancestor packages and expose ancestor/descendant information over RPC
- Remove trickle login in dealing with transactions INV
- Implement shared lock semantics for the UTXO

Commit details
-------


- `0d2e889` Use the snapshot when doing CheckFinalTx() in txadmission.cpp (Peter Tschipper)
- `109f376` Wait for 3 seconds after invalidateblock() before checking for assertions (Andrea Suisani)
- `e703c88` Bump version to 1.5.0.0 (Andrea Suisani)
- `17d235a` add configuration parameter to allow miners to specify their bip135 vote (#1401) (Andrew Stone)
- `42073a7` Cosmetics (Awemany)
- `c4eb3ae` Use a single round of SHA256 on the CHECKDATASIG message (Amaury Séchet)
- `7cb6221` Hash the input of OP_CHECKDATASIG instead of expecting the user to do it. (Amaury Séchet)
- `8d9e114` Fix checkdatasig-activation.py for BU (Awemany)
- `5a8c9c8` Extend test_framework/script.py for CDS/-V (Awemany)
- `ee2d9d5` Activation test code for OP_CHECKDATASIG (Amaury Séchet)
- `448d1fb` Activation code for CHECKDATASIG/-VERIFY (Awemany)
- `0cf2a6f` Implement OP_CHECKDATASIG and OP_CHECKDATASIGVERIFY (Amaury Séchet)
- `b604ede` Count OP_CHECKDATASIG and OP_CHECKDATASIGVERIFY as sigops when the proper flag is set. (Amaury Séchet)
- `2a1ec7d` Pass flags down to GetP2SHSigOpCount and GetSigOpCountWithoutP2SH (Amaury Séchet)
- `fe2c7d4` Add test case for FormatScript, fix a bug with recent opcodes not being formated. (Amaury Séchet)
- `c85f7b0` Refactor script_tests.cpp to separate signature generation from PushSig (Amaury Séchet)
- `de0dc1d` Make sure CScript::GetSigOpCount(CScript &) is tested when called on non P2SH scripts. (Amaury Séchet)
- `2e09053` Update sigopcount_tests.cpp to recent ABC version (Amaury Séchet)
- `e54b71c` Implementation for InsecureRand256() (Awemany)
- `95e0976` Add CheckDataSignatureEncoding to check signature with a sighashtype. (Amaury Séchet)
- `57ba5a8` Replace exhaustive flags and hash type checking with randomization (Awemany)
- `2fdd0c5` Import Bitcoin ABC's reworked signature encoding check function (Amaury Séchet)
- `f9f3b6b` Add test case for CheckSignatureEncoding for proper DER encoding (Amaury Séchet)
- `b540dd6` Add test for CheckSignatureEncoding to verify low S is checked properly. (Amaury Séchet)
- `5936c8d` Check error code return by CheckPubKeyEncoding in tests (Amaury Séchet)
- `fad30df` Add test cases for CheckSignatureEncoding (Amaury Séchet)
- `4148a2a` Import signature hash type declarations from interpreter.h (Awemany)
- `d3ccc90` Port of Bitcoin ABC's sighashtype.h file (Jason B. Cox)
- `0cf83d7` Add test for CheckPubKeyEncoding (Amaury Séchet)
- `e0bacdd` Decouple non compressed pubkey from segwit (Amaury SECHET)
- `0d44f7a` Import IsCompressedKey from Bitcoin Core (Johnson Lau)
- `cbf56ab` Check GetOpName(..) for completeness (Awemany)
- `2089e90` Add a flag to activate OP_CHECKDATASIG (Amaury Séchet)
- `b83b2ab` Add CHECKDATASIG opcode. (Amaury Séchet)
- `8f1e290` Remove OP_DATASIGVERIFY as it will be replaced (Awemany)
- `21406f4` Store transaction size within the transaction rather than using GetSerializeSize() each time. (#1398) (Peter Tschipper)
- `8ea289f` Moved DNS Max IPs constant to net.h and other cleanups. (#1399) (Angel Leon)
- `be4ac65` [Minor] break from loop instead of continue in txadmission (#1394) (Peter Tschipper)
- `52efe56` DRY refactor on dbwrapper Read (#1385) (Angel Leon)
- `d2f9d8d` define version bits for Nov features (#1290) (Andrew Stone)
- `f535938` parallel mining and chain rewind fixes (#1397) (Andrew Stone)
- `e698b5c` Change a few reject codes and add check for IsNov152018Scheduled() (Peter Tschipper)
- `f9082f5` Fix activation test by using conensus.forkNov152018 (Peter Tschipper)
- `9b97575` Add handling for IsNov152018Sheduled() (Peter Tschipper)
- `64a00f7` Remove shadowing of block variable in nov152018_forkactivation.py (Peter Tschipper)
- `5afbb5e` Pad the coinbase to be at least 100 bytes only after fork activation. (Peter Tschipper)
- `50ed276` [qa] fix flaky abc-magnetic-anomaly-activation.py (Shammah Chancellor)
- `f94c749` Fork activation test update (Peter Tschipper)
- `3a6664d` Add cleanstack check when activating November, 15 2018 upgrade (Amaury Séchet)
- `dde8ac1` Make push only mandatory when magnetic anomaly activates. (Amaury Séchet)
- `5080079` Impose Mimimum transaction size of 100 bytes after Nov15,2018 HF (Peter Tschipper)
- `a40e8c7` clean up UtilMkBlockTmplVersionBits by removing a few params (Andrew Stone)
- `6e67168` Print configuration to debug.log (Awemany)
- `6986516` Process potential double spends as quickly as possible. (#1387) (Peter Tschipper)
- `6294cf8` implement little endian ascending transaction ordering using a sequential algorithm (CTOR) (Andrew Stone)
- `5143f87` Remove old wallet_ismine.cpp (Awemany)
- `dd9833e` Add one second wait time in notify.py (Peter Tschipper)
- `6af31f6` Add READ/WRITE locks to orphanpool handling (Peter Tschipper)
- `0220c84` Add src/blockrelay files to .formatted-files (#1378) (Peter Tschipper)
- `6e6938d` Clarify forks.cpp. Use pindexTip rather than pindexPrev. (#1388) (Peter Tschipper)
- `e1daa07` Limit the number of IPs we use from each DNS seeder (e0)
- `8d80349` Use NET debug category to log connect() and getsockopt() errors (Andrea Suisani)
- `992dff5` Remove check for fork and sighash when missing inputs (#1379) (Peter Tschipper)
- `2ac746c` Handle n values > 255 (#1371) (Peter Tschipper)
- `13e0513` Turn on graphene by default. (Peter Tschipper)
- `16dc98b` Encapsulate nodestate (Peter Tschipper)
- `0269732` Make boost::multi_index comparators const (take #2) (#1375) (Andrea Suisani)
- `c714844` remove register keyword from fast filter (#1374) (Greg Griffith)
- `8e825c4` [UI] Expand services displayed in the debug dialog peer details. (#1339) (Justaphf)
- `241d62d` Fastfilter enhancements (#1353) (Andrew Stone)
- `55be41e` check the commit queue in TxAlreadyHave so we don't request transactions that we've already processed.  Fix tx whose inputs have bloom filter false positives with themselves by pushing the first transaction on the defer queue directly to the inqueue without checking the empty filter.  Make tx admission threads handle a minimum of 200 tx (if available) before the commit thread can take back over. (#1369) (Andrew Stone)
- `e8e62f4` A couple of small fixes (#1370) (Peter Tschipper)
- `cb12f3b` refactor code out of main.cpp (Greg Griffith)
- `fb81709` txadmission.cpp: Fix a log message type (Awemany)
- `8f6819e` Change the way we access node state from reference to pointer (Andrea Suisani)
- `35a6d57` Add missing override attribute to _GetBestBlock() (Andrea Suisani)
- `af59f34` Return with "unknown" peer if no peer supplied (Peter Tschipper)
- `7b1b26a` Change where we mark the txn as received. (Peter Tschipper)
- `1c63745` November 2018 activation code (#1331) (Andrea Suisani)
- `d3f6503` Ensure that the bloom filter uses at least 1 hash function (Andrew Stone)
- `b7a8b8b` Eliminate duplicate transactions from the deferQ (Peter Tschipper)
- `9bf9550` [PORT] qa test changes from bitcoin/bitcoin#14249 for CVE-2018-17144 (Suhas Daftuar)
- `9a92726` Update the transaction response time (Peter Tschipper)
- `d74bec5` Prevent the re-requesting of txns (Peter Tschipper)
- `12c080a` Emit a warning when parsing disabled parameters rather than raise an error. (Andrea Suisani)
- `c155bcf` Fix a variety of issues related to the QA test suite and multithreading: (Andrew Stone)
- `91b6032` rpc-test.py: Fix a potential race (Awemany)
- `a02dbf9` Clean up comments and locking in CommitTxToMempool (Peter Tschipper)
- `a806288` When processing getdata make sure to check the commitQ (Peter Tschipper)
- `cefd7e6` On regest do not DOS if too many xblocktx or graphenblocktx requests are made (Peter Tschipper)
- `2838aed` Better handling for rejected txns in ThreadTxAdmission() (Peter Tschipper)
- `ddcde72` When reconstructing a block, check the txCommitQ if necessary (Peter Tschipper)
- `092e9d2` Add any txn hashes from the txCommitQ to the xthin bloom filter. (Peter Tschipper)
- `5f7d3d0` Add txCommitQ size to GetGrapheneMemepoolInfo (Peter Tschipper)
- `c3d4acc` Set RPC warmup to finished at the very end of init. (Peter Tschipper)
- `4120268` Initialize nThreads in parallel.cpp (Peter Tschipper)
- `59b0666` Add a sync_blocks() in walletbackup.py (Peter Tschipper)
- `6ab30f3` format the new files (Andrew Stone)
- `ec4d7b2` Fix transactions per second (Peter Tschipper)
- `60a92e2` multithreaded transaction admission by making the mempool read-only during parallel transaction processing, and then locking tx processing and committing the changes to the mempool (Andrew Stone)
- `e160642` Add requesting a block by hash to requestmanager (Greg Griffith)
- `a36cb85` Add "respend" and "weakblocks" to the list of debug categories in the helper message (Andrea Suisani)
- `551a75e` [Tests] Check output of parent/child tx list from getrawmempool, getmempooldescendants, getmempoolancestors, and REST interface (Conor Scott)
- `ca4b495` [RPC] Add list of child transactions to verbose output of getrawmempool (Conor Scott)
- `12c8734` Add test coverage for new RPC calls (Suhas Daftuar)
- `d45bbd8` Add ancestor statistics to mempool entry RPC output (Suhas Daftuar)
- `f5836b5` Add getmempoolentry RPC call (Suhas Daftuar)
- `a834608` Add getmempooldescendants RPC call (Suhas Daftuar)
- `f30b38b` Add getmempoolancestors RPC call (Suhas Daftuar)
- `5cf283d` Refactor logic for converting mempool entries to JSON (Suhas Daftuar)
- `30e843d` fix format (Peter Tschipper)
- `c3aa8cf` Check all ancestor state in CTxMemPool::check() (Suhas Daftuar)
- `132406e` Add ancestor feerate index to mempool (Suhas Daftuar)
- `f599d72` Add ancestor tracking to mempool (Suhas Daftuar)
- `2c2d097` Remove work limit in UpdateForDescendants() (Suhas Daftuar)
- `9d5e638` Rename CTxMemPool::remove -> removeRecursive (Suhas Daftuar)
- `5dd4f94` CTxMemPool::removeForBlock now uses RemoveStaged (Suhas Daftuar)
- `9ec79ae` Cleanup and simplify the initialization of the parallel block validator. (Peter Tschipper)
- `d20d7ba` changes to aid in testing and debugging: in QA if --tmpdir is used, then delete old data there.  Also add a few convenience functions to some objects.  Provide a tweak to not disconnect if timeouts triggered (if attached via gdb, the process may not respond to timeouts).  Provide an API call DbgPause() that only is valid in --enable-debug mode that pauses the current thread in a manner that can be resumed once attached via gdb.  Rename all threads from bitcoin-XXXX to just XXXX because the thread names get cut off and the bitcoin- prefix is not meaningful. (Andrew Stone)
- `cf8fffd` Add blockstorage/dbabstract.h to src/Makefile.am (Andrea Suisani)
- `8e1f14e` test_framework/script.py: Support for pretty-printing (Awemany)
- `cd96b92` Remove bolierplate introduction and use less generic class name (Andrea Suisani)
- `26c425f` Remove trailing spaces from p2p-fullblocktest.py (Andrea Suisani)
- `ffc2f97` Go lock free for tracking bytes sent and received. (Peter Tschipper)
- `d4ce459` Setup the number of p2p message processing threads in init.cpp (Peter Tschipper)
- `5ade9f4` Add tests. (Peter Tschipper)
- `2edad27` Remove the restriction of minrelaytxfee when mining a new block (Peter Tschipper)
- `14a79cd` Switch to atomic<uint64_t> for nSendSize in CNode (Peter Tschipper)
- `8f4eac1` fix PR comments and spurious trigger of the deadlock detector (Andrew Stone)
- `7888f07` QA test clean ups (some due to simplified CTransaction handling) (Awemany)
- `0ea8737` nodemessages.CTransaction improvements (Awemany)
- `7d76b44` Use relative import for cashlib (Awemany)
- `550153d` parallelize message processing. PING/PONG can be used as serialization points changes to enable nolnet testing, and enable 1 minute nolnet blocks (we can change this back after some testing if desired) (Andrew Stone)
- `bd5668e` Remove non existent configuration parameters from May '18 activation test (Andrea Suisani)
- `3428eca` Allow for the downloading of initial headers from one pruned node. (Peter Tschipper)
- `d202c9c` Use FPR_FILTER_MAX when checking for size of filter rather than 1.0 (#1315) (Peter Tschipper)
- `eac6313` If bitcoind binary is not found (or fails to start up for some other reason), the code does not work as expected (it does not print the error, and doesn't ignore failures in the argument to  since the problem happens during args eval.  This restores the expected functionality (#1314) (Andrew Stone)
- `86890cd` UI - Add running "time since last block" counter to debug UI (#1304) (Justaphf)
- `96fd040` UI - Adjust word wrap for peer details (#1303) (Justaphf)
- `d3aef7c` Fix bitcoin-tx cashaddr parsing (#1310) (awemany)
- `4c2c72a` trivial fixes to validateblocktemplate (#1312) (Andrew Stone)
- `d965bf8` Remove check for is UAHF active in signrawtransaction() (#1301) (Peter Tschipper)
- `fc932a5` Add rpc feature: reconsidermostworkchain (#1291) (Peter Tschipper)
- `12045b2` Remove trickle logic (#1299) (Peter Tschipper)
- `5266210` do not create qa cache directory until the cache is complete (#1308) (Andrew Stone)
- `5d12f6a` split mkblocktemplate() into 3 functions. The two new ones MkFullMiningCandidateJson() UtilMkBlockTmplVersionBits() are used to make either a CBlock or JSON. (Not both as before) (lonoami)
- `af895e9` fix bug introduced in #1296 (#1306) (Peter Tschipper)
- `85288e2` Tidy up basic xthinblock checking and use atomics for tracking requests (#1296) (Peter Tschipper)
- `a03d9f2` move fastfilter from gigaperf, add unit tests (#1300) (Andrew Stone)
- `e696997` Do not show Upgrade message in UI when not upgrading (#1298) (Peter Tschipper)
- `00a2337` Make better use of peers tab real estate (#1297) (Peter Tschipper)
- `2dff793` Only print out one log entry when checking the graphene/thinblock timers (#1295) (Peter Tschipper)
- `1e4f5a6` Fix URL of protocol upgrade specifications (#1277) (#1282) (Andrea Suisani)
- `2385b04` fix issue with recursively locking a non recursive lock (#1293) (Greg Griffith)
- `b71bb3b` Consolidate xthin/graphene code into  src/blockrelay (#1287) (Peter Tschipper)
- `486d4f8` Extend CTransactionRef into mempool lookups (#1281) (Peter Tschipper)
- `960dffe` add dbabstract.h to .formatted-files (Greg Griffith)
- `13c1f86` DetermineStorageSync now checks all possible modes, pblocktree other creation is now as needed for a specific mode after we determine if we need it to sync from another db type (Greg Griffith)
- `a9f9dcc` update usage of bestblock funcs, check for pblockdb instead of db mode (Greg Griffith)
- `dba808e` GetBestBlock and WriteBestBlock now interact with current db mode, and have overloads to access the variables for other storage modes. (Greg Griffith)
- `52a38b0` move BlockDBMode enum, rename DB_BLOCK_STORAGE (Greg Griffith)
- `a2e1996` Change CBlockDB to CBlockLevelDB and derive it from CDatabaseAbstract (Greg Griffith)
- `d5cb607` global pointer pblockdb now type CDatabaseAbstract, remove pblockundodb (Greg Griffith)
- `6721e9e` implement database abstract class (Greg Griffith)
- `3f0f6de` added tests to miningtest.py (lonoami)
- `f43b05d` Trivial - clean up network RPC help messages (#1285) (Justaphf)
- `5e39be7` (port from giga_perf) Implement shared lock semantics for the UTXO (#1283) (Andrew Stone)
- `3baa9d1` Fix 2 shadowing warnings in bloom.cpp (Andrea Suisani)
- `9a770ce` Use pindexStart->nHeight when setting first headers expected height. (Peter Tschipper)
- `35b0fe2` Take out old code preventing GETHEADERS requests when in IBD (Peter Tschipper)
- `70f74b6` bitcoinsize now uses correct function calls (lonoami)
- `b4064e1` Extend CTransactionRef into the double spend relay code (#1279) (Peter Tschipper)
- `ddee772` Add the size of the orphan pool to the total mempool size (#1280) (Peter Tschipper)
- `eda4bec` Benchmark rolling bloom filter (Pieter Wuille)
- `af42b60` More efficient bitsliced rolling Bloom filter (Pieter Wuille)
- `9596ec5` Switch to a more efficient rolling Bloom filter (Pieter Wuille)
- `466b10a` Travis tweaks (#1250) (awemany)
- `b5e7a63` Weakblocks landgrab 😀 (#1262) (awemany)
- `adfd46a` Use CTransactionRef in maprelay (#1274) (Peter Tschipper)
- `4aeb1ae` Added functionality for a miner to request a specific size coinbase tx. It should only be used when a miner replaces the content. (It is just a pile of bytes - not properly formatted) (lonoami)
- `f94d2f0` Add LOCK(cs_utxo) to the remaining CCoinsViewDB methods (Peter Tschipper)
- `bfb1a41` Fix typos (Dimitris Apostolou)
- `f43a86d` Add BUcash 1.4.0.0 release notes to doc/release-notes folder (Andrea Suisani)
- `4ba0bbf` Further elaborate on stand alone cpu miner (Andrea Suisani)
- `353d382` Typo fix, explain further cashlib, add graphene activation param (Andrea Suisani)
- `f54148a` Both graphene and blocksdb are experimental and turned off by default (Andrea Suisani)
- `33c94dd` Add release notes for BUcash 1.4.0.0 (Andrea Suisani)
- `7f6c4a5` Change .travis.yml to compile with 2 cores instead of 3 (Peter Tschipper)
- `32bdf1c` util: Remove designator initializer from ScheduleBatchPriority (Wladimir J. van der Laan)
- `52ce97e` util: Pass pthread_self() to pthread_setschedparam instead of 0 (Wladimir J. van der Laan)
- `f4a5419` Use std::thread::hardware_concurrency, instead of Boost, to determine available cores (fanquake)
- `22c5026` Set SCHED_BATCH priority on the loadblk thread. (Evan Klitzke)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Angel Leon
- Awemany
- Dimitris Apostolou
- Greg Griffith
- Justaphf
- Peter Tschipper
- lonoami

We have backported an amount of changes from other projects, namely Bitcoin Core and Bitcoin ABC.

Following all the indirect contributors whose work has been imported via the above backports:

- Amaury Séchet
- Conor Scott
- Evan Klitzke
- Pieter Wuille
- Shammah Chancellor
- Suhas Daftuar
- Wladimir J. van der Laan
- Jason B. Cox
- e0
- fanquake
