Release Notes for BCH Unlimited 1.9.0.1
======================================================

BCH Unlimited version 1.9.0.1 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/issues>

This is a minor bugs fix release of BCH Unlimited compatible with the upcoming protocol upgrade of the Bitcoin Cash network. You could find
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

Main Changes in 1.9.0.1
-----------------------

This is list of the main changes that have been merged in this release:

- fix a 1.9.0 issue reported on macos
- fixes for boost v1.73+
- add support to build ElectrsCash on macos
- xversion fixes and improvements
- disable coin freeze feature
- better handling of block header validation
- various DS proof fixes


Commit details
--------------

- `e4f648430` Fix merge conflict due to the last rebase (Andrea Suisani)
- `1c48b9d28` Ensure new DSProof is valid (Calin Culianu)
- `bc4114f26` Rename getters for private Spender(s) attrbutes in DoubleSpendClass (Andrea Suisani)
- `8b4eddad2` Restricted size of pushData to 520 (Calin Culianu)
- `00344be7c` Fixed a bug in addOrphan() (Calin Culianu)
- `bcefff161` Enforce some message size limits for DSPROOF messages (Calin Culianu)
- `8ab4d5d72` DSProof: Various nits and fixups (Calin Culianu)
- `8cc157114` Remove orphans (if any) when adding a new dsproof (ptschip)
- `03060d79c` Replace int with NodeId when using a peerId (ptschip)
- `3ef2363b9` Repoint build status badge from Travis to Gitlab (Justaphf)
- `d01385f31` Graphene version negotiation at connect time only (Andrew Stone)
- `ea71bf0ee` Fix compile warning in bitcoin-miner.cpp (Peter Tschipper)
- `af09b749d` Remove unnecessary printf from sigopcount_tests (ptschip)
- `01ff735d4` Update runtime error message when block size is exceeded (Peter Tschipper)
- `05ff861cbf` git ignore src/pull-tester/ctorout.txt (ptschip)
- `a0b89b1332` fix univalue kvp insertion complexity. now log(N) instead of N^2 (Griffith)
- `15de8e7837` move CheckAgainstCheckpoint call into contextualcheckblockheader (Griffith)
- `0d6df76a41` only send extversion message if both peers are using the protocol (Griffith)
- `2e7f74083a` Use GetLastCheckpoint() (ptschip)
- `9f46b150b4` Re-enable GetLastCheckpoint() (ptschip)
- `675e2dd665` Remove TestConservativeBlockValidity (Griffith)
- `9f09e83cae` add java function to verify a message's signature (Andrew Stone)
- `ee4b02cd31` change XVersionMap from unorderded_map to map for better performance (Griffith)
- `2310695b90` Disable the coin freeze feature (Peter Tschipper)
- `10fc9b8d8f` Fix printf warning in the cpu miner (Andrea Suisani)
- `86d6ef8ffd` fix ubuntu 20 warning: use default copy op and copy constructor rather then defining the constructor to call the copy op and not defining the copy op (Andrew Stone)
- `1988fa6466` small cherrypick fix (Andrew Stone)
- `e119940172` qt: Replace objc_msgSend with native syntax (Hennadii Stepanov)
- `74e8d720fe` Use Qt signal for macOS Dock icon click event (Hennadii Stepanov)
- `31046fa5f9` fixes for boost v1.73 (Andrew Stone)
- `fd90d2ddbf` Fix compiling error on OSX when using --enable-debug (Andrea Suisani)
- `04fcedd068` build: Add options to override BDB cflags/libs (Andrea Suisani)
- `d355b7de91` Use a different critical section for vNodesDisconnected. (Peter Tschipper)
- `ccce53d990` [gitian] Avoid to build rust for linux 32 bits (Andrea Suisani)
- `e54ba72977` qt: include QPainterPath header (redfish)
- `93b531e6a0` Properly scope mutex variable used with condition_variable wait_until (Andrea Suisani)
- `428dd80dfb` Add a missing atomic include (Andrea Suisani)
- `b1f2424a02` Turn the bench compile off if tests are disabled (ptschip)
- `08710b9bef` [electrum] Add OSX support for electrs build script (Dagur Valberg Johannsson)
- `a17dc00fe6` Clear the availableFrom list when processing a transaction (Peter Tschipper)
- `0f1e849adc` Bump ElectrsCash HEAD commit hash (Dagur Valberg Johannsson)
- `c641b8c0f7` Address a few issues after the 1st round of review (Andrea Suisani)
- `0891cf91f9` regressions: fix may2020 fork time and use else if not just if (Andrew Stone)
- `b5a3724cd5` Change `electrum.blocknotify` default to true (Dagur)
- `02c6533dca` cache TXN concat xversion config param for efficiency, like many other xversion configs are cached, rather then looking it up in a map repeatedly (Andrew Stone)
- `9f9af7b87b` protect xmap when assigning values in set_u64c() (ptschip)
- `8b1641bfd5` Remove 'continue' if we have found an orphan and reclamed it. (Peter Tschipper)
- `664dc175f2` Remove rpc-tests2.py (Andrea Suisani)
- `bd15887324` Bump rust to ver 1.45.2 (Andrea Suisani)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Dagur Valberg Johannsson
- Griffith
- Peter Tschipper
- redfish
- Justing Holmes (Justaph)

We have backported an amount of changes from other projects, namely Bitcoin Core and Bitcoin Cash Node.

Following all the indirect contributors whose work has been imported via the above backports:

- Hennadii Stepanov
- Calin Culianu
