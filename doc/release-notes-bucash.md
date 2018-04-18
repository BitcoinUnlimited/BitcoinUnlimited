Release Notes for Bitcoin Unlimited Cash Edition 1.3.0.0
=========================================================

Bitcoin Unlimited Cash Edition version 1.3.0.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a major release version based of Bitcoin Unlimited compatible
with the Bitcoin Cash specifications you could find here:

https://github.com/bitcoincashorg/spec/blob/master/uahf-technical-spec.md (Aug 1st Protocol Upgrade, bucash 1.1.0.0)
https://github.com/bitcoincashorg/spec/blob/master/nov-13-hardfork-spec.md (Nov 13th Protocol Upgrade, bucash 1.1.2.0)
https://github.com/bitcoincashorg/spec/blob/master/may-2018-hardfork.md (May 15th Protocol Upgrade, bucash 1.3.0.0)



Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

If you are upgrading from a release older than 1.1.2.0, your UTXO database will be converted
to a new format. This step could take a variable amount of time that will depend
on the performance of the hardware you are using.

Downgrade
---------

In case you decide to downgrade from BUcash 1.2.0.1 to a version older than 1.1.2.0
will need to run the old release using `-reindex` option so that the
UTXO will be rebuild using the previous format. Mind you that downgrading to version
lower than 1.1.2.0 you will be split from the rest of the network that are following
the rules activated Nov 13th 2017 protocol upgrade.

Main Changes
------------

- May 15th 2018 protocol upgrade, at the median time past (MTP) time of 1526400000 (Tue May 15 12:00:00 UTC, 2018) this new features will introduced:
	- OP_RETURN data carrier size increases to 220 bytes
	- Increase the maximum blocksize (EB) to 32,000,000 bytes
	- Re-activate the following opcodes: OP_CAT, OP_AND, OP_OR, OP_XOR, OP_DIV, OP_MOD
	- Activate these new opcodes: OP_SPLIT to replace OP_SUBSTR, OP_NUM2BIN, OP_BIN2NUM
- New RPC command listtransactionsfrom
- Add new OP_DATASIGVERIFY (currently disabled)
- Increase LevelDB performance on Linux 32 bit machine (port from Core)
- QA enhancements (port from Core)
- Improve XTHIN machinery by the use of shared txn
- Greatly improve initial blocks download (IBD) performances
- Automatically determine a more optimal -dbcache setting if none provided
- Improve Request Manager functionalities

Commit details
--------------

- `aba7387a3` Fix several tests in script_tests.json (Shammah Chancellor)
- `9e4d451be` fix for #1035: blocknotify (#1037) (ptschip)
- `d8bf8c422` add cumulative chain work to 'getchaintips' (gandrewstone)
- `a51fd31d1` Move requester.sendrequests() to just before the place where we sleep. (ptschip)
- `8e68a25e1` Check blocks and headers explicitly against the checkpoint hash (#1030) (gandrewstone)
- `2f61f0964` Update copyright date on all file under src/ (sickpig)
- `013f64db7` add listtransactionsfrom to the args conversion list (#1031) (gandrewstone)
- `6170b0558` Update wget dependency in native Windows build env setup script (Justaphf)
- `b73e9564e` Bump version to 1.3.0.0 (sickpig)
- `dee4bfbd7` rely exclusively on the request manager for managing txn requests (#1017) (ptschip)
- `f84e54d89` use cs_objDownloader to lock mapBlocksInFlight (#1022) (ptschip)
- `d45cc2381` add changed implementation to spec (gandrewstone)
- `4bf1a8928` switch signature type byte to the end of the pushed signature.  Add additional tests and check exact test return codes. (gandrewstone)
- `f5b105420` add listtransactionsfrom RPC call that behaves like listtransactions probably should have originally ('from' index 0 is the oldest transaction) (gandrewstone)
- `eccd0d7ed` Initilize counter to a random value. (sickpig)
- `cc1028de7` Prevent the premature adjustment of the coins cache size (ptschip)
- `6ebc73ec5` remove duplicate fork activation logic (gandrewstone)
- `f00aeeced` Make both May fork tests have similar names to avoid confusion. (ptschip)
- `b4d386bde` Move activation for EB, MG and data carrier size to UpdateTip (ptschip)
- `7648f0bb7` Use may152018 rather than monolith to describe files and variables (ptschip)
- `1ff2cf290` Fix branding in developer doc (ptschip)
- `43a60fe88` remove unnecessary print statement from python test (ptschip)
- `a769ca2d2` Fix abc-monolith-acitivation.py test failures (ptschip)
- `5f4d34c3c` Set Monolith opcode enable script flag if monolith enabled (Daniel Connolly)
- `0facaf79e` Add support for DIV and MOD opcodes (Jason B. Cox)
- `5136489ff` Add support for SPLIT opcodes (joshuayabut)
- `a5d9cdcb4` Add support for CAT (joshuayabut)
- `e3750749c` Refactored existing monolith_opcodes tests (Jason B. Cox)
- `52d4e1088` Implement support for NUM2BIN (deadalnix)
- `65d5d58d1` Refactor opcode test to reduce redundancy (deadalnix)
- `49d96caee` Add support for BIN2NUM opcode (deadalnix)
- `54db51bc1` Simplify IsOpcodeDisabled (deadalnix)
- `ee18fa742` Add support for AND, OR and XOR opcodes (deadalnix)
- `f60a14bc3` Refactor MinimalizeBigEndianArray t be part of CScriptNum (deadalnix)
- `f74611198` Ensure that CScriptNum is self contained. (deadalnix)
- `91f92cf0e` Add function to trim leading array zeros maintaining MSB. (Jason B. Cox)
- `15b1bd10a` Pull minimal check out of CScriptNum constr. into IsMinimalArray (Shammah Chancellor)
- `33fe10340` Add helper function for disabled opcodes (Shammah Chancellor)
- `ccc1ffba7` Cleanup scriptflags in unit tests (ptschip)
- `4171184b3` Add a flag to gate opcodes activation for the new HF (deadalnix)
- `ce1472782` Explode disabled opcode if into a switch (deadalnix)
- `c7b05822e` Add missing test cases in script_tests.cpp (deadalnix)
- `143bab69a` Prepare for re-enabled opcodes for the May 2018 protocol upgrade (Daniel Connolly)
- `1a9c9cc6d` Fix bug in ClearOphanCache() (ptschip)
- `a281657b5` Use CBlockRef in other places (ptschip)
- `24c8b0395` Use p for pointer notation to signify these are shared pointers to blocks (ptschip)
- `be573930d` Introduce CBlockRef typedef and MakeBlockRef() and use them. (ptschip)
- `22b59f60d` add -conf path before launching bitcoind (Samuel Kwok)
- `eb2d3a661` control which config params add to bitcoin.conf (Samuel Kwok)
- `3d674c084` switch from LogPrint to LOG message (ptschip)
- `190a3442d` Increase LevelDB max_open_files unless on 32-bit Unix. (Evan Klitzke)
- `d99f72021` In unlimited.cpp reference mapOrphanTransations by the orphanpool singeton (ptschip)
- `efae8487a` Update the calculation of maxAllowedSize to reflect shared pointer use (ptschip)
- `2bc6211b9` Add the size of the shared txn pointers to the orphanpool size (ptschip)
- `8507ded63` Make unit tests work with the new orphan pool singleton (ptschip)
- `7ff80f653` Add comments to orphanpool.h (ptschip)
- `ea8ae51a9` Use emplace for inserting elements into the orphan pool (ptschip)
- `cedff4dc5` Move orphan functions out of main.cpp (ptschip)
- `8b659d6e5` Use shared pointers in the orphan cache (ptschip)
- `afe8c8f8e` Lower the maximum size a thinblock can be before we stop processing. (ptschip)
- `ec32856d1` Use CTransactionRef in mapMissingTx for xthins (ptschip)
- `f85091585` Start using CTransactionRef when reconstructing an XTHIN (ptschip)
- `9b3c684a0` Fix GetTransaction() (ptschip)
- `c3be5619b` Introduce convenience type CTransactionRef (sipa)
- `767dbb083` Make CBlock::vtx a vector of shared_ptr<CTransaction> (sipa)
- `bb1002382` Add deserializing constructors to CTransaction and CMutableTransaction (sipa)
- `b8a86d887` Add serialization for unique_ptr and shared_ptr (sipa)
- `4ae8089fb` Move mapBlocksInflight to the request manager (ptschip)
- `42d7d43d7` If a node disconnects then reset lastrequesttime for any blocks in flight (ptschip)
- `8745a932c` Use C++11 range based loops and nullptr's (ptschip)
- `cb86320af` Use emplace for inserting a new blockvalidationthread entry (ptschip)
- `8ec03a48e` Remove references to requester singleton in RequestManager.cpp (ptschip)
- `2c6eab70e` Enforce the use of Qt5 at configure script level (sickpig)
- `da27ab3ea` Append CLIENT_VERSION_BUILD to Apple's CFBundleGetInfoString (sickpig)
- `9dd8b90dc` Properly set up VERSION var for the win installer (sickpig)
- `62ea24cf7` Add CLIENT_VERSION_BUILD to AC_INIT (sickpig)
- `75df382d6` add enable/disable to datasigverify and count sigops. (gandrewstone)
- `f5e8c0045` Clear warn in QT and getinfo if initial conditions is not valid anymore (#993) (sickpig)
- `330553f4a` Partial port of XT PR #319 from dagurval/qa-cherries (dgenr8)
- `442805625` Port XT #315 from dagurval/28-01-httpserver (dgenr8)
- `ba3423edf` Port Core #8166: src/test: Do not shadow local variables (laanwj)
- `edeb49eab` Ccorrect human-readable fork time in comment (dgenr8)
- `8992e345f` Port Core #7888: prevector: fix 2 bugs in currently unreached code paths (laanwj)
- `3d6b95491` Kill insecure_random and associated global state (laanwj)
- `b2ee649e7` fPreferHeaders should be false when initialized (ptschip)
- `d855c2883` Remove parameter nodeid from InitializeNode() (ptschip)
- `4b9598788` Add a couple of DbgAsserts for if state is NULL (ptschip)
- `849ef6163` Switch from fSyncStartTime to nSyncStartTime (ptschip)
- `26942665c` Use a proper constructor for CNodeState (ptschip)
- `ef79dea10` Switch from pnode to nId (ptschip)
- `28e4929f4` Initialize all variables for CNodeState (ptschip)
- `924c27a8b` Update May 15, 2018 fork activation time as per spec draft (#987) (sickpig)
- `829437ee5` Allow -rpcconnect in bitoin.conf, Fixes #990 (ptschip)
- `d8aecc78a` Fix compiler warning on uint64_t to int comparison (ptschip)
- `954ae6b40` [consensus] Pin P2SH activation to block 173805 on mainnet (John Newbery)
- `a43d77ab3` Update chainparams for regest so that BIP34 is activated (ptschip)
- `b407feb88` Consensus: Remove ISM (NicolasDorier)
- `b1853eb6d` Pull script verify flags calculation out of ConnectBlock (Matt Corallo)
- `22192741f` Move checking of blockdownload timeout to the request manager (ptschip)
- `f7f227e72` Fix for rollbackchain() (#994) (ptschip)
- `52238c64a` Remove dead code related to SizeForkExpiration consensus parameter (sickpig)
- `c1f201405` clean up accepnonstdtxn flag by removing redundant global and making chainparams API correct (gandrewstone)
- `f7cb6ffbb` [IBD patch 4] Deprioritise slower downloaders (#970) (ptschip)
- `dbe128827` add OP_RETURN to list of imported opcodes since we use it (#985) (gandrewstone)
- `715aabfc1` Automatically determine a more optimal -dbcache setting if none provided (#948) (ptschip)
- `e9c49ff65` sync: Remove superfluous assert(..) (Awemany)
- `a48397293` update datasigverify as per feedback (gandrewstone)
- `ccd8e889f` move spec to be standalone rather than a pull request in the re-enable op_codes spec (gandrewstone)
- `0aa71ea70` Update QT build instructions (ptschip)
- `3bf63aece` fix copyright date, and remove inapplicable comment (gandrewstone)
- `77969c13d` Use references in range based for loops (ptschip)
- `3b0359633` Make sure lock on cs_main isn't held before aquiring semaphore grant (sickpig)
- `b88136e25` Add ability to assert a lock is not held in DEBUG_LOCKORDER (Matt Corallo)
- `0ba50e39d` Use C++11 nullptr keyword rather than NULL macro (sickpig)
- `b21911c74` Remove using namespace std from thinblock.cpp (sickpig)
- `24b645189` Substitute BOOST_FOREACH with c++11 range-based for loop (sickpig)
- `11852363b` Fix comments for some xthin stat gathering methods (sickpig)
- `b11e9d7e4` remove unnecessary arg (gandrewstone)
- `925ced865` add ability to specify different binary files/directories for each client (gandrewstone)
- `6e8564a87` basic may 2018 fork infrastructure (gandrewstone)
- `6ffb81e6b` ensure that the subversion and miner coinbase is updated when EB or AD is updated (gandrewstone)
- `f12ccf059` Use Logging namespace to access LogAcceptCategory (sickpig)
- `a1d26e83e` Add a few ui messages to startup when opening databases (ptschip)
- `c4db28f24` Makefile.am: Typo fix that broke `make clean` rule (awemany)
- `1bf53307c` OP_DATASIGVERIFY reference implementation (gandrewstone)
- `b39dd57ed` Remove the redundantblocksize argument from HandBlockMessage (ptschip)
- `9c4414d20` Use GetBlockSize() in place of ::GetSerializedSize (ptschip)
- `0394fc695` Create block method GetBlockSize() (ptschip)
- `a28ee85d3` Scripts to help automate first-time setup of the mingw dev environment on Windows. (#85) (Justaphf)
- `fe425dc63` Explicitly mark fall-through cases (sickpig)
- `dcf43cf9d` change to pointer type (ptschip)
- `c08e038d5` convert CBlock to a shared pointer to limit copying of a block (gandrewstone)
- `e8e1d02e8` Fix warnings on ReadOrderPos and WriteOrderPos (sickpig)
- `b3ca26c44` Only run data gathering  code if BENCH logging is enabled. (ptschip)
- `9d38d55c8` Change log message from LogPrintf to LOG(NET, ... (ptschip)
- `7e6db6810` net: initialize socket to avoid closing random fd's (theuni)
- `466d14aef` If there is nothing to trim then do not continue (ptschip)
- `c2f41c97d` Prevent potential integer underflow of nTrimHeight (ptschip)
- `49243d0be` Replace BOOST_FOREACH and NULL, in SendMessages, with C++11 notation (ptschip)
- `4164aa386` Remove duplicate section of code (ptschip)
- `c0f806f4b` change mempool to use a non-recursive shared lock (gandrewstone)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- awemany
- Justaphf
- Peter Tschipper
- Pieter Wuille
- Samuel Kwok
- Tom Harding

We have backported an amount of changes from other projects, namely Bitcoin Core, Bitcoin ABC and Bitcoin XT.

Following all the indirect contributors whose work has been imported via the above backports:

- Amaury Séchet
- Wladimir J. van der Laan
- Shammah Chancellor
- Matt Corallo
- NicolasDorier
- Cory Fields
- Daniel Connolly
- Evan Klitzke
- Jason B. Cox
- John Newbery
- joshuayabut
