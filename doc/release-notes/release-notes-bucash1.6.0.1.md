Release Notes for Bitcoin Unlimited Cash Edition 1.6.0.1
=========================================================

Bitcoin Unlimited Cash Edition version 1.6.0.1 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a a bug fix release candidate of Bitcoin Unlimited compatible
with the Bitcoin Cash specifications you could find here:

- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/uahf-technical-spec.md (Aug 1st '17 Protocol Upgrade, bucash 1.1.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17 Protocol Upgrade, bucash 1.1.2.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/may-2018-hardfork.md (May 15th '18 Protocol Upgrade, bucash 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18 Protocol Upgrade, bucash 1.5.0.0, 1.5.0.1, 1.5.0.2, 1.5.1.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2019-05-15-upgrade.md (May 15th '19 Protocol Upgrade, bucash 1.6.0.0)

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

If you are upgrading from a release older than 1.1.2.0, your UTXO database will be converted
to a new format. This step could take a variable amount of time that will depend
on the performance of the hardware you are using.

Other than that upgrading from a version lower than 1.5.0.0 your client is probably stuck
on a minority chain and need some manual intervention to make so that it follow the majority
chain after the upgrade. For more detail please look at `reconsidermostworkchain` RPC commands.

Main Changes in 1.6.0.1
---------------------

This is list of the main changes that have been merged in this release:

- Improve Graphene decode rates by padding IBLT
- QA improvements
- remove dead lock in cpu miner
- Fix BIP37 processing for non-topologically ordered blocks
- Documentation improvements
- Fix all stats rpc APIs making them thread safe
- Bump electrs to 0.7
- Add CDV to the set of standard flag
- SV protocol features has been removed
- Update Windows build script
- Improve Graphene decode rates by padding IBLT


Commit details
--------------

- `dc48a8c47` move electrs forward (Andrew Stone)
- `e235393a9` Bump BU version to 1.6.0.1 (#1835) (Andrea Suisani)
- `248c4a10f` ensure that the wallet is fully synced before proceeding with c… (#1833) (Andrew Stone)
- `dcbe42bd7` Track in-progress checks (#1832) (Andrew Stone)
- `78ba99c39` Fix incorrect validation state when connecting many blocks (#1828) (Peter Tschipper)
- `2378531b9` Release the noderef upon disconnect. (#1831) (Peter Tschipper)
- `c959b5c5f` Do not find xthin peers or disconnect non network peers during reindex (#1812) (Peter Tschipper)
- `8c06804fa` fix an early possible free for wallet transaction objects (#1826) (Andrew Stone)
- `f288cdf7b` Use nMaxConnections plus some padding as a limiter on set size (#1824) (Peter Tschipper)
- `67d93ac14` Add a few timing points to ctor.py (#1822) (Peter Tschipper)
- `2056061ac` Use a set to ensure we don't double count peers (#1821) (Peter Tschipper)
- `2bcc58faf` Remove support for requesting a THINBLOCK via getdata. (Peter Tschipper)
- `73f07a33a` fix formatting (Andrea Suisani)
- `3c3386ea9` Prevent deadlock of CPU miner and TX commit thread (Awemany)
- `92f799475` Remove cs_main lock when syncing with wallets (Peter Tschipper)
- `ff22c0a00` Fix BIP37 processing for non-topologically ordered blocks (Mark Lundeberg)
- `a4254bb08` Also ban BitcoinUnlimited SV type peers (Peter Tschipper)
- `99a0fc0c4` spawn electrs in a seperate process group so spurious SIGHUPs don't kill it (Andrew Stone)
- `5d00480f2` move unit tests to use a temporary directory so they don't overwrite an ongoing regtest (Andrew Stone)
- `f07a42bcc` get result in the try then return instead of returning inside the try (Greg-Griffith)
- `bd4d4d301` remove unusued OnPostCommand slot and its corresponding signal (Greg-Griffith)
- `9d83a4554` Declare aux variable only if we are actually using it. (Andrea Suisani)
- `f052d438e` Avoid 1 << 31 (UB) in calculation of SEQUENCE_LOCKTIME_DISABLE_FLAG (practicalswift)
- `793fbab04` mkdir src (Søren Bredlund Caspersen)
- `339ac73da` Delete unused files (Søren B. Caspersen)
- `84a50b54b` Fix typo (Søren B. Caspersen)
- `53e4bfe04` Fix typos (Søren B. Caspersen)
- `39a04d20b` Remove old gitian instructions (Søren B. Caspersen)
- `1256c3525` fix a problem where a stat may be deleted but its timeout handler is simultaneously running.  This fix makes all stat APIs thread-safe.  If subsequent performance testing finds that this is inefficient, it is still possible to leave the update operations unlocked and only use the lock in the destructor and the timeout().  This will then rely on the caller to ensure non-simulaniety of the update operations, or risk losing an update.  Also remove unnecessary lock in sendmany. (Andrew Stone)
- `a8a886a46` Disable eletrumserver unit tests if boost version is lower than 1.65 (Andrea Suisani)
- `4efd0499e` [electrum] Make host cfg independent of port (Dagur Valberg Johannsson)
- `15422617a` [util] Add UnsetArg (Dagur Valberg Johannsson)
- `a9a83d152` Bump electrs to v0.7.0 (Dagur Valberg Johannsson)
- `fc374a9df` [rpc] Fix electrum monitor parsing of long ints (Dagur Valberg Johannsson)
- `fdc357c71` document return value for EraseOrphanTx() (Peter Tschipper)
- `5572db1d6` Get the new iterator when erasing from the map; (Peter Tschipper)
- `54c45af55` Erase orphans before enqueuing them. (Peter Tschipper)
- `39771b66a` Cleanup CVariableFastFilter (#1790) (bissias)
- `26b45a59f` travis: Properly cache and error on timeout (MarcoFalke)
- `1c347943d` travis: Use absolute paths for cache dirs (MarcoFalke)
- `8e9e931da` travis: Fix caching issues (MarcoFalke)
- `e7cc45942` Add --enable-debug configure flag to one of the travis jobs (Andrea Suisani)
- `36fae130f` [travis] fix unit tests execution (Andrea Suisani)
- `4206803fc` Add CHECKDATASIG to script standard flags (#1750) (Andrea Suisani)
- `6f641d65c` Remove initializtion of nLastBlockSize in the requestManager constructor (#1792) (Peter Tschipper)
- `c7e04b36d` Ban SV peers by checking the user agent string (Peter Tschipper)
- `d2adbccbd` Add Last Block Size to the Debug UI (#1785) (Peter Tschipper)
- `13a1f8641` [travis] fix unit tests execution (Andrea Suisani)
- `3a8746f58` Change May 2019 activation code to work by block height (#1780) (Andrea Suisani)
- `0b77a6da7` Update testnet and mainnet checkpoints list (#1770) (Andrea Suisani)
- `7fc875662` Fix bug in limitfreerelay (#1782) (Peter Tschipper)
- `9377538d4` The last of the thinrelay refactors (#1755) (Peter Tschipper)
- `75d7015a6` Initialize struct *QuickStats fields to 0 upon construction (#1768) (Andrea Suisani)
- `8aa2be92b` Replace DbgAsserts() with if statements when checking peers connected (#1777) (Peter Tschipper)
- `a3397252b` Remove unused var in graphene.h and double inclusion in bitcoind.cpp (#1778) (Andrea Suisani)
- `c6d80834c` fix inclusion order for librsm (Greg-Griffith)
- `2e26cc4b3` remove lock tracking, rsm is guarenteed to not deadlock itself (Greg-Griffith)
- `c425c92b8` add missing $(LIBRSM) include to bitcoin cli, tx, and miner (Greg-Griffith)
- `1cefad2c7` replace NULL with nullptr (Greg-Griffith)
- `fe82c083a` add missing header include, fix formatting (Greg-Griffith)
- `78bccf70f` add rsm to lockstack (Greg-Griffith)
- `e5c357242` A few relatively minor thinrelay changes (#1754) (Peter Tschipper)
- `f9b11eec1` Cleanup finalize node (#1753) (Peter Tschipper)
- `c9afcd264` Make excessiveblocksize parameter and setexcessiveblock RPC command conistent (#1747) (Andrea Suisani)
- `6e297ef47` remove old gitian build instructions, we have docker for this now (#1774) (Griffith)
- `5f1267ef8` If a block fails acceptance then set the fProcessing flag back to false (#1776) (Peter Tschipper)
- `d35fd66fb` Remove sv features (#1722) (Griffith)
- `c1ba66b9d` properly release cnode refs upon shutdown (#1775) (Andrew Stone)
- `0882bb6c2` Cleanup and optimize commitq entry (#1732) (Peter Tschipper)
- `616061aa4` Remove bitcoinCashForkBlockHash unused variable. (#1765) (Andrea Suisani)
- `6790898cb` Add BU security policy document to the repo. (#1771) (Andrea Suisani)
- `b9e3036af` Add BCH protocol upgrades timeline (#1773) (Andrea Suisani)
- `cc50e5ec3` remember git clone, and some links (#1772) (Søren Bredlund Caspersen)
- `bfa46bb03` Reduce the scope of write lock on cs_mapBlockIndex in AcceptBlock (#1769) (Andrea Suisani)
- `feb28baf6` Add syncronisation point to wallet.py when doing maintenance tests (#1766) (Peter Tschipper)
- `8d8b3b69d` Tidy up notify.py and add one test at the end (#1763) (Peter Tschipper)
- `ba11685ea` Use nullptr instead of NULL for all files in src/ (#1703) (Andrea Suisani)
- `87e1ac3b8` Update copyright date on all file under src/ (#1702) (Andrea Suisani)
- `9429ce98f` [UI] Save column width/sort for peers tables on debug dialog (#1762) (Justaphf)
- `9e6bcd0f4` Make sure to lock cs_mapBlockIndex when accessing nStatus or nSequenceId (#1715) (Peter Tschipper)
- `08cd0d8d6` Do not relay transactions if the bloom filter is empty (#1758) (Peter Tschipper)
- `f71e839a5` Reduce the message max multiplier (#1743) (Peter Tschipper)
- `ddd9ee4a5` Add a few asserts (#1752) (Peter Tschipper)
- `bafe4d8aa` fix issue where wallet.py does not actually run the maintenance tests (#1756) (Griffith)
- `d9b487fb3` Fix for license not being valid (#1757) (Marius Kjærstad)
- `9577b373a` Update COPYRIGHT_YEAR in clientversion.h to 2019 (#1759) (Marius Kjærstad)
- `2fb7b3bad` Avoid triggering undefined behaviour in base_uint<BITS>::bits() (practicalswift)
- `6a84749c5` pull rsm 1.0.1 from upstream (#1720) (Griffith)
- `3bc0f7285` on thread group deconstruct interrupt and join threads instead of detach (#1742) (Griffith)
- `9fafa51a5` Update validation time for re-requested txns (#1744) (Peter Tschipper)
- `ae9cea5bd` Refactor Graphene out of CNode (#1708) (Peter Tschipper)
- `b5749f04f` add missing header ifdef (#1749) (Griffith)
- `de7d7e711` [syntax only refactor] rename hash.h to hashwrapper.h because there is a hash.h in secp256k1. (#1748) (Andrew Stone)
- `b4b960f83` remove boost from tweaks (#1746) (Andrew Stone)
- `35e112e61` Set to nullptr after delete (#1719) (Andrea Suisani)
- `121fe6bcf` Better dbcaching for giga-net  (#1738) (Peter Tschipper)
- `987df4713` Disable the UI versionbits warning message (#1733) (Peter Tschipper)
- `318d4cc4b` [elects] Bump to version 0.6.1 (#1730) (dagurval)
- `c4681e17b` change READLOCK to WRITELOCK (#1740) (Griffith)
- `fa2623d0c` Add a couple of more syncronisation  points to ctory.py (#1737) (Peter Tschipper)
- `588f8fc87` check for DISCONNECT_UNCLEAN instead of !fClean (#1741) (Griffith)
- `6cd592ef6` Do not re-request a block if it is currently being processed (#1694) (Peter Tschipper)
- `182ab00e2` Solve a few performance issues in SendMessages() (#1736) (Peter Tschipper)
- `3807f9c0f` fix the error we are looking for (Greg-Griffith)
- `4a26c1252` Increase the max dbcache size from 16GB to 32GB (Peter Tschipper)
- `e136a065b` adjust wallet.py test to account for a change in param conversion (Greg-Griffith)
- `2453b3c1a` disable invalid param type check, param types now validated serverside (Greg-Griffith)
- `3bda46cd5` remove getrawtransaction duplicate entry (Greg-Griffith)
- `296c38454` add missing params set (Greg-Griffith)
- `3c0a50cfc` add rpc param conversion to the server side execute (Greg-Griffith)
- `d8f9c7dcc` Remove check for CSV transactions in accepttomemorypool (Peter Tschipper)
- `cafa11eff` Disconnect instead of ban when we exceed request limit   for thin objects. (Peter Tschipper)
- `be919d385` Simplify the code (Peter Tschipper)
- `dba894735` Properly increment the nNumRequests counter. (Peter Tschipper)
- `2c8042e9f` Fix statistics for time to validate a compact block (Peter Tschipper)
- `998fc1609` Properly initialize CVariableFastFilter members in default constructor (#1713) (bissias)
- `def16695a` [Build Scripts] - Update Windows build scripts wget dependency to v1.20.3 (#1707) (Justaphf)
- `1dd7470ed` add an environment variable that allows you to push configuration into every bitcoin.conf file generated by the test framework (#1686) (Andrew Stone)
- `954532d76`  [rpc] Improve getelectruminfo (#1692) (dagurval)
- `bf78bbf34` Improve reliability of ctor.py (Peter Tschipper)
- `a4ab07e80` Use $(top_srcdir) instead of ${PWD} to determine current path in electrs target (Andrea Suisani)
- `75108762e` Remove redundant NULL checks after new (practicalswift)
- `1c5ed2c6f` Remove redundant check (!ecc is always true) (practicalswift)
- `c1bf7743f` Remove duplicate uriParts.size() > 0 check (practicalswift)
- `132168929` Avoid NULL pointer dereference when _walletModel is NULL (which is valid) (practicalswift)
- `edcdd83b8` Remove find by tx hash method from CBlockHeader (Andrea Suisani)
- `052e83abb` Remove outdated comments (Peter Tschipper)
- `70b8f9378` Remove unnecessary cs_main lock from rest.cpp (Peter Tschipper)
- `0ee456714` add param conversion for getrawtransactionssince (#1711) (Griffith)
- `541268568` Update forkscsv_tests.cpp (Peter Tschipper)
- `d28e72b2a` Add bip68 activation heights to chainparams.cpp (Peter Tschipper)
- `19ee0d3f2` Add LOCK for inode.cs_vSend to access inode.vAddrToSend (Andrea Suisani)
- `a0c24a3a4` Add READLOCK around mapOrphanTransactions and mapOrphanTransactionsByPrev (Andrea Suisani)
- `b86223c85` Remove unused variable in unlimited.cpp (Andrea Suisani)
- `b53e56573` Check nVersion before sending out sendcmpct p2p message: fixes #1701 (Peter Tschipper)
- `a8a74e292` Fix comment about s in schnorr sigs (Florian)
- `cf405d7fa` [wallet] Close DB on error (Karl-Johan Alm)
- `e134dac26` Remove nonnull warning when calling secp256k1_schnorr_sign with NULL noncefp (Florian)
- `45776c567` Add schnorr verify benchmark (Florian)
- `a3c3d9991` Add schnorr sign benchmark (Florian)
- `39cdcb0c4` fix electrs documentation link (#1699) (Griffith)
- `a2678f575` Fix Qt's rcc determinism for depends/gitian (ARM) (#1695) (Andrea Suisani)
- `149aed147` Break up script_build() in script_tests.cpp (Peter Tschipper)
- `677899563` It seems we are ready to release final BUcash 1.5.1 (#1535) (Andrea Suisani)
- `57276b647` ppa update (#1529) (Søren Bredlund Caspersen)
- `34d548a75` Add a new parameter to define the duration of Graphene and Thinblock preferential timer. (#1531) (Andrea Suisani)
- `18ba7f446` Switch ctor on earlier in the init process if we are on the BCH chain (Andrea Suisani)
- `b1890a645` Remove a useless assert in PruneBlockIndexCandidates() (Andrea Suisani)
- `f231d566c` Add Graphene specification v1.0 (#1522) (Andrea Suisani)
- `e5c6f9395` Improve Graphene decode rates by padding IBLT (#1426) (bissias)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Awemany
- George Bissias
- Dagur Valberg Johannsson
- Greg-Griffith
- Justaphf
- Marius Kjærstad
- Peter Tschipper
- Søren Bredlund Caspersen

We have backported an amount of changes from other projects, namely Bitcoin Core and Bitcoin ABC.

Following all the indirect contributors whose work has been imported via the above backports:

- Florian
- practicalswift
- Mark Lundeberg
- Karl-Johan Alm
- MarcoFalke

