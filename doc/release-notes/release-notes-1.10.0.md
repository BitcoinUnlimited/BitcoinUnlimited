Release Notes for BCH Unlimited 1.10.0
======================================================

BCH Unlimited version 1.10.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/issues>

This is a major release of BCH Unlimited compatible with the upcoming May 15th, 2022, protocol upgrade of the Bitcoin Cash network:

- https://upgradespecs.bitcoincashnode.org/2022-05-15-upgrade/

The following is detailed list of all previous protocol upgrades specifications:

- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/uahf-technical-spec.md (Aug 1st '17, ver 1.1.0.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17, ver 1.1.2.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/may-2018-hardfork.md (May 15th '18, ver 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18, ver 1.5.0.0, 1.5.0.1, 1.5.0.2, 1.5.1.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2019-05-15-upgrade.md (May 15th '19, ver 1.6.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2019-11-15-upgrade.md (Nov 15th '19, ver 1.7.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2020-05-15-upgrade.md (May 15th '20, ver 1.8.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2020-11-15-upgrade.md (Nov 15th '20, ver 1.9.0, 1.9.0.1, 1.9.1)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2021-05-15-upgrade.md (May 15th '21, ver 1.9.2)

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

Main Changes in 1.10.0
-----------------------

This is list of the main changes that have been merged in this release:

- CHIP-2021-03: [Bigger Script Integers](https://gitlab.com/GeneralProtocols/research/chips/-/blob/master/CHIP-2021-02-Bigger-Script-Integers.md) increased precision for arithmetic operations (see [discussion](https://bitcoincashresearch.org/t/chip-2021-03-bigger-script-integers/39)).
- CHIP-2021-02: [Native Introspection Opcodes](https://gitlab.com/GeneralProtocols/research/chips/-/blob/master/CHIP-2021-02-Add-Native-Introspection-Opcodes.md) enabling smart contracts to inspect the current transaction idetails (see [discussion](https://bitcoincashresearch.org/t/chip-2021-02-add-native-introspection-opcodes/307)).
- Bundle with the new ElectrsCash ver 3.1.0 (see [release notes](https://github.com/BitcoinUnlimited/ElectrsCash/blob/master/RELEASE-NOTES.md#310-8-march-2022))[
- [BIP69](https://github.com/bitcoin/bips/blob/master/bip-0069.mediawiki) lexicographical sorting of transaction inputs/outputs for increased transaction privacy. A new option -usebip69=<boolean> has been added to toggle this behaviour. This is enabled by default.

The new consensus rules in the aforementioned CHIPs will take effect once the median time past (MTP) [1] of the most recent 11 blocks is greater than or equal to UNIX timestamp 1652616000.

Commit details
--------------

- `229f7056e1` Add May 2022 activation code to the mempool admission process (Andrea Suisani)
- `a7339662f1` [DEV] move static outside of function to avoid multi-thread issues with static var initialization (Andrew Stone)
- `ac6f85c22e` Pin ElectrsCash to version 3.1.0 (Dagur Valberg Johannsson)
- `c05870b54a` Show proper output index in transaction details (QT) (Griffith)
- `c8be02841a` Bump version to 1.10.0 and build date to jan 1, 2022 (Andrew Stone)
- `93cb8b0508` fix small test issues (Andrew Stone)
- `b29c0c71d2` build depends with --std-c++17 flag (Andrea Suisani)
- `bf04876e45` Use clang 10 in the fot build-debian-clang CI task (Andrea Suisani)
- `6cd564a4c3` [depends] protobuf: drop -Werror from CPPFLAGS (Andrea Suisani)
- `89322049b6` Switch back to osx SDK 10.14 (Andrea Suisani)
- `62f2157f32` Revert "depends: only use dbus with qt on linux" (Andrea Suisani)
- `163718c4d1` build: pass -dead_strip_dylibs to ld on macOS (fanquake)
- `dab8d616f2` depends: don't use OpenGL in Qt on macOS (fanquake)
- `71c383db38` depends: only use dbus with qt on linux (fanquake)
- `c801117ccf` depends: qt: Fix C{,XX} pickup (Carl Dong)
- `58669d5a63` depends: qt: Fix LDFLAGS pickup (Carl Dong)
- `8b5a06e025` build: remove unnecessary qt xcb patching (fanquake)
- `58f437cde8` build: remove unnecessary macOS qt patching (fanquake)
- `04ef6a4890` depends: qt: Fix C{,XX}FLAGS pickup (Carl Dong)
- `66c867d6db` depends: disable unused Qt features (fanquake)
- `d3b2dcfbce` doc: remove line numbers from qt package links (fanquake)
- `c2c971dbc9` doc: fix typo in bitcoin_qt.m4 comment (fanquake)
- `b7983cee82` build: remove jpeg lib check from bitcoin_qt.m4 (fanquake)
- `6dbdc40c72` build: disable libjpeg in qt (fanquake)
- `387cb9e3b5` depends: Bump QT to LTS release 5.9.8 (THETCR)
- `40903eb45d` depends: qt: Patch to remove dep on libX11 (Carl Dong)
- `677b27ba0f` gitignore: Actually pay attention to depends patches (Carl Dong)
- `428c6a024c` symbol-check: Disallow libX11-*.so.* shared libraries (Carl Dong)
- `dfd5963887` depends: libXext isn't needed by anyone (Carl Dong)
- `890fc4a757` build-aux: Remove check for x11-xcb (Carl Dong)
- `474ac53539` depends: qt: Explicitly stop using Xlib/libX11 (Carl Dong)
- `521ab46d0c` depends: xproto is only directly needed by libXau (Carl Dong)
- `1dc3d3768e` depends: qt: Don't hardcode pwd path (Carl Dong)
- `e0427e55d3` depends: expat 2.2.6 (fanquake)
- `665042e088` Bump macOSX min version to 10.12 (Sierra) and OSX SDK to 10.15 (Andrea Suisani)
- `7ff224d497` Fix formatting (Andrea Suisani)
- `4523086161` Fix deprecation declaration warning in the secure allocator (Andrea Suisani)
- `0941a7a5de` Use standard mutex in init/validation/txmempool.cpp (Andrea Suisani)
- `e585467644` Use std lib for mutex and condition_variable in the mining code (Andrea Suisani)
- `794e879e56` Fix an uninitialized const reference warning (Andrea Suisani)
- `2859fa2ba9` Use standard mutex in transactions admission code (Andrea Suisani)
- `b399f0af2c` Use standard algorith library rather than Qt deprecated counterpart (Andrea Suisani)
- `24c29d65cf` Use standard mutex implementation for signature caches (Andrea Suisani)
- `f718498f2e` Convert blockrelay subsys to use standard mutexes (Andrea Suisani)
- `c694cf6833` Fix a bunch of warnings due mistmatching types in BOOST_CHECK_EQUAL (Andrea Suisani)
- `fb77e878ec` Use standard library mutexes for our synchronization mechanism (Andrea Suisani)
- `e079a035d8` Silence clang warnings in boost asio library (Andrea Suisani)
- `a7f6359f26` Enable C++ 17 for the code base (Andrea Suisani)
- `d5fb5b3b73` resolve most compilation warnings (Griffith)
- `b667d2c280` change SubmitBlock param to a ConstCBlockRef (Griffith)
- `083955a7d7` add sigtype arg to signrawtransaction (Griffith)
- `6462f7227b` Update copyright year to 2022 (Marius Kjærstad)
- `aeab29d5d9` [DEPENDS] Use parallel compilation when building packages (Andrea Suisani)
- `e797427d4a` Make sure we flush when IBD is complete (Peter Tschipper)
- `a4737cad0f` Fix 3 warnings spotted by compiling the source code using clang (Andrea Suisani)
- `b4bb05038f` Fix libbitcoinconsensus dll compilation (Andrea Suisani)
- `df44a55390` [ci] osx depends: upload only needed artifacts (Andrea Suisani)
- `f7f326d378` Remove documents related to unconfirmed txn chain limits (Peter Tschipper)
- `992d9b706e` Fix installation of clang 12 in our CI workflow (Andrea Suisani)
- `69b3b77d19` Tidy up the error message for absurd fees (Peter Tschipper)
- `bd8882601e` use shared_ptr for cblock throughout all validation code, make the cblock const (Griffith)
- `0d19500319` only log network connection failures if debug=net is configured (Griffith)
- `1049204d7e` minor cleanup and bugfixes (Griffith)
- `931660b690` remove IP address check specific to pre 0.2.9 node addr messages (Griffith)
- `c17510106d` Dont allow free transactions into the mempool bug fix (Peter Tschipper)
- `8e989a3504` script interpreter fixes (Griffith)
- `67a18f6b49` Update Qt 5.9.7 URL to point to the new location (Andrea Suisani)
- `dc68b242c2` add may2022 fork activation checks (Griffith)
- `76ff705cc7` return proper change output index for bip69 (Griffith)
- `90d42152e6` BCH Native Introspection (Griffith)
- `a2dbef3438` use bip69 by default in CWallet::CreateTransaction() (Griffith)
- `70ceeb008a` Bigger script ints (Griffith)
- `f02b199622` Fix spurious failure in mempool_tests.cpp (ptschip)
- `006a712b1a` May2022 fork activation code (Griffith)
- `aa91028974` add the config path to cmakelists.txt so that bitcoin-config.h is found (Andrew Stone)
- `b72ae44124` remove 'config' path from all bitcoin-config.h includes.  This prepended path allows an external target directory build to accidentally pick up a residual bitcoin-config.h from a in-tree build.  This happens because external builds still need a -I that pulls in the source tree and the internal bitcoin-config.h is located at src/config/bitcoin-config.h (Andrew Stone)
- `b47648b230` fix use before set error in graphene simultaneous processing check (Andrew Stone)
- `feba5e8aab` cherry pick !2513 to dev (Andrew Stone)
- `30f5cb32d5` fix tests (Fernando Pelliccioni)
- `02ccc0d435` Remove unnecessary file (ptschip)
- `358a99e8bd` Move nextchain antifragile QA fixes to dev (Andrew Stone)
- `6116d98203` Fix miner.cpp when mining priority transactions (ptschip)
- `eb395b35ae` Remove unnecessary cs_main lock in rpc generate() and remove internal bitcoin miner (Peter Tschipper)
- `c67e96cf3d` make the mininode properly handle extversion automatically, and report extversion support in getpeerinfo RPC (Andrew Stone)
- `b8a6c20eec` Do not try to output a public label if this is a coinbase (ptschip)
- `b169f2fa5d` Update dependencies.md to 1.71 not 1.70 (Andrew Stone)
- `8de9b6c637` Remove boost assign (Griffith)
- `ffa7b903c5` Fix libbitcoinconsensus cross-compiling for win32/64 (Andrea Suisani)
- `840430e4bf` build: add PTHREAD_LIBS to LDFLAGS configure output (fanquake)
- `fff9db19cd` build: split PTHREAD_\* flags out of AM_LDFLAGS (fanquake)
- `f95e9551ef` Replace uses of boost date_time with std::chrono (Griffith)
- `96bf3a8151` build: AX_PTHREAD serial 27 (fanquake)
- `0590d912e9` [trivial] Sync ax_pthread with upstream draft (fanquake)
- `e45068c6e7` build,boost: update download url. (fdov)
- `6a908dacea` [build][depends]: Bump boost to 1.71.0 (Andrea Suisani)
- `0f722f22b5` [build] Remove unused boost patches (Andrea Suisani)
- `ef4188f0c7` depends: boost: Refer to version in URL (Carl Dong)
- `2f08ae777e` depends: Consistent use of package variable (Peter Bushnell)
- `d01cf2ec91` depends: fix boost mac cross build with clang 9+ (Cory Fields)
- `83addfb6a0` [depends] boost: update to 1.70 (Sjors Provoost)
- `8358a4750d` run clang-format-12 on all files (BitcoinUnlimited Janitor)
- `26fafb1b7d` update to clang-format-12 (Griffith)
- `bc98daecf3` Validate tx p2p message (Griffith)
- `f2a00b7d8a` Prevent the modal overlay from briefly appearing after chain is synced (Peter Tschipper)
- `4359cdf447` Fix testnet nov2020 height activation (Andrea Suisani)
- `a97724675d` enable zmq on windows builds (Griffith)
- `94d050e083` Remove unnecessary call to CNodeRef destructor (Peter Tschipper)
- `db068a997b` syscall requires missing unistd.h (Griffith)
- `f208400825` Post Upgrade: We can finally remove all references to ancestor and descendant limits (Peter Tschipper)
- `eaf46c647f` Only consider "Up to date" when receiving a recent block, not a header. (Griffith)
- `632987bdf1` Fix help message/option for QA execution wrapper (Andrea Suisani)
- `dbbbbfcafc` regression test for spaces in tweak set (Andrew Stone)
- `355dda4984` Revert "Pin ElectrsCash to version 3.0.0" (Dagur Valberg Johannsson)
- `bf77d68a55` electrum: Add test for `blockchain.utxo.get` (Dagur Valberg Johannsson)
- `57e0b5a6b5` CI: Cache rust artifacts (Dagur Valberg Johannsson)
- `9a793b01d5` qa: Use PortSeed for Electrum server (Dagur Valberg Johannsson)
- `50ae5e5ffb` Order the block requests. (Peter Tschipper)
- `4fc9e5a1f1` [moveonly] Move port seed features to own unit (Dagur Valberg Johannsson)
- `8d7dbbfdec` Simplify by removing duplicate code. (Peter Tschipper)
- `259ad4d6e8` Allow whitespace between "=" when setting tweaks (Peter Tschipper)
- `f9aee75efa` Request manager fixes for downloading blocks during IBD (Peter Tschipper)
- `8d5f261db5` CheckFinalTx is checked inside IsTrusted, no need to call it twice (Griffith)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Dagur Valberg Johannsson
- Fernando Pelliccioni
- Griffith
- Marius Kjærstad
- Peter Tschipper

We have backported a set of changes from Bitcoin Core.

Following all the indirect contributors whose work has been imported via the above backports:

- Carl Dong
- Cory Fields
- THETCR
- fanquake
- fdov
- Peter Bushnell
- Sjors Provoost
