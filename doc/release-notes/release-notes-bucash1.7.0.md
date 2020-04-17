Release Notes for Bitcoin Unlimited Cash Edition 1.7.0
======================================================

Bitcoin Unlimited Cash Edition version 1.7.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a major release of Bitcoin Unlimited compatible with the upcoming protocol upgrade of the Bitcoin Cash network. You could find
November 15th, 2019 upgrade specifications here:

- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2019-11-15-upgrade.md (Nov 15th '19 Protocol Upgrade, bucash 1.7.0)

The following is a list of the previuos network upgrades specifications:

- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/uahf-technical-spec.md (Aug 1st '17, ver 1.1.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17, ver 1.1.2.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/may-2018-hardfork.md (May 15th '18, ver 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18, ver 1.5.0.0, 1.5.0.1, 1.5.0.2, 1.5.1.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2019-05-15-upgrade.md (May 15th '19, ver 1.6.0.0)

Upgrading
---------

If you are running your client with `-txindex` (`txindex=1), the first session
after upgrading to 1.7.0 the transactions index database will be migrated to a new
format. This process could take quite a while especially if you are storing your blockchain
data on a rotative hard disk (up to one hour). During the migration bitcoind won't respond
to RPC calls.

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

Upgrading from a version lower than 1.6.0.0 would probably lead your client to be stuck
on a minority chain and need some manual intervention to make so that it follow the majority
chain after the upgrade. For more detail please look at `reconsidermostworkchain` RPC commands.

Main Changes in 1.7.0
---------------------

This is list of the main changes that have been merged in this release:

- [Mempool synchronization via Graphene](#mempool-synchronization-via-graphene) primitives
- Intelligent unconfirmed transaction forwarding
- Child-Pay-For-Parent implementation based on [Ancestor Grouped Transactions](#new-cpfp-and-long-chains-of-unconfirmed-transactions) (AGT)
- Graphene ver 2.1 and IBLT specifications
- New dead-lock detection mechanism
- New getblockstats rpc call
- ElectrsCash v1.0 ([release notes](https://github.com/BitcoinUnlimited/ElectrsCash/blob/master/RELEASE-NOTES.md#100-18-september-2019))
- QA improvements
- Schnorr multisignature (Nov 15th' 2019 upgrade)
- Enforce minimal data push at the consensus layer (Nov 15th' 2019 upgrade).
- Transaction index database improvements
- Add transaction rate trend graph in Qt debug dialog

Features Details
----------------

### New CPFP and Long Chains of Unconfirmed Transactions

The alternative way of dealing with chain of unconfirmed transaction [implemented](https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/1903) in BU make it so that we could introduced a bump-up fee mechanism known as ["Child Pay For Parent"](https://bitcoin.org/en/glossary/cpfp) (CPFP). BU implementation is based on a new concept that show an enormous performance improvement. This is accomplished by considering a group of ancestors as a single transaction. We can call these transactions, Ancestor Grouped Transactions (AGT). This approach to grouping allows us to process packages orders of magnitude faster than other methods of package mining since we no longer have to continuously update the descendant state as we mine part of an unconfirmed chain.

Attached a plot showing transactions selection time versus the maximum length of chains of unconfirmed transaction permitted for the Bitcoin Unlimited implementation of CPFP and Bitcoin Core's.

![image](https://user-images.githubusercontent.com/12862928/65771848-f7c0f600-e0ed-11e9-8fe3-ff1be31d9f41.png)

This mean that BU could increase the maximum length of chains of unconfirmed transaction by 2 orders of magnitude with no performance regression at all.

Increasing unilaterally such parameter is not something a single implementation, especially if not the most used, could do without some care. The problem is that this parameter is something we call "quasi-consensus" or in other words: there exists network wide configuration values that, if inconsistent, cause undesirable network behavior (but do not cause a fork).

An example of this undesirable is increasing the chance of success of double spend attacks. This is due to the fact that if BU accept, let's say, chain as long as 50 transactions whereas all the rest of the network set the limit to 25. To mitigate this side effect we implemented [Intelligent unconfirmed transaction forwarding](https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/1937), i.e. when a block comes in that confirms enough parent transactions to make the transaction valid in non-BU mempools, a double-spender is essentially racing the entire BU network to push his double-spend into the miner nodes that now accept the transaction.

Intelligent unconfirmed transaction forwarding is delivered as an "experimental" (off by default) feature.  To enable this feature, an operator would add configuration into bitcoin.conf.  Set both new unconfirmed limits (these config fields have existed for a long time) and turn on the new intelligent unconfirmed transaction forwarding:

- limitancestorsize = "KB of RAM"
- limitdescendantsize = "KB of RAM"
- limitancestorcount = "number of allowed ancestors"
- limitdescendantcount = "number of allowed ancestors"
- net.unconfChainResendAction = 2

### Mempool synchronization via Graphene

[Implementation](https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/1879) of mempool synchronization using Graphene set reconciliation primitives. The overall approach is for peers to pull synchronization objects from other peers at regular intervals (currently once every 30 seconds).  Each interval, the requesting peer randomly chooses a peer from whom to request synchronization. When a responding peer receives a mempool sync request, he makes a best effort to send to the requester all transactions in his mempool that are missing from the requester's mempool. Synchronization is not necessarily symmetric: peer A can sync with the mempool of B, but B need not necessarily sync with A.

The feature is marked as experimental and turned off by default. One could turn off/off this synchronization method at run time, the same applies to the sync interval length. Peers selections is based on xVersion, in fact this is the mechanism that node use to signal their ability to support Graphene mempool sync, among other things.

A test has been performed to evaluate the efficiency of the sync mechanism. Basically node `A` (internal) is connected to the main net BCH only via a proxy node `B` (edge). The only method of exchanging transactions between `A` and `B` is via Graphene mempool synchronization. We let the nodes run for 24 hours, the following is the plot of the mempool transactions counts over the test period. The lines are nearly identical, indicating close synchronization between the two mempools.

![image](https://user-images.githubusercontent.com/1005112/65324924-8926ea00-db7b-11e9-90b1-e3577563c684.png)

### ElectrsCash

Consider making your ElectrsCash electrum server public to support the network light client infrastructure.  To do so, make your server public, by setting `electrum.host=0.0.0.0` in your bitcoin.conf file and opening a hole at port 50001 in your firewall (override the port via `electrum.port=12345`).  To support Electron Cash wallets, also [enable SSL support](https://github.com/BitcoinUnlimited/ElectrsCash/blob/master/doc/usage.md).

Commit details
--------------

- `5a45ed562` remove old bip135 votes (#1965) (Andrew Stone)
- `b4a7ddd28` [doc] Update dependencies.md (#1957) (Andrea Suisani)
- `30f11b6eb` Ignore the state of the queues when disconnecting a node (#1964) (Peter Tschipper)
- `acc6b9431` Adapt segwit recovery QA test to work with BU (#1963) (Andrea Suisani)
- `129080be2` Move the cleanup of the mempool sync maps to FinalizeNode() (Peter Tschipper)
- `a93995137` Don't request a mempool sync unless our chain is nearly synced (Peter Tschipper)
- `28737a558` basic implementation of mempool sync (George Bissias)
- `3450aed1c` Set lexicographical transaction ordering on by default (#1958) (Andrea Suisani)
- `3e52b654d` small fixes for QA stability when run 100s of times (Andrew Stone)
- `2d541475e` Add some range protections to tx rate graph widget (Justaphf)
- `bf23d5273` Add transaction rate graph to Qt debug UI (Justaphf)
- `de0d6b4e7` Refactor to return all txn rate stats atomically as a group (Justaphf)
- `847af6923` Expose some local variables in the update txn rate method (Justaphf)
- `40407b341` implementation of a transaction forwarding algorithm that is aware of peer mempool policy and pushes transactions to nodes only when they will be accepted by the peer. (Andrew Stone)
- `0d95893c3` gitignore Visual Studio .vs directory (Justaphf)
- `b171c401f` Remove assert for sync_with_ping() (Peter Tschipper)
- `43abc96bd` Also add waitFor in send_txs_and_test (Peter Tschipper)
- `ebcba2f63` Add waitfor() statements to schnorrmultisig-activation.py (Peter Tschipper)
- `c23078285` temporarily disable live dld and tests (Greg-Griffith)
- `8ad0c8a8e` misc fixes for android compiler of problems caused by recent commits (Andrew Stone)
- `927418c36` Erase the entire range at once rather than looping through the range (Peter Tschipper)
- `3d62ee8a4` Turn mapBlocksToReconstruct into a multi-map (Peter Tschipper)
- `e6edf59d0` Fix lock guard definition in lockorder.h (Peter Tschipper)
- `77743b939` [qa] BU adaptions to abc-schnorrmultisig-activation (Dagur Valberg Johannsson)
- `f45e616f9` Cleanup reject_code in abc-schnorrmultisig-activation (Jason B. Cox)
- `6d860e80a` [qa] Test for new Schnorr multisig activation (Mark Lundeberg)
- `ff8c8578d` better error message for mandatory-flag tx rejections (Mark Lundeberg)
- `56df68863` Add unit tests for CheckMinimalPush() (Andrea Suisani)
- `68025fae3` Misc small changes (#1948) (dagurval)
- `4bfc005d1` Make test framework produce lowS signatures (Johnson Lau)
- `4cd5598f9` for compatibility with unit tests, create a global boolean variable that indicates when lockdata is destructed (Andrew Stone)
- `fb009acc7` logical not the check, because assert/dbgassert triggers when the boolean FAILS (Andrew Stone)
- `8425eaac9` [qa] BU adaptions for minimaldata test (Dagur Valberg Johannsson)
- `6768995cf` MINIMALDATA consensus activation (Mark Lundeberg)
- `3a03419fa` Add activation code for Schnorr mutlisignature (Andrea Suisani)
- `42af12eb8` Fix warning related to the wrong use of static_assert in interpreter.cpp (Andrea Suisani)
- `4f96a038a` make CheckMinimalPush available to codebase (Mark Lundeberg)
- `c1d069b0f` [rpc] Improve error on method not found (Dagur Valberg Johannsson)
- `7e95a067b` [qa] Better assertion on unexpected rpc error code (Dagur Valberg Johannsson)
- `ba9e8fb18` [qa] allow passing headers to msg_headers (Dagur Valberg Johannsson)
- `fb0dedafc` [qa] Add P2PDataStore (Dagur Valberg Johannsson)
- `61f0e8a81` [qa] Add assert_debug_log (Dagur Valberg Johannsson)
- `40e33b3bd` [qa] add assert on missing connection (Dagur Valberg Johannsson)
- `de11663ca` [qa] add optional hex encoding to gethash (Dagur Valberg Johannsson)
- `00631c988` [qa] allow custom scriptPubKey in create_coinbase (Dagur Valberg Johannsson)
- `78031aa8e` [qa] Add function 'make_conform_to_ctor' (Dagur Valberg Johannsson)
- `7873040d2` Allow log to be both output to stdout and file (Dagur Valberg Johannsson)
- `e61e21c9c` reword trylock comment, make it clear when these added locks are removed (Greg-Griffith)
- `a14cc18f1` remove the boolean available data member from LockData struct (Greg-Griffith)
- `576dd3873` cleanup deadlock errors for clarity and capitalization. (Greg-Griffith)
- `30b49fa90` CLockOrderTracker data member names now all start with lowercase letters (Greg-Griffith)
- `fcc5f6eb8` add missing lock on critical section for method CanCheckForConflicts (Greg-Griffith)
- `a91b6a41a` make class variables protected by default, not private (Greg-Griffith)
- `a7d24b798` Assert CPubKey::ValidLength to the pubkey's header-relevent size (Ben Woosley)
- `8a36bb374` move lock before count to solve potential segfault issue where map can be read and written to at the same time (Greg-Griffith)
- `4a744fb1c` Use ptrdiff_t type to more precisely indicate usage and avoid compiler warnings (murrayn)
- `f873a5bee` Scope the ECDSA constant sizes to CPubKey / CKey classes (Jack Grigg)
- `8b3d00340` Ensure that ECDSA constant sizes are correctly-sized (Jack Grigg)
- `f47f552bb` emove redundant `= 0` initialisations (Jack Grigg)
- `a991554ca` Specify ECDSA constant sizes as constants (Jack Grigg)
- `79bdf5749` Fix potential overflows in ECDSA DER parsers (Jack Grigg)
- `5aa32d5bb` rpc: faster getblockstats using BlockUndo data (Felix Weis)
- `046101dc7` Add bitfield to cashlib since its required in the interpreter. Add addtl tests and cleanup. (Andrew Stone)
- `e2ab6c82e` Implement new checkmultisig trigger logic and execution logic. Cherry-pick from ABC and merged. Summary: See specification at https://github.com/bitcoincashorg/bitcoincash.org/pull/375 Differential Revision: https://reviews.bitcoinabc.org/D3474 Also include elements of: D3472 and D3265 (Mark Lundeberg)
- `c0fca63f2` add missing DEBUG_LOCKORDER defines (Greg-Griffith)
- `3fa3166ee` Update gitian descriptor yaml files to use 1.7 version (Andrea Suisani)
- `659e12d13` Bump version to 1.7.0 (Andrea Suisani)
- `65ce32562` clear the lock order tracking at the end of the deadlock tests (Greg-Griffith)
- `95d3c2a1b` add a test for lock order checking using two separate threads (Greg-Griffith)
- `4790ca03c` temporarily disable test 9 until feature is implemented (Greg-Griffith)
- `20ff1475d` rebase fixes (Greg-Griffith)
- `0b059f61e` update makefile and fix formatting (Greg-Griffith)
- `ed0d30356` move lock ordering to its own file, track more information (Greg-Griffith)
- `9e62d0c7d` refactor locklocation into its own file (Greg-Griffith)
- `7639d543b` remove unused headers (Greg-Griffith)
- `9aed989bf` move deadlock detection to its own folder (Greg-Griffith)
- `855c83eed` only check lock ordering when we are not locking recursively (Greg-Griffith)
- `14d944b37` disallow naming critical sections "cs" for easier debugging (Greg-Griffith)
- `6c79de3f1` make GetTid function inline (Greg-Griffith)
- `40cd4db05` add basic test for lock order tracking (Greg-Griffith)
- `0fbbb87e4` add tracking for lock ordering for non try locks (Greg-Griffith)
- `ab3579a5d` add typedef lost during rebase (Greg-Griffith)
- `461b22b86` add some doxygen comments to functions declared in header (Greg-Griffith)
- `556cee564` reorganize function order and remove HasAnyOwners from header (Greg-Griffith)
- `e7e806500` remove second declaration of LocksHeld (Greg-Griffith)
- `05611a7f0` dont call SetWaitingToHeld for try locks, they should never be waiting (Greg-Griffith)
- `249b06f37` add some debug asserts for missing waiting locks (Greg-Griffith)
- `9d5211b46` fix issue where a try lock would be considered waiting, try never waits (Greg-Griffith)
- `744caf388` add held locks even if waiting locks do not exist for some reason (Greg-Griffith)
- `7c30f27cc` skip deadlock detection when recursively locking (Greg-Griffith)
- `d78f854eb` do not erase locks from held map that we have recursively locked them and have more than 0 remaining locked (Greg-Griffith)
- `a8f925e56` store the locktype in the locklocation struct, add getter for it (Greg-Griffith)
- `ca6c8b39f` fix issue with try locks not being shown in heldlocks maps (Greg-Griffith)
- `dd8b5ceb6` add missing lock in SetWaitingToHeld (commit lost in rebase) (Greg-Griffith)
- `17d2b06ae` clean up some syntax nitpicks, fix formatting (Greg-Griffith)
- `218e972a7` replace the now removed CSharedUnlocker with manual locking/unlocking (Greg-Griffith)
- `af30ca05e` debug assert if we attempt to push_lock for an unsupported mutex type (Greg-Griffith)
- `6949d7dca` change tests to rely on counters instead of sleeping/timing (Greg-Griffith)
- `13909b8d7` remove functions from emptysuite so it is now empty (Greg-Griffith)
- `01efe73c5` fix incorrect logic, a thread with only readlocks can deadlock (Greg-Griffith)
- `553b7e796` initialize bitbuf_size to 0 to avoid filling buffer when not needed (Greg-Griffith)
- `7d0685a9b` remove boost include that is no longer needed (Greg-Griffith)
- `bafc53aff` change isExclusive bool to an enum, remove extra function header decs (Greg-Griffith)
- `4b6a23847` remove CSharedUnlocker (Greg-Griffith)
- `80ca0efe1` add defines to deadlock tests so they only run when debugging lockorder (Greg-Griffith)
- `e71fea886` add missing fastrandomcontext initialization (Greg-Griffith)
- `fc4e8a335` CSharedUnlocker now uses the lockstack when DEBUG_LOCKORDER is defined (Greg-Griffith)
- `83dadb9e4` add clarification comment about a continue in deadlock detection logic (Greg-Griffith)
- `4b9092d8e` add comments explaining the lock ordering being tested in each test (Greg-Griffith)
- `8ff63d609` add new lockstack delcaration to globals and utility binaries (Greg-Griffith)
- `f9779a262` hackfix to get around segfault when dealing with old openssl mutex array (Greg-Griffith)
- `c5f6aa639` add tests for new deadlock detection system (Greg-Griffith)
- `2878e6516` implement new deadlock detection system (Greg-Griffith)
- `83aa7004b` fix needless ifdefs to a single ifdef for DEBUG_LOCKORDER for the file (Greg-Griffith)
- `126a06a45` move getTid() from sync.cpp to threaddeadlock.h (Greg-Griffith)
- `fb388e1a0` add (empty) threaddeadlock files to be used for new deadlock detection (Greg-Griffith)
- `315bc7658` minor refactoring of class ordering for consistency (Greg-Griffith)
- `d2d51d3c6` remove entire current deadlock detection system (Greg-Griffith)
- `d7be31f65` update travis centos config (#1934) (Griffith)
- `f5a06a796` [qa] Add tests for electrum cashaccounts (Dagur Valberg Johannsson)
- `d60fc2115` convert some python2 tools to python3, run optimize-pngs.py to crush png images (Andrew Stone)
- `7a6805f70` [qa] support debug build of electrscash (Dagur Valberg Johannsson)
- `777b9ca29` [qa] add timeout to electrum sockets (Dagur Valberg Johannsson)
- `b3b48a51d` [electrum] electrs v0.7 -> ElectrsCash v1.0 (Dagur Valberg Johannsson)
- `8f523fa7b` Add missing header to makefile (Dagur Valberg Johannsson)
- `7a738f25e` Bump rust to 1.37.0 (Dagur Valberg Johannsson)
- `dc3fe2f55` [rpc] Switch to BCH (was stats) in getblockstats (Dagur Valberg Johannsson)
- `0ac597375` rpc: faster getblockstats using BlockUndo data (Felix Weis)
- `c46397551` determine if param is a number string by length, not content. (Greg-Griffith)
- `1130db9b4` Allow getblockstats to use either the hash or a numerical block height (Greg-Griffith)
- `dae897c7f` Cashlib random bytes fix (Andrew Stone)
- `2691766db` interpret the 2nd getblockstats parameter as json (Andrew Stone)
- `03c81adbf` Restore the previous version of forEachThenClear (Peter Tschipper)
- `6164107c0` Install dependencies using apt for  "#x86_64 Linux" (Andrea Suisani)
- `4b2387056` Give more time to travis to build BU (Andrea Suisani)
- `b80382d3c` Fix potential deadlock (Peter Tschipper)
- `e75616fd9` Rename the secondary commit queue from just q to txCommitQFinal (Peter Tschipper)
- `5af280727` Consolidate mempool clearing code in case of reorg across hard fork block (Andrea Suisani)
- `1f8d7b48a` refactor: add a function for determining if a block is pruned or not (Karl-Johan Alm)
- `466a4dda2` Replace median fee rate with feerate percentiles (Marcin Jachymiak)
- `ddeb0efdd` Fix a bunch of warnings (Andrea Suisani)
- `cea90e1a8` Update cashlib.py script error enumerator (Andrea Suisani)
- `38d4444ae` No need for anonymous namespace for set_{success,error}() definition (Andrea Suisani)
- `6e746c1e2` Actual formatting for bitbitfield.* and related unit tests (Andrea Suisani)
- `3e97fe850` Add new files to the list of files to auto format (Andrea Suisani)
- `a9f4cc0e9` Add a facility to parse and validate script bitfields. (Amaury Séchet)
- `96d06c129` Move set_success and set_error to script/script_error.h (Andrea Suisani)
- `cc79f33e8` Update guiutil.cpp service flags list (Andrea Suisani)
- `6dea7ab8f` clear mempool during blockchain rewinds (#1902) (Andrew Stone)
- `35c77aa8b` RPC: Introduce getblockstats to plot things (#1913) (dagurval)
- `a9b3efa53` Use black magic for popcount. (Amaury Séchet)
- `f315f4d63` CPU Miner Enhancements -- Follow-up (Calin Culianu)
- `8dfef0012` CPU Miner Enhancements (#1904) (Calin Culianu)
- `1f684893f` Add #include guard to cashlib.h (Andrea Suisani)
- `2c9a76364` Add missing copyright header to cashlib header files (Andrea Suisani)
- `719205d5f` Update cashlib.py script flags list (Andrea Suisani)
- `3f65600e0` Fix bash syntax error in .travis.yml (Andrea Suisani)
- `543343c70` limit the txes sent to the inqueue in cases where many transactions have been received but not processed, since they will need to be pulled back during every mempool commit phase (Andrew Stone)
- `3d5daefb1` Fix a bash syntax error in .travis.yml (Andrea Suisani)
- `6d8473417` Update tweak label use to set Nov '19 mining fork time (Andrea Suisani)
- `33e504b14` Initialize nov2019ActivationTime and nMiningForkTime using a const rather than a literal (Andrea Suisani)
- `eafd55983` Add unit tests for Nov 2019 activation code (IsNov2019Enabled) (Dagur Valberg Johannsson)
- `c287b9a71` Clear mempool and txs queue in case we get a reorg across Nov 2019 block activation (Andrea Suisani)
- `514eb3866` Fixed compile error for --enable-debug on non-Linux (Calin Culianu)
- `7e62aabd8` Add helpers to use in Nov 2019 protocol upgrade MTP based code activation (Andrea Suisani)
- `828194322` Fix a few more nits (Peter Tschipper)
- `bacdcf06f` make sure CScript << vector generates MINIMAL_DATA compliant script binary (Andrew Stone)
- `5a7b6bb5e` Remove May, 15Th 2019 activation code (Andrea Suisani)
- `a8942c524` remove SCRIPT_ENABLE_SCHNORR flag and clean up tests (Mark Lundeberg)
- `bb5a4633f` remove effect of SCRIPT_ENABLE_SCHNORR flag (Mark Lundeberg)
- `263296ccc` Remove Schnorr activation (Mark Lundeberg)
- `efe9e63ca` Add script tests with valid 64-byte ECDSA signatures. (Mark Lundeberg)
- `4656b0993` remove four duplicate tests from script_tests.json (Mark Lundeberg)
- `82c5c3676` Make nTotalPackage and nTotalScore atomic (Peter Tschipper)
- `70f8ad3fd` Use a for loop instead of while when iterating through the mempool. (Peter Tschipper)
- `ad2f68946` A variety of nits (Peter Tschipper)
- `9c57d24ce` Accept SegWit recovery transactions if acceptnonstdtxn=1 (Andrea Suisani)
- `0d42580ee` Fix incorrect node being checked in segwit recovery test (Jason B. Cox)
- `1db325723` remove another unused variable (Andrea Suisani)
- `d9f9a5e5f` remove ComparisonTestFramework dependency from segwit recovery test (Mark Lundeberg)
- `538dc274e` remove unused variable (Andrea Suisani)
- `0284ab0b7` clean up script_tests -- move segwit recovery into static json (Mark Lundeberg)
- `18b77b2aa` Clean up Segwit Recovery feature (Antony Zegers)
- `fa0cf8428` [travis] timeout after script_b.sh only if we need to run unit tests (Andrea Suisani)
- `ccbac4076` Format bitcoin-miner.cpp (Andrea Suisani)
- `8efaa9e4c` adjust forkid error message (Greg-Griffith)
- `6753c4609` return false when inputs are missing. we cant continue with missing data (Greg-Griffith)
- `66673cfab` bitcoin_miner: Made CPU miner thread safe by using per-thread randgen (Calin Culianu)
- `1f3e7b888` CPFP (AGT) (Peter Tschipper)
- `2a2cf343b` Use ancestor-feerate based transaction selection for mining (Suhas Daftuar)
- `118c2ee6a` add private key parsing to cashlib (#1892) (Andrew Stone)
- `f06e89056` add missing script error string translations (#1884) (Griffith)
- `4d92223ab` fix mutex self-deadlock in stat and add regression test (#1887) (Andrew Stone)
- `3f9f0adf6` Fix transactions per second data (#1891) (Peter Tschipper)
- `e96b3db10` [script] simplify CheckMinimalPush checks, add safety assert (#1882) (dagurval)
- `da8c2e888` [electrum] Option to shutdown node if electrs dies (#1866) (dagurval)
- `0504448db` Add script tests (#1881) (dagurval)
- `b757533be` fix spelling issue, madatoy -> mandatory (#1886) (Griffith)
- `b43da6f01` fix ping latency time to use stopwatch time (#1880) (Andrew Stone)
- `8c2702f4f` [Electrum] Support passing arguments to electrs (#1874) (dagurval)
- `f79841996` if not pruning, set preallocation of block/undo files to their max size (#1873) (Griffith)
- `ecbe37e41` SCRIPT_ENABLE_CHECKDATASIG flag not needed in Schnorr unit tests (#1878) (Andrea Suisani)
- `ebd2619e5` Update electrum documentation (#1876) (dagurval)
- `ca7bb68cd` Using cashaddr econding by default for addresses. (#1877) (Andrea Suisani)
- `d23e58ef4` Validaterawtx (#1726) (Griffith)
- `680594b60` Standard flags cleanups (#1847) (Andrea Suisani)
- `0ad30ba2c` Time cleanup (#1872) (Andrew Stone)
- `8ad384839` [qa] Enable electrum if binary has been built (#1869) (dagurval)
- `b49761878`  Make it so that uacomment[] are correctly reported in subver string (#1830) (Andrea Suisani)
- `359fb009c` During IBD we need to flush cache as infrequently as possible. (#1868) (Peter Tschipper)
- `5cc1c3398` Fix a compiler warning showed when native compiling on a 32 bit linux machine (#1839) (Andrea Suisani)
- `155870dcf` Terminate rpc-tests.py if there's no test to run. (#1870) (Andrea Suisani)
- `c79d37fec` Refactor cache configuration settings (#1867) (Peter Tschipper)
- `9d3963912` Clarify the debug ui transaction pool naming (#1849) (Peter Tschipper)
- `9870aee7b` Significantly reduce the number of dbcache lookups (#1864) (Peter Tschipper)
- `feba375ac` Simplify the intialization of g_txindex and txindex_db (Peter Tschipper)
- `47551f9f2` The caller should lock cs_main for FindForkInGlobalIndex() (Peter Tschipper)
- `208240ff0` Tidy up cs_main locking (Peter Tschipper)
- `5cb75b629` Add python tests (Peter Tschipper)
- `fdb9767dc` Change BlockUntilSynced to chain to IsSynced(). (Peter Tschipper)
- `13bd0f63d` Remove the Read and Write flags for txindex (Peter Tschipper)
- `b8d45bda6` Do not writebest block if we are shutting down but have not written the txindex data (Peter Tschipper)
- `66e14726f` Set the fReindex flag when starting txindex (Peter Tschipper)
- `a2581715a` Adjust parameter naming to be more in alignement with BU's codebase (Peter Tschipper)
- `124baf9fe` Don't allow rpc until the txindex is fully synced (Peter Tschipper)
- `0dfe2b0ab` Tidy up the progress messages for the upgrade (Peter Tschipper)
- `40bf498f6` Remove unnecessary check for fTxIndex in init.cpp (Peter Tschipper)
- `fec16f6f7` Fix error in params (Peter Tschipper)
- `00d5a8c1e` Commit the new txns to the txindex database in ConnectBlock() (Peter Tschipper)
- `d0ebbd9f5` Remove unused functions (Peter Tschipper)
- `9f2e8e19a` Move txindex logic from  GetTransaction() to TxIndex::FindTx() (Peter Tschipper)
- `edb90ab6e` [init] Initialize and start TxIndex in init code. (Jim Posen)
- `cb34348c2` [index] TxIndex method to wait until caught up. (Jim Posen)
- `63446b594` [index] Allow TxIndex sync thread to be interrupted. (Jim Posen)
- `556895ba4` [index] TxIndex initial sync thread. (Jim Posen)
- `f3f3e812f` [index] Create new TxIndex class. (Jim Posen)
- `f1efa8ba9` [db] Migration for txindex data to new, separate database. (Jim Posen)
- `a1fcc399e` [db] Create separate database for txindex. (Jim Posen)
- `7b125b78f` Initialize the dbcache size and nCoinCacheMaxSize for the unit tests (Peter Tschipper)
- `5a04d307e` move debug defines to configure from util.h (Greg-Griffith)
- `faf92c608` get cashlib compiling on android, and add JNI calls in android and linux. (Andrew Stone)
- `b27641224` UPnP bug fix: When disabling UPnP from options dialog we get a hang. (Peter Tschipper)
- `d840576ef` Add UpdateBlockAvailability() when we handle  Thinblocks or CompactBlocks (Peter Tschipper)
- `fad65ce29` Make sure to lock cs_mapBlockIndex when we RaiseValidity() (Peter Tschipper)
- `3c731d11c` Pruning: fix lock order issue and potential deadlock (Peter Tschipper)
- `fedafa1ca` Update comments for ProcessGetData()  and prevent taking lock if not needed. (Peter Tschipper)
- `c09ce100e` Fix compiler warnings (Peter Tschipper)
- `71d098859` Make nCoinCacheMaxSize atomic (Peter Tschipper)
- `6ff54f748` If not on windows, replace boost once with std once replace boost mutex with std mutex. (Andrew Stone)
- `e2ebe1a4f` Fix checkblock_test hidden failure (#1834) (Justaphf)
- `60d3ad764` Implementation of variable keycheck mask for CIblt (#1819) (bissias)
- `8d38508e7` A few small fixes to trimming the coincache during IBD or after txadmission (#1813) (Peter Tschipper)
- `9d9f0c27d` Fix edge condition where we try to connect an invalid chain but… (#1838) (Peter Tschipper)
- `c33720d2c` Fix hang on shutdown when upnp is enabled (#1856) (Peter Tschipper)
- `a0ed4cf37` Show the peak transaction per second rate in the debug ui (#1842) (Peter Tschipper)
- `22103cacf` Update docs to agree with commit 12e21ec4b (George Bissias)
- `55c6e8804` remove unserialized fields from CGrapheneBlock and CGrapheneSet (George Bissias)
- `fa5480845` fix broken image link (George Bissias)
- `b63dba94b` add specification for Graphene version 2.1 (George Bissias)
- `8cc3c3514` small edit to iblt spec (George Bissias)
- `06266a87e` small edits to fast filter (George Bissias)
- `bdb1eec06` fix formatting in fast filter (George Bissias)
- `a4124033b` add spec for fast filter (George Bissias)
- `a74bb72ee` minor changes (George Bissias)
- `c402d2ccc` initial draft of IBLT spec (George Bissias)
- `7a920f0bf` Tidy up request manager block type checking   so that we only have to make changes in one place. (Peter Tschipper)
- `17c83f76a` update head commit hash (#1848) (Andrea Suisani)
- `0892e9e76` Criptolayer nodes seeder are going to be decomissioned (#1841) (Andrea Suisani)
- `1868b9158` Fix invalid memory access in CScript::operator+= (#1846) (Andrea Suisani)
- `e618f957c` Add additional unit tests for segwit recovery (Florian)
- `d9dc5610a` [travis] Re-enable unit tests for win64 (Andrea Suisani)
- `34cd9f2ac` [travis] Disable unit tests for win32/64 only via RUN_TESTS=false (Andrea Suisani)
- `a61ec4d5b` Use temporaries when evaluating atomics in an 'if' statment (Peter Tschipper)
- `cd9e9dad2` Fix edge condition where we try to connect an invalid chain (Peter Tschipper)
- `b9905b5a8` Run unit test without using travis_wait (Andrea Suisani)
- `951b5dc40` Print folding log statements for unit tests only if we run make check (Andrea Suisani)
- `0406c1190` debugging electrs deterministic build (Andrew Stone)
- `dc48a8c47` move electrs forward (Andrew Stone)
- `ee8f321c2` Run unit test without using travis_wait (Andrea Suisani)
- `a721e9424` Print folding log statements for unit tests only if we run make check (Andrea Suisani)
- `cb119c64b` [qa] Add electrum address balance test (Dagur Valberg Johannsson)
- `8e3cbc805` [qa] electrum helper functions (Dagur Valberg Johannsson)
- `86c15677b` [qa] Add ElectrumConnection (Dagur Valberg Johannsson)
- `6447f6f37` [qa] add cashaddr module (Shammah Chancellor)
- `44fb481c3` [qa] calc hash on prevtx in create_transaction (Dagur Valberg Johannsson)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Calin Culianu
- Dagur Valberg Johannsson
- George Bissias
- Greg-Griffith
- Justaphf
- Peter Tschipper

We have backported an amount of changes from other projects, namely Bitcoin Core, Bitcoin ABC and ZCash.

Following all the indirect contributors whose work has been imported via the above backports:

- Amaury Séchet
- Antony Zegers
- Ben Woosley
- Felix Weis
- Florian
- Jack Grigg
- Jason B. Cox
- Jim Posen
- Johnson Lau
- Karl-Johan Alm
- Marcin Jachymiak
- Mark Lundeberg
- murrayn
- Shammah Chancellor
- Suhas Daftuar


