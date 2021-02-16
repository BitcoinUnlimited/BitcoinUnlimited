Release Notes for BCH Unlimited 1.9.1
======================================================

BCH Unlimited version 1.9.1 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/issues>

This is a main new release of BCH Unlimited compatible with the upcoming protocol upgrade of the Bitcoin Cash network. You could find a detailed list of all the specifications here:

- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/uahf-technical-spec.md (Aug 1st '17, ver 1.1.0.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17, ver 1.1.2.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/may-2018-hardfork.md (May 15th '18, ver 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18, ver 1.5.0.0, 1.5.0.1, 1.5.0.2, 1.5.1.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2019-05-15-upgrade.md (May 15th '19, ver 1.6.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2019-11-15-upgrade.md (Nov 15th '19, ver 1.7.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2020-05-15-upgrade.md (May 15th '20, ver 1.8.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2020-11-15-upgrade.md (Nov 15th '20, ver 1.9.0, 1.9.0.1, 1.9.1)

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

Main Changes in 1.9.1
-----------------------

This is list of the main changes that have been merged in this release:

- Unbounded Mempool Transaction Chains ([see here](https://docs.google.com/document/d/1Rgc60VipaMnbWksyXw_3L0oOqqCW2j0FKtENlqLgqYw/edit for more details)
- Add DSPproof at wallet UI level
- Add zmq notifications for dsproofs
- Fix Compact Blocks to work with big blocks
- Compatibility with [ElectrsCash 3.0.0](https://github.com/BitcoinUnlimited/ElectrsCash/blob/master/RELEASE-NOTES.md)
- Improve ElectrsCash test integration
- Add support for testnet4 and scalenet
- Block finalization
- Locking and scalability improvements

Commit details
--------------

- `b6056a5a4` Fix BIP34 consensus difference between BCHN & Core and BU (Calin Culianu)
- `c88de9137` fixes to improve QA test stability (Andrew Stone)
- `d96d393f1` Remove fOverrideMempoolLimit since it is not used anywhere (ptschip)
- `24ba1bc12` Fix GuessVerificationProgress when checkpoints are enabled. (ptschip)
- `032ae7116` Minor CheckQ locking changes (Andrew Stone)
- `69b00b3c7` Disconnect peers if they give us a bad header at a checkpoint (ptschip)
- `faf160157` Do not use GetHeight or nVersion before the BIP34 soft fork enforced nVersion semantics (Andrew Stone)
- `61ba59a3b` Catch the exception in case basedir has been already created (Andrea Suisani)
- `07e4ee63b` Revert the coins cache leveldb block_size back to 4096 (Peter Tschipper)
- `f1b133116` allow mining candidates to be reused if multiple RPC calls request candidates within a configurable # of seconds.  Only allow a configurable maximum number of candidates to be 'live' at any time (Andrew Stone)
- `3b1d650df` give mapUnconnectedHeaders its own lock (Andrew Stone)
- `6e561ee1a` Add Nov 2020 checkpoints (Andrea Suisani)
- `23fccdc77` Allow a greater range for prefilled transactions in compactblocks (Peter Tschipper)
- `c1c032504` Bump version to 1.9.1.0 (Andrea Suisani)
- `6adecca64` Save orphanpool to disk and load again on startup (Peter Tschipper)
- `10b7d3cb4` fixes ExtVersion docs (Fernando Pelliccioni)
- `baa6fa9fd` Update copyright year (Andrea Suisani)
- `c86dd044c` Fix copyright headers (Andrea Suisani)
- `772576f3c` Update copyright year to 2021 (Marius Kjærstad)
- `89983f144` Fix zmq DSProof notification (Andrea Suisani)
- `be41ab815` No need to run though the loop if wallet is disabled. (ptschip)
- `6890a4003` Remove class ScoreCompare from miner.cpp (ptschip)
- `910784aa5` Fix potential propagation issues when reconstructing thintype blocks (ptschip)
- `c3a4cfc4b` Update hashBlock in GetTransaction() if needed (Andrea Suisani)
- `c79f24b5c` Drop --jsonrpc-import flag when starting ElectsCash (Andrea Suisani)
- `9a551255a` add explorer.bitcoinunlimited.info as our default transaction viewer and validate proper txviewer URLs before letting the user choose them (Andrew Stone)
- `546a94326` Use uint64_t for file sizes, update CBlockFileInfo to use specific byte size integers (Griffith)
- `855e2f429` Workaround for bug in extversion (ptschip)
- `8ce32d848` Make sure to check correctly for known INV types (ptschip)
- `c17fcd667` Add maxtipage to allowed args (Justaphf)
- `81e19bed7` improve nToFetch calculation in the request manager (Griffith)
- `a55d659fc` some fixes for qa test consistency (Andrew Stone)
- `36d70bb10` electrum: Integrate ElectrsCash v3 upgrades (Dagur Valberg Johannsson)
- `e2f972546` electrum: Test for verbose unconfirmed tx (Dagur Valberg Johannsson)
- `495992fff` finish the port.  convert to BU function calls and add some test framework helper functions to make subsequent porting easier.  add all zmq test into the qa runner.  Use different zmq ports so tests can be run simultaneously.  remove unused block attach/unattach notification.  expand interface_zmq.py, add missing bitcoind option.  ZMQ tests are sensitive to race conditions between python asking for a zmq message and bitcoind generating it.  Add wrappers that retry ZMQ receive until a timeout.  Add a P2PKH tx to the wallet and use it for the ZMQ doublespend notification test in zmq_test.py and interface_zmq.py. (Andrew Stone)
- `733f4997a` Upgrade UniValue code in zmq/zmqrpc.cpp (#51) (BigBlockIfTrue)
- `eed220efd` Backport of BCHN 5df3c1d33e RPC: Add new getzmqnotifications method. (Daniel Kraft)
- `1fa143a00` electrum: Test for addresses field in verbose tx (Dagur Valberg Johannsson)
- `7090e786b` start electrs build earlier (Griffith)
- `871f7159c` make QA utility function create_confirmed_utxos operate more efficiently by not generating 101 blocks unless needed, and by utilizing a wider split.  This fixes the mempool_push block sync timeout problem that happens in underpowered QA runs (Andrew Stone)
- `02bf0068b` Test for blockchain.transaction.get_merkle (Dagur)
- `562ab6d8e` electrum: Test more of verbose transaction output (Dagur Valberg Johannsson)
- `af423d801` electrum: Refactor mempool chain tests (Dagur Valberg Johannsson)
- `185e3cdc9` Validate dsproof after we create it and before we forward it (Peter Tschipper)
- `f6f0892e7` Use the dirty flag (Peter Tschipper)
- `49d46e12b` Build development version of ElectrsCash for the dev branch of BU (Andrea Suisani)
- `b087eb8c1` Remove deb pkgs that are already installed in out custom docker image (Andrea Suisani)
- `fa61f1970` Remove `--coverage` when running electrum server tests only (Dagur Valberg Johannsson)
- `c0621f903` [ci] Add electrscash build & testing (Dagur Valberg Johannsson)
- `f1845b9ab` [electrum] Support out-of-source build (Dagur Valberg Johannsson)
- `416bebf53` Avoid updating ban score for whitelisted nodes (Andrea Suisani)
- `0401ecdf2` electrum: Add test for connection limit (Dagur Valberg Johannsson)
- `db9e1e622` electrum: Detect disconnect in electrum framework (Dagur Valberg Johannsson)
- `78901cd4c` Move the calculation of ancestors into updateEntryForAncestors() (Peter Tschipper)
- `aabb4d112` Add the Double Spent status to the transaction description dialog and place a warning icon next to the transaction in the transaction list. (Peter Tschipper)
- `2ba0b9f20` Add the Double Spent status to the transaction description dialog and place a warning icon next to the transaction in the transaction list. (Peter Tschipper)
- `7fbaa4c06` Change the way we trim transactions from the mempool (Peter Tschipper)
- `29e921b03` Fix rpc_tests for manually added bantimes (ptschip)
- `15e8a1f2f` Change the place where we check for nTxPersec < 0 (Peter Tschipper)
- `dea675a90` Implement a block lookahead when receiving data (Peter Tschipper)
- `fcd2ea7fa` Add logging category for tweaks (Justin Holmes)
- `ee3a464dd` Enable electrum server integration on Mac OS (Dagur)
- `c89de9f23` Make the CNode XVERSION variables atomic (Peter Tschipper)
- `24207f1bb` Fix a warning in validation.h (Andrea Suisani)
- `3583d7c99` Add autoconf to the list of prereqs to ubuntu/debian build instructions (Andrea Suisani)
- `98dbdb83d` Add EXTVER in the peers UI to indicate support of EXTVERSION (Peter Tschipper)
- `1c6f7bee9` electrum: Tests for blockchain.address.(un)subscribe (Dagur)
- `b28d10e0e` Turn on XVal by default when mining a new block (Peter Tschipper)
- `dd70b4a4c` Remove old xversion (Griffith)
- `27440feeb` Fix a startup issue where the QT Wallet is getting hung (Peter Tschipper)
- `6a0dda96c` Update seeders domain name server (Andrea Suisani)
- `6c0db6f94` Fix bug in connection logic and tweak ref (Peter Tschipper)
- `e7065fac4` increase max block file size to 2 GB, fix loop when selecting block file (Griffith)
- `50240eca3` Distinguish testnet4 and scalenet icon colors (Justaphf)
- `ed87d7ea6` Rework excessiveblock size and mining block size into chainparams (Griffith)
- `90e2c9b9e` change calculation for max http body size (Griffith)
- `99fb9d630` fix formatting (Griffith)
- `f847c47d6` set maxchecksigs based on default EB value but dont override user tweaks (Griffith)
- `dcd77333e` set a minimum excessive block size for each chain (Griffith)
- `407601663` fix bad comparators in python qa suite (Griffith)
- `95985c4e8` fix scalenet network id string (Griffith)
- `3be452abc` move default mining block size to chainparams, set it for each chain (Griffith)
- `a96c15bdd` move the default excessive block size to chainparams, set for each chain (Griffith)
- `b6bf60f01` Make CFeeRate atomic (ptschip)
- `456a548ba` Update build instructions (Peter Tschipper)
- `5b3f678b9` Add missing lock in keystore.cpp (Peter Tschipper)
- `f60d1a9a4` Update the dev environement build instructions for Windows (ptschip)
- `eac70d692` Check size limits when doing IsTxAcceptable (ptschip)
- `6d5b5b4a1` Add a minimum delay before block finalization (Fabien)
- `47866cb58` [rpc] add getfinalizedblockhash to return the current finalized block (Shammah Chancellor)
- `8f9ebc1de` Only update the finalized state if finalization is turned on. (ptschip)
- `c77ada887` Partially revert 31046fa5f9a843a8ba00c38e5c6d57d8b1c2c1da (Andrea Suisani)
- `424d848f4` `_idx` checks were already made in `_Contains`, just fetch the index (Griffith)
- `a99a59dd7` Add tweak blockchain.maxReorgDepth  rather than using -maxreorgdepth (ptschip)
- `073f8333e` Add a DbgAssert() to check pindex in IsBlockFinalized() (ptschip)
- `09baa1bd5` Create a new scope for cs_main (ptschip)
- `5bfc0c300` make the explanation string for -maxreorgdepth a little easier to understand (ptschip)
- `ee51f2969` Remove accendental line which was cherry-picked into bip68_sequence.py (ptschip)
- `e10a112f4` fix format (ptschip)
- `8e6ff1c63` Add missing cs_main lock when checking pindexFnalized (ptschip)
- `7f09d7804` Prevent auto-finalization from moving backwards (ptschip)
- `019afca74` Do not remove the validity mask for block valid when finalizing (ptschip)
- `3d93a7671` [rpc] add getfinalizedblockhash to return the current finalized block (Shammah Chancellor)
- `7c2595684` Fix the python tests (ptschip)
- `52723403f` Add check for genesis block when finalizing (ptschip)
- `beb3fed9c` Auto-finalize block once they reached a certain depth (by default 10) (Amaury Séchet)
- `e2e23da8f` Remove unnecessary print statements from finalize.py (ptschip)
- `6a5acf948` Remove mining blocks by score (ptschip)
- `2fded1dfa` Fix a typo in 1.9.0.1 rel notes (Andrea Suisani)
- `ad99f5210` Fix finalizeblock.py so that it works with BU code (ptschip)
- `2131c7d8f` MOVE only abc-finalize-block.py -> finalizeblock.py (ptschip)
- `39f8c245e` Fix format (ptschip)
- `d61fcb1d6` Check and set validity bits correctly (ptschip)
- `e212024bc` Fix a varity of complile issues related to the port of finalize block (ptschip)
- `a8435411f` Add an RPC to finalize a block (Amaury Séchet)
- `2aa5fa3d4` WIP: Add scalenet (Justaphf)
- `58939c8bb` [PORT] Add testnet4 IPv6 Seeds from BCHN (Justaphf)
- `c1b836afb` Update allowed args ports for all supported chains (Justaphf)
- `ec6824442` Add separate GUI const (Justaphf)
- `46563a959` Add missing May 2018 & May 2019 fork heights (Justaphf)
- `242ad1cc0` Set May 2020 fork height to 0 (Justaphf)
- `c43f58be2` Fix testnet4 RPC port (Justaphf)
- `6958e9683` added checkpoints and chatxdata for testnet4 (Griffith)
- `baddb1e14` Add seed.tbch4.loping.net testnet4 seed (Axel Gembe)
- `09fe99261` Fix testnet4 activation heights (Axel Gembe)
- `8a716e586` Update testnet4 (Axel Gembe)
- `67b3ddbf6` add testnet4 network sytle for gui (Griffith)
- `61eeae736` Add testnet4 (Jonathan Toomim)
- `f56e6cc88` fixup: allowed_args formatting (Justaphf)
- `496ac2174` [PORT] Add testnet4 IPv6 Seeds from BCHN (Justaphf)
- `911c87d0e` Update allowed args ports for all supported chains (Justaphf)
- `afb2b02b6` Add separate GUI const (Justaphf)
- `8bccec000` Add missing May 2018 & May 2019 fork heights (Justaphf)
- `49363a17f` Set May 2020 fork height to 0 (Justaphf)
- `f34f54e3b` Fix testnet4 RPC port (Justaphf)
- `d2ee954bc` added checkpoints and chatxdata for testnet4 (Griffith)
- `97345a054` Add seed.tbch4.loping.net testnet4 seed (Axel Gembe)
- `c14ec899c` Fix testnet4 activation heights (Axel Gembe)
- `aa9e69661` Update testnet4 (Axel Gembe)
- `94f7b3221` add testnet4 network sytle for gui (Griffith)
- `a4e33e9ee` Add testnet4 (Jonathan Toomim)

Credits
=======

Thanks to everyone who directly contributed to this release:


- Andrea Suisani
- Andrew Stone
- Dagur Valberg Johannsson
- Fernando Pelliccioni
- Greg Griffith
- Justin Holmes
- Marius Kjærstad
- Peter Tschipper

We have backported an amount of changes from other projects, namely Bitcoin Core and Bitcoin Cash Node.

Following all the indirect contributors whose work has been imported via the above backports:

- Amaury Séchet
- Axel Gembe
- BigBlockIfTrue
- Calin Culianu
- Daniel Kraft
- Jonathan Toomim
- Fabien
- Shammah Chancellor
