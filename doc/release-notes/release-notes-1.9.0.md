Release Notes for BCH Unlimited 1.9.0
======================================================

BCH Unlimited version 1.9.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a major release of BCH Unlimited compatible with the upcoming protocol upgrade of the Bitcoin Cash network. You could find
Nov 15th, 2020 upgrade specifications here:

- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2020-11-15-upgrade.md

The following is a list of the previous network upgrades specifications:

- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/uahf-technical-spec.md (Aug 1st '17, ver 1.1.0.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17, ver 1.1.2.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/may-2018-hardfork.md (May 15th '18, ver 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18, ver 1.5.0.0, 1.5.0.1, 1.5.0.2, 1.5.1.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2019-05-15-upgrade.md (May 15th '19, ver 1.6.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2019-11-15-upgrade.md (Nov 15th '19, ver 1.7.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2020-05-15-upgrade.md (May 15th '20, ver 1.8.0)

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

Main Changes in 1.9.0
---------------------

This is list of the main changes that have been merged in this release:

- ASERT: new difficulty adjustment algorithm
- DSProof: Double spend Proof
- libsecp256k1 update
- Benchmark suites update
- QA test improvements
- Improve code base parallelism
- Bundled with ElectrsCash new version 2.0.0
- Xversion upgrade

Commit details
--------------

- `e03d523c7` Bump version of BCHUnlimited to 1.9 (Andrea Suisani)
- `5a38ab366` Fix MAX_STANDARD_TX_WEIGHT check (Johnson Lau)
- `be4e4329d` a few small extra checks: make sure pskip is not nullptr, and be explicit about the uint16 to uint64 type promotion rather than relying on the compiler's implicit rules (Andrew Stone)
- `371c542ae` Update the user facing name for the miningForkTime tweak (Andrea Suisani)
- `cf6c258e4` change frac from auto to uint16_t just for clarity. (Griffith)
- `abb3d9ee5` add asert spec to docs folder (Griffith)
- `210de0257` port additional asert c++ tests and fix formatting (Griffith)
- `cc8e2504b` port ASERT daa c++ test from BCHN (contains minor changes) (Griffith)
- `17b9b0f06` port ASERT daa from BCHN (contains minor changes) (Griffith)
- `d53a7e29e` add nDAAHalfLife to consensus params (Griffith)
- `5d55cb348` Add more logging to ctor.py (Andrew Stone)
- `4198bbd33` Add ctorout.txt to the artifacts set for qa testing (Andrea Suisani)
- `4f62981e1` Save the spend hash rather than the mempool iterator (Peter Tschipper)
- `4bc1a779b` break up nodeps build in CI (Griffith)
- `2b4ab5bde` Correctly remove orphans from the doubleSpendProofStorage (Peter Tschipper)
- `b8fa47688` Add code comments to DoubleSpendProof.cpp/.h (Peter Tschipper)
- `8f9e7b163` When validating DSP, return Invalid if P2SH is true or GetOp fails (Peter Tschipper)
- `4c1ffdac8` Remove unused functions firstSpender(), doubleSpender() (Peter Tschipper)
- `0b791cdc1` Add DbgAssert() in hashTx() (Peter Tschipper)
- `0103f9129` Update the double-spend-proof-specification (Peter Tschipper)
- `3abb57535` change creatHash() and make it GetHash() (Peter Tschipper)
- `3944684da` Re-enable the early return if respend is not interesting (Peter Tschipper)
- `0246f38bb` refactor broadcastDSPInv() out of the mempool write lock scope (Peter Tschipper)
- `dcc6b80fb` Send a reject message is the dsproof was not found when requested (Peter Tschipper)
- `44e3d196d` Convert assert() statements to DbgAssert() (Peter Tschipper)
- `af1966edb` Fix potential assert (Peter Tschipper)
- `84c8b968e` change dsproof to dsproof-beta (Peter Tschipper)
- `57a471614` Forward dsproofs to SPV peers for descendant txns (Peter Tschipper)
- `00804e89c` Consolidate duplicate code for dsp broadcasting (Peter Tschipper)
- `c108933e7` fix failing test in respenddetector_tests.cpp (Peter Tschipper)
- `0adefa1a7` fix locking issue (Peter Tschipper)
- `803c07839` Add tests for doublespend proof orphans (Peter Tschipper)
- `86ec70ec8` Reintroduce the handling of dsproof orphans (Peter Tschipper)
- `4d2fa151f` Use a multi input transaction for testing dsproof (Peter Tschipper)
- `000e4ccfc` Add Log message when broadcasting INV for dsproof (Peter Tschipper)
- `9a9a3cdb8` Remove last check for missing signature (Peter Tschipper)
- `94e713995` Add check for identical transactions (Peter Tschipper)
- `2a4c4a60b` Add test for order of transactions in dsproof (Peter Tschipper)
- `0a2bab0b6` Unit tests for DoubleSpend proofs (Peter Tschipper)
- `35a458aa2` Add DSPROOF to the allNetMessageType array (Peter Tschipper)
- `566b9f6dc` Clean up the requesting of a dsproof via and INV message (Peter Tschipper)
- `8980eddc4` Remove relaying of double spent transactions (Peter Tschipper)
- `038147e97` Only print out log messages if DSPROOF is enalbed (Peter Tschipper)
- `b3d3cbc53` Fix Resend Relayer test (Peter Tschipper)
- `48091facc` Remove the old dsproof broadcasting code (Peter Tschipper)
- `8a6454a40` Broadcast the dbouble spend from within the respendrelayer trigger (Peter Tschipper)
- `78baf3c54` change message name from dsproof-beta to dsproof (Peter Tschipper)
- `8c6a45577` fix getlogcategories.py (Peter Tschipper)
- `f164d51fc` Fix various complile wanrnings (Peter Tschipper)
- `c1420c9ec` Add doublespend cpp/h files to .formatted-files (Peter Tschipper)
- `cb40a023b` fix compile issue for some platforms (Peter Tschipper)
- `974825702` Fix a host of rebase issues and formatting issues (Peter Tschipper)
- `524f45c07` Add some more comments and sanity checks (TomZ)
- `4d8cb8825` Nicely initialize the variable every loop (TomZ)
- `c9e450390` Import DSP spec from gitlab.com/snippets/1883331 (TomZ)
- `28ab94520` Import DoubleSpendProof support (TomZ)
- `e3c734c96` clean up how we use python Proc, add a notice to compact_blocks2 that an exception is expected. (Andrew Stone)
- `65abd5b8e` Make sure to remove an item when there are no longer any sources (Peter Tschipper)
- `877c8895b` Add a null check for `CNode*` when sending priority messages (Peter Tschipper)
- `222da9f7e` Change the isValid description in the validaterawtransaction help text (Peter Tschipper)
- `0ad088092` adjust locking in pval to avoid cs_main being held thru delay loop (Andrew Stone)
- `93765a87b` fix a few timing issues in mempool_push that cause rare failures (Andrew Stone)
- `3bacbb6ab` treat too deep unconf chain tx as orphans (Andrew Stone)
- `48e4193c3` Avoid deleting core files after inspection, mving it in a different path (Andrea Suisani)
- `af323b634` Remove useless gdb command from coreanalysis.gdb (Andrea Suisani)
- `3c808fe30` Add info proc and info proc cmdline to the list of command executed by gdb (Andrea Suisani)
- `1d4b1f622` Add bitcoind and cores to qa artifacts in case of failure (Andrea Suisani)
- `bac1dd08a` Print the core file name before trying to extract info from it (Andrea Suisani)
- `fd3dc175a` Use a catch-all exception when it comes to core dump analysis (Andrea Suisani)
- `93fff3770` Add missing gdb package in case we need to analyze core dump (Andrea Suisani)
- `62cb10a3a` Increase DELAY_TIME in mempool_push.py (Andrea Suisani)
- `63e3de960` Create the temporary folder where to store eventual core dump (Andrea Suisani)
- `9c70cb410` Timeout individual tests after 20 minutes also when in gitlab CI (Andrea Suisani)
- `4e87f3a33` update pull-tester rpc test list (Griffith)
- `97b563b9c` remove signchecks_inputs activation test (Griffith)
- `a4de1335b` remove signchecks activation py test (Griffith)
- `98aa733d0` simplify op_reversebytes_activation to op_reversebytes.py (Griffith)
- `99eac98e7` remove activation for mempool policy. fork has already occurred. (Griffith)
- `734c06e00` update may activation functions to use IsMay2020Activated (Griffith)
- `4533ffa51` change may 2020 to block height in chain params, add nov2020 HF MTP (Griffith)
- `59af25be3` change may 2020 HF variable to use the block height instead of time (Griffith)
- `09f558410` add nov 2020 HF activation time variable to consensus params (Griffith)
- `94ae8da66` add new next HF check functions and may HF activated functions (Griffith)
- `e5a55f518` Make thinblock and grapheneblock checks more permissive (Peter Tschipper)
- `3bb385725` Handle modifications to mempool between hash access and tx access in graphene (Andrew Stone)
- `de8434a6b` need to increase timeouts for debug build because mempool check is very slow for large mempools.  need to provide the datadir to bitcoin-cli (Andrew Stone)
- `461878e71` [cashlib] Export 'sign data' (Dagur Valberg Johannsson)
- `448d4f631` Implement xversion handshake state (Griffith)
- `b558c5157` Always send inventory to SPV peers (Peter Tschipper)
- `de20d9b2e` Change xversion key size (Griffith)
- `3fc311655` upload qa tests as artifact on qa_tests failure (Griffith)
- `13859fa24` remove old debugging prints (Peter Tschipper)
- `f204fd734` When checking for conflicts in mempool_accept.py use a subprocess (Peter Tschipper)
- `af55941a1` Use sendrawtransaction in mempool_accept.py (Peter Tschipper)
- `116dd8c56` Serialize AcceptToMemoryPool() (Peter Tschipper)
- `f52c48b8e` Add missing stack include (Axel Gembe)
- `983cbb1c9` [electrum] Allow passing switch params with rawarg (Dagur Valberg Johannsson)
- `d723fb433` [qa] Add tests for `blockchain.*.get_first_use` (Dagur Valberg Johannsson)
- `17d29fa2b` Fix debug ui -> blockcount and size not updating (Peter Tschipper)
- `91890c896` remove tag (Peter Tschipper)
- `74b8585bb` Always ensure we have at least one scriptcheck thread running (Peter Tschipper)
- `5a4de43f3` Use the correct variable name CI_MERGE_REQUEST_IID (Andrea Suisani)
- `b25d36df1` Remove MSG_MEMPOOLSYNC as an inventory message type (Peter Tschipper)
- `1fd81f61f` Split build-debian in two job one for deps and for the actual bitcoind sw (Andrea Suisani)
- `d015961be` Add NO_RUST parameter to depends Makefile (Andrea Suisani)
- `1c905aa29` Activate Repo Lockdown bot on GitHub Legacy repository. (Andrea Suisani)
- `238408531` [electrum] Introduce electrum.blocknotify (#2233) (dagurval)
- `ed84d5b9f` [qa] Add benchmark comparator tool (freetrader)
- `f62f2d4c0` [qa] Multiple client electrum subscription test (Dagur Valberg Johannsson)
- `9b84890e6` [qa] Convert electrum test to async (Dagur Valberg Johannsson)
- `cf49aaf4e` bump cmake version due to linking problem compiling with old versions (#2230) (Andrew Stone)
- `e13a49d39` output confirmations and more accurate time in some RPC calls in some edge conditions (#2225) (Andrew Stone)
- `5b488ba0b` keep unprocessed priority messages, shorten messagehandler sleep (#2224) (Griffith)
- `6b59ad008` Fix bench_bitcoin command line flags parsing (#2227) (Andrea Suisani)
- `84b4695df` Remove warning about not NUL-terminated string (Andrea Suisani)
- `06fc6447c` Fix shadowing warnings on csv parser (Andrea Suisani)
- `d05f815de` Squashed 'src/fast-cpp-csv-parser/' changes from 3b439a6640..327671c577 (Andrea Suisani)
- `bb51d6723` Change xversion to match new spec (#2134) (Griffith)
- `9cc8dccab` Remove reference counting from the requestmanager (#2209) (Peter Tschipper)
- `e9aefddcd` Do not shutdown bitcoind if ActivateBestChain() fails on startup (#2211) (Peter Tschipper)
- `0defe16dc` [cashlib] Throw in GetPubKey on invalid argument (Dagur Valberg Johannsson)
- `d341c5208` Add test case for stack mutation (Chris Pacia)
- `a1efff086` Add the ability to launch or kill txAdmissionThreads and msgHandlerThreads (#2200) (Peter Tschipper)
- `bc9c35869` Fix formatting for `cashlib/cashlib.cpp` (#2220) (Andrea Suisani)
- `19420749b` [cashlib] Handle address type correctly (#2219) (dagurval)
- `d72461a8e` Make getblock  RPC call works both with block height and block hash (#2205) (Andrea Suisani)
- `b356ba1ff` Remove a useless assert() from getblockchainfo() (Andrea Suisani)
- `67bd407bf` [rpc] getblockchaininfo: add size_on_disk, prune_target_size (Daniel Edgecumbe)
- `c7ff1a5c6` Add mainet and testnet checkpoints for May 2020 net upgrade (Andrea Suisani)
- `73c79c892` add missing defines for Android cashlib based on libssecp256k1 changes (Andrew Stone)
- `cad61c742` have mempool tests use global mempool object not locally created one (Greg-Griffith)
- `0eb33f062` This will come in handy in a short while for benchmarking new UniValue code changes/optimizations. (Calin Culianu)
- `f4bd47227` update old rpm ius release link (#2215) (Griffith)
- `dce9ab871` Make getblockheader works both with block height and block hash (#2202) (Andrea Suisani)
- `f295dcd5c` 4 fix/cleanups (#2199) (Andrew Stone)
- `583659698` [qa] Electrum tests for `blockchain.address.*` (#2213) (dagurval)
- `f73a2a9d3` Add a new log category for priority queue messages (#2203) (Peter Tschipper)
- `3c91b661d` bench: Benchmark blockToJSON (Kirill Fomichev)
- `54a3c9a6c` bench: Move generated data to a dedicated translation unit (João Barbosa)
- `eb12dc156` Remove unused includes (Andrea Suisani)
- `288342cc6` Add `src/bench/bench_constants.h` to `Makefile.bench.include` (#2212) (Andrea Suisani)
- `ed175e2a9` bench: Benchmark mempoolToJSON (#2206) (Andrea Suisani)
- `edc2b1389` Disable reconsidermostworkchain during initial bootstrap when chain is not synced (Andrea Suisani)
- `02b4fc4ee` Change txvalidationcache unit tests to work with the new max len chain limit (#2208) (Andrea Suisani)
- `37645dad5` add modaloverlay.cpp/h to formatted files (Peter Tschipper)
- `c7bd75a22` Use compare_exchange_weak when setting bestBlockHeight (Peter Tschipper)
- `105b6122c` fix format (Peter Tschipper)
- `285a7db79` Add signal for Header Tip UI update (Peter Tschipper)
- `bee2d4e77` Add code that went missing during the port from Core. (Peter Tschipper)
- `203da05d5` [Qt] modalinfolayer: removed unused comments, renamed signal, code style overhaul (Jonas Schnelli)
- `25839fa97` [Qt] only update "amount of blocks left" when the header chain is in-sync (Jonas Schnelli)
- `242fd66a6` [Qt] add out-of-sync modal info layer (Jonas Schnelli)
- `9bfade775` [Qt] make Out-Of-Sync warning icon clickable (Jonas Schnelli)
- `eb799d4b6` [Refactor] refactor function that forms human readable text out of a timeoffset (Jonas Schnelli)
- `0709f4d07` Move ActivateBestChain() into ThreadImport() during startup (#2184) (Peter Tschipper)
- `e67b8c5b6` [giga-net] Concatenate transactions before sending (#1852) (Peter Tschipper)
- `fe38cf34b` [Giga-net] Activate XVal for newly mined blocks (#1791) (Peter Tschipper)
- `9213429e5` Speed up OP_REVERSEBYTES test significantly (tobiasruck)
- `b9ed54231` Error out and exit if unknown command line parameters are used (Andrea Suisani)
- `b7db11c8e` trivial: Mark overrides as such. (Daniel Kraft)
- `7f743ad5c` bench: Add block assemble benchmark (MarcoFalke)
- `05765d9b6` Remove SATOSHI constant from sigcache_tests.cpp (Andrea Suisani)
- `f1cacf162` benchmark: Removed bench/perf.{cpp.h} (Thomas Snider)
- `3f62aaba9` bench: Move constructors out of mempool_eviction hot loop (MarcoFalke)
- `ea3e14617` bench/tests: Avoid copies of CTransaction (MarcoFalke)
- `2405fb6fb` Fix missing or inconsistent include guards (practicalswift)
- `b6bc4a612` Add missing bench files to the list of the ones that have to be formatted (Andrea Suisani)
- `a04452116` Fix a memory leak in bench and use C++1 for loop rather than BOOST_FOREACH (Andrea Suisani)
- `eafd104f1` Use PACKAGE_NAME instead of hardcoding application name in log message (Wladimir J. van der Laan)
- `439a25c86` Log debug build status and warn when running benchmarks (Wladimir J. van der Laan)
- `7ba49f939` Removed CCheckQueueSpeed benchmark (Martin Ankerl)
- `cc4599f80` Improved microbenchmarking with multiple features. (Martin Ankerl)
- `c7af95c75` Remove deadlock detector variable definition in bench_bitcoin (Andrea Suisani)
- `499ac76b7` Remove useless include from Example.cpp (Andrea Suisani)
- `6a265f8c0` Initialize recently introduced non-static class member lastCycles to zero in constructor (practicalswift)
- `789b4fdea` Require a steady clock for bench with at least micro precision (Matt Corallo)
- `58ede7cd2` bench: prefer a steady clock if the resolution is no worse (Cory Fields)
- `f90ee20e6` bench: switch to std::chrono for time measurements (Cory Fields)
- `16959e0a1` Remove countMaskInv caching in bench framework (Matt Corallo)
- `19340bfbd` Changing &vec[0] to vec.data() (Andrea Suisani)
- `aba39dd86` Avoid unwanted conversion for the PrevectorJob contructor (Andrea Suisani)
- `14c4eab45` Avoid static analyzer warnings regarding uninitialized arguments (practicalswift)
- `e9abdbaff` Replace boost::function with std::function for bench suite (Andrea Suisani)
- `e8ba4a81e` Use FastRandomContext::randrange in src/bench/checkqueue.cpp (Andrea Suisani)
- `60530c41d` Properly initialize random number generator for the unit tests suite (Andrea Suisani)
- `40ef87874` [bench] Avoid function call arguments which are pointers to uninitialized values (practicalswift)
- `dbc84dbf3` bench: Fix initialization order in registration (Wladimir J. van der Laan)
- `7d0404964` Assert that what might look like a possible division by zero is actually unreachable (practicalswift)
- `bd59d6a61` Fix a typo in src/bench/perf.cpp (Andrea Suisani)
- `1fa8b619b` Address ryanofsky feedback on CCheckQueue benchmarks. (Jeremy Rubin)
- `3c978c714` Add Basic CheckQueue Benchmark (Jeremy Rubin)
- `4f43bf145` Add missing random generator initialization in bench_bitcoin.cpp (Andrea Suisani)
- `713256fa8` build: Make "make clean" remove all files created when running "make check" (practicalswift)
- `e06d1b467` Bugfix: Correctly replace generated headers and fail cleanly (Luke Dashjr)
- `7cc5f249b` Add microbenchmarks to profile more code paths. (Russell Yanofsky)
- `491787c6c` bench: Add support for measuring CPU cycles (Wladimir J. van der Laan)
- `a7a719130` Add deserialize + CheckBlock benchmarks, and a full block hex (Matt Corallo)
- `feb52902e` bench: Added base58 encoding/decoding benchmarks (Yuri Zhykin)
- `bc617fe74` bench: Fix subtle counting issue when rescaling iteration count (Wladimir J. van der Laan)
- `5ca3c9248` Avoid integer division in the benchmark inner-most loop. (Gregory Maxwell)
- `0f519a504` Try to send xthin blokcs via expedited only if when needed (Andrea Suisani)
- `f4484ba3f` Use trasaction reference in hasNoDependencies mempool method (Andrea Suisani)
- `d82171eed` [Doc only] update the xversion spec (#2132) (Griffith)
- `6a1825bbc` [electrum] Add test for .scripthash.unsubscribe (Dagur Valberg Johannsson)
- `9cc80486f` Revert "Pin ElectrsCash to v1.1.1 tag (#2167)" (Andrea Suisani)
- `24c7c9b47` Use correct type for syncMempoolWithPeers tweak (Andrea Suisani)
- `298d0881c` Removed unused reference to removed tweaks (Andrea Suisani)
- `e08bee049` remove some commented out code for gui alerts (Greg-Griffith)
- `22ca076de` change connection limitation log prints to be more descriptive (Greg-Griffith)
- `b6636e0ff` [electrum] Catch http_get error in getelectruminfo (Dagur Valberg Johannsson)
- `b084cf4eb` Fixes warning on variable shadowing (Dagur Valberg Johannsson)
- `a46189e96` try to bump file descriptors to requested amount before setting maxconns (Greg-Griffith)
- `c49b589a5` set the maxoutboundconns after maxconns as it depends on it (Greg-Griffith)
- `400cd73af` add missing lot_mutex lock in DeleteCritical method in CLockOrderTracker (Greg-Griffith)
- `ab516bf84` Add regular blocks to the priority queue (Peter Tschipper)
- `cf383f79f` Remove duplicate code from TestConservativeBlockValidity() (Peter Tschipper)
- `449867661` Add bool flag fConservative to TestBlockValidity (Peter Tschipper)
- `bfe022da5` Move TestConservativeBlockValidity into validation.cpp (Peter Tschipper)
- `69fed501d` [SECP256K1] Fix issue where travis does not show the logs (Jonas Nick)
- `c6e94f53b` [SECP256K1] Request --enable-experimental for the multiset module (Fabien)
- `b9a4ef081` [SECP256K1] Fix a valgrind issue in multisets (Fabien)
- `e464333f9` Remove secret-dependant non-constant time operation in ecmult_const. (Gregory Maxwell)
- `04ff1abbf` Preventing compiler optimizations in benchmarks without a memory fence (Elichai Turkel)
- `987738996` README: add a section for test coverage (Marko Bencun)
- `3979c6ed3` Overhaul README.md (Tim Ruffing)
- `48279ad90` Convert bench.h to fixed-point math (Wladimir J. van der Laan)
- `dab9ca5ff` Add SECURITY.md (Jonas Nick)
- `ea96adf14` Clarify that a secp256k1_ecdh_hash_function must return 0 or 1 (Tim Ruffing)
- `a0e817337` doc: document the length requirements of output parameter. (Rusty Russell)
- `4d14cdbf2` variable signing precompute table (djb)
- `6e370d486` Docstrings (Marko Bencun)
- `87b2d7160` Increase robustness against UB in secp256k1_scalar_cadd_bit (roconnor-blockstream)
- `06ea34ef9` Remove mention of ec_privkey_export because it doesn't exist (Jonas Nick)
- `bcf9360ed` Remove note about heap allocation in secp256k1_ecmult_odd_multiples_table_storage_var (Jonas Nick)
- `046f1930b` Make no-float policy explicit (Tim Ruffing)
- `b81b98936` JNI: fix use sig array (liuyujun)
- `ddce63353` Avoid calling secp256k1_*_is_zero when secp256k1_*_set_b32 fails. (Gregory Maxwell)
- `911c810db` Add a descriptive comment for secp256k1_ecmult_const. (Gregory Maxwell)
- `e9c5af824` secp256k1/src/tests.c:  Properly handle sscanf return value (Mustapha Abiola)
- `9800a4714` Fix typo (practicalswift)
- `73fc00228` Fix typo in secp256k1_preallocated.h (Jan Xie)
- `14d7ac520` Make ./configure string consistent (Tim Ruffing)
- `6de23a816` Fix a nit in the recovery tests (Elichai Turkel)
- `2bb1c151d` typo in comment for secp256k1_ec_pubkey_tweak_mul () (philsmd)
- `8775aed21` scalar_impl.h: fix includes (Marko Bencun)
- `b1345a2c6` Moved a dereference so the null check will be before the dereferencing (Elichai Turkel)
- `0a7a55f6e` Fix typo in docs for _context_set_illegal_callback (Tim Ruffing)
- `49069982f` [secp256k1] Allow to use external default callbacks (Tim Ruffing)
- `513503326` [secp256k1] Remove a warning in multiset test (Amaury Séchet)
- `4b0e48bd6` Add ECMH multiset module to libsecp256k1 (Tomas van der Wansem)
- `787737ffc` [SECP256K1] Fix ability to compile tests without -DVERIFY. (Gregory Maxwell)
- `078670905` scratch space: use single allocation (Andrew Poelstra)
- `f249faa83` Enable context creation in preallocated memory (Tim Ruffing)
- `b4a1d3148` Make WINDOW_G configurable (Tim Ruffing)
- `7a7e3fc1e` Use trivial algorithm in ecmult_multi if scratch space is small (Jonas Nick)
- `b4ef9a242` Note intention of timing sidechannel freeness. (Gregory Maxwell)
- `38e5a1885` configure: Use CFLAGS_FOR_BUILD when checking native compiler (Tim Ruffing)
- `b31767625` Respect LDFLAGS and #undef STATIC_PRECOMPUTATION if using basic config (DesWurstes)
- `b3963c12e` Make sure we're not using an uninitialized variable in secp256k1_wnaf_const(...) (practicalswift)
- `9b262aae3` Pass scalar by reference in secp256k1_wnaf_const() (Tim Ruffing)
- `d0a8fb83f` Avoid implementation-defined and undefined behavior when dealing with sizes (Tim Ruffing)
- `cfee5fb47` Guard memcmp in tests against mixed size inputs. (Gregory Maxwell)
- `5d5cabdca` Use __GNUC_PREREQ for detecting __builtin_expect (Tim Ruffing)
- `8e4bf0341` Add $(COMMON_LIB) to exhaustive tests to fix ARM asm build (Gregory Maxwell)
- `b99e73c8c` Switch x86_64 asm to use "i" instead of "n" for immediate values. (Gregory Maxwell)
- `41026917f` Allow field_10x26_arm.s to compile for ARMv7 architecture (Roman Zeyde)
- `a37c4b0d9` Clear a copied secret key after negation (Seonpyo Kim)
- `45285e88c` Use size_t shifts when computing a size_t (Pieter Wuille)
- `ec8e0c73d` Fix integer overflow in ecmult_multi_var when n is large (Jonas Nick)
- `f69ba0b35` Add trivial ecmult_multi algorithm which does not require a scratch space (Jonas Nick)
- `739354368` Make bench_internal obey secp256k1_fe_sqrt's contract wrt aliasing. (Gregory Maxwell)
- `fab890597` Summarize build options in configure script (Evan Klitzke)
- `588fa2825` Portability fix for the configure scripts generated (Pierre Pronchery)
- `b1a92f87b` Correct order of libs returned on pkg-config --libs --static libsecp256k1 call. (Phillip Mienk)
- `01fa0c8c6` Eliminate scratch memory used when generating contexts (Andrew Poelstra)
- `7c02862ce` Optimize secp256k1_fe_normalize_weak calls. (Russell O'Connor)
- `b2e52710e` Assorted minor corrections (Russell O'Connor)
- `2d3bdf82b` Make constants static: static const secp256k1_ge secp256k1_ge_const_g; static const int CURVE_B; (Russell O'Connor)
- `fd9b158da` secp256k1_fe_sqrt: Verify that the arguments don't alias. (Russell O'Connor)
- `850582f1a` Make randomization of a non-signing context a noop (Tim Ruffing)
- `5ff4d3d06` add static context object which has no capabilities (Andrew Poelstra)
- `3af502fe8` Fix algorithm selection in bench_ecmult (Jonas Nick)
- `2ed9a0770` Make use of TAG_PUBKEY constants in secp256k1_eckey_pubkey_parse (Ben Woosley)
- `13747a767` improvements to random seed in src/tests.c (Don Viszneki)
- `462e2b0cf` [secp256k1] [ECDH API change] Support custom hash function (Amaury Séchet)
- `c86a8df67` Update secp256k1 README (Antony Zegers)
- `27030cd4b` [SECP256K1] Create a different library when building with JNI (Fabien)
- `9cb5bc507` [SECP256K1] Build java class files out of tree (Fabien)
- `c15b224b6` [SECP256K1] JNI tests : remove dependency to obsolete DatatypeConverter (Fabien)
- `96c19b148` [TRIVIAL] Cleanup the JNI test file (Fabien)
- `97e7b4ee0` [schnorr] Refactor the signature process in reusable component (Amaury Séchet)
- `954372d1a` [secp256k1] add schnorr sign jni binding (sken)
- `b7be9d9dc` [secp256k1] refactor nativeECDSABuffer to a more generic name (sken)
- `de3a385dc` [secp256k1] add schnorr verify jni binding (sken)
- `539dfda15` [secp256k1] remove unused byte array (sken)
- `4fd3b91ef` [secp256k1] remove guava dep (sken)
- `6d955c645` [secp256k1] fix java secp256k1 test (sken)
- `d10db37f6` Allow for running secp256k1 java build/tests out of tree (Fabien)


Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Axel Gembe
- Cory Fields
- Dagur Valberg Johannsson
- Greg-Griffith
- Peter Tschipper
- TomZ

We have backported an amount of changes from other projects, namely Bitcoin Core, BCHN, BCHD, Flowee and Bitcoin ABC.

Following all the indirect contributors whose work has been imported via the above backports:

- Amaury Séchet
- Andrew Poelstra
- Antony Zegers
- Ben Woosley
- Calin Culianu
- Chris Pacia
- Daniel Edgecumbe
- Daniel Kraft
- DesWurstes
- djb
- Don Viszneki
- Elichai Turkel
- Evan Klitzke
- Fabien
- freetrader
- Gregory Maxwell
- Jan Xie
- Jeremy Rubin
- João Barbosa
- Johnson Lau
- Jonas Nick
- Jonas Schnelli
- Kirill Fomichev
- liuyujun
- Luke Dashjr
- MarcoFalke
- Marko Bencun
- Martin Ankerl
- Matt Corallo
- Mustapha Abiola
- Phillip Mienk
- philsmd
- Pierre Pronchery
- Pieter Wuille
- practicalswift
- roconnor-blockstream
- Roman Zeyde
- Russell O'Connor
- Russell Yanofsky
- Rusty Russell
- Seonpyo Kim
- sken
- Thomas Snider
- Tim Ruffing
- tobiasruck
- Tomas van der Wansem
- Wladimir J. van der Laan
- Yuri Zhykin

