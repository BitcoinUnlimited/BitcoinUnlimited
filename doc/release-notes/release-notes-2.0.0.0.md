Release Notes for BCH Unlimited 2.0.0
======================================================

BCH Unlimited version 2.0.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/issues>

This is a major release of BCH Unlimited compatible with the upcoming May 15th, 2023, protocol upgrade of the Bitcoin Cash network:

- https://upgradespecs.bitcoincashnode.org/2023-05-15-upgrade/

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
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2022-05-15-upgrade.md (May 15th '22, ver 1.10.0)


Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

Main Changes in 1.10.0
-----------------------

This is list of the main changes that have been merged in this release:

- Bump rostrum to v8.1.0 ([2699](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2699))
- Activation code ([2681](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2681))
- Min tx size to 65 bytes ([2684](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2684))
- Enforce nVersion in consensus ([2685](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2685))
- Chipnet ([2693](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2693)) and ([2697](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2697))
- P2SH_32 ([2687](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2687))
- CashToken (see ([this](https://github.com/bitjson/cashtokens) specification document for more details)):
  - Interpolator (new opcodes, sighash change) ([2695](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2695))
  - Token aware CashAddr ([2695](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2688))
  - HeapOptional ([2698](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2689))
  - Transaction serialization ([2691](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2691))
  - Serializer extensions (GenericVectorWriter etc) ([2691](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2691))
  - Policy changes ([2691](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2691))
  - RPC updates ([2698](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/(2698))
  - Wallet updates (including coin control, don't select token utxos) ([2698](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2698))


The new consensus rules in the aforementioned CHIPs will take effect once the median time past (MTP) of the most recent 11 blocks is greater than or equal to UNIX timestamp 1684152000.

Commit details
--------------

- `e1224d2140` Bump BCHU version to 2.0.0.0 (Andrea Suisani)
- `f9817b2e43` Add span.h to src/Makefile.am (Andrea Suisani)
- `8bce709581` Fix gitian rostrum build (Andrea Suisani)
- `4bb564c008` build: Fix CI linker issue (Dagur Valberg Johannsson)
- `8d91d136c5` Add unit tests for GetMinimumTxSize() (Andrea Suisani)
- `e95da9f498` Tag rostrum release v8.1.0 (Dagur Valberg Johannsson)
- `8958ef2903` Minimal CashToken RPC token support (Dagur Valberg Johannsson)
- `b769c74da4` Add a dummy BOOST_AUTO_TEST_CASE to chip_testing_setup.cpp (Andrea Suisani)
- `31a4ee174f` Add new network: "chipnet" (-chipnet) (Calin Culianu)
- `117f75e38a` Add signing with token support + fix tests (Dagur Valberg Johannsson)
- `c1b5bd0539` Handle validating SIGHASH_UTXOs (Andrew Stone)
- `4dc5cfe925` Update script error messages (Dagur Valberg Johannsson)
- `ceb4a93169` Calculate dust limit for token outputs (Dagur Valberg Johannsson)
- `59af01d2ec` Workaround prevector issue (Dagur Valberg Johannsson)
- `63395ee035` Improve reject reason for oversized OP_RETURN (Dagur Valberg Johannsson)
- `a0ef0a8f3b` Update ATMP validation order (Andrew Stone)
- `1a4eab9dc3` Add cashtokens introspection opcodes (Andrew Stone)
- `8c5c580d49` May 2023 CHIP test vectors (Dagur Valberg Johannsson)
- `e2a9f5b9b5` Update and fix testnet4 consensus parameters. (Andrea Suisani)
- `561fcd5dc5` fix some locking issues evident in debug builds (Andrew Stone)
- `178541b514` Build fix for OS X (Dagur Valberg Johannsson)
- `e9cb4b762a` CashToken primitives w/o opcodes and sighash (Dagur Valberg Johannsson)
- `d48c341d06` CashAddr: Add support for token-aware cash addresses (Calin Culianu)
- `58fdd2cec9` consensus: Add P2SH-32 support (Dagur Valberg Johannsson)
- `878546a15c` build: Fix boost depends build with glibc >= 2.34 (Axel Gembe)
- `92872e99d7` util: Add HeapOptional (Dagur Valberg Johannsson)
- `1c29cdf082` Fix bip69 output sort (Griffith)
- `3e1f5c070f` consensus: Change min tx size limit to 65 bytes (Dagur Valberg Johannsson)
- `6c812da871` Port 'feature_tx_version' test to BU framework (Dagur Valberg Johannsson)
- `cfe161714c` Unbundle the bad tx testing in blocks, post upgrade (freetrader)
- `0a753020cf` BCH May 2023 Upgrade: Enforce tx nVersion in consensus rather than standardness (Calin Culianu)
- `0307f9329c` tests: Remove OldSetKeyFromPassphrase/OldEncrypt/OldDecrypt (practicalswift)
- `50467cf089` wallet: Change CCrypter to use vectors with secure allocator (Wladimir J. van der Laan)
- `1369634056` Replace Q_FOREACH where PAIRTYPE is used (Dagur Valberg Johannsson)
- `e3e4dff4a0` Use correct network message in net.cpp comment (Andrea Suisani)
- `17e916dc34` Fix formatting (Andrea Suisani)
- `3a8752db4c` Add checkpoints for May 2021 and May 2022 BCH network upgrades (Andrea Suisani)
- `281c7af796` Execute as many QA test in parallel as the number of cpus (Andrea Suisani)
- `b5905081ec` Run functional and unit tests using binaries build with system libs deps (Andrea Suisani)
- `50766b58b8` Remove 32 bits CI task for windows and linux (Andrea Suisani)
- `c3b728ac6b` Remove installation of clang-format-12/15 from .gitlab-ci.yaml (Andrea Suisani)
- `3dadd03408` Emit an error if clang-format is not installed (Dagur Valberg Johannsson)
- `dd7b7e64b5` Add run-formatting target to makefile.am (Dagur Valberg Johannsson)
- `ec025a8cf0` Make check-formatting target to work with exec names w/o version (Dagur Valberg Johannsson)
- `8f844972ab` Increase timeout for mempool_accept.py (Andrea Suisani)
- `2505d88253` Formattng changes after updating clang-format to version 15.0.7 (Andrea Suisani)
- `2f39eb892b` Update formatting tools to use clang-format ver 15 (Andrea Suisani)
- `2bc9250b93` build: Bump rust to 1.64.0 (Dagur Valberg Johannsson)
- `7c4f09c50d` test: Fetch & run electrum tests from rostrum repo (Dagur Valberg Johannsson)
- `79642298e9` ci: Disable check-formatting (Dagur Valberg Johannsson)
- `e60aaf133e` Execute as many QA test in parallel as the number of cpus (Andrea Suisani)
- `778c944ada` Run functional and unit tests using binaries build with system libs deps (Andrea Suisani)
- `919c39e74e` Remove 32 bits CI task for windows and linux (Andrea Suisani)
- `c70f332aca` Remove installation of clang-format-12/15 from .gitlab-ci.yaml (Andrea Suisani)
- `7304293fae` Emit an error if clang-format is not installed (Dagur Valberg Johannsson)
- `d80c01500a` Add run-formatting target to makefile.am (Dagur Valberg Johannsson)
- `6c69cad9be` Make check-formatting target to work with exec names w/o version (Dagur Valberg Johannsson)
- `4f3fe0535c` Increase timeout for mempool_accept.py (Andrea Suisani)
- `5f6bd6e6ae` Formattng changes after updating clang-format to version 15.0.7 (Andrea Suisani)
- `8170cacb5b` Update formatting tools to use clang-format ver 15 (Andrea Suisani)
- `9219f5ab35` ci: Disable check-formatting (Dagur Valberg Johannsson)
- `94f6656cf4` consensus: Add check for May 2023 activation MTP (Dagur Valberg Johannsson)
- `5ff3e5e4a7` consensus: Use height for May 2022 hf activation (Dagur Valberg Johannsson)
- `1b70c386c6` build: Bump rust to 1.64.0 (Dagur Valberg Johannsson)
- `07c46de42c` test: Fetch & run electrum tests from rostrum repo (Dagur Valberg Johannsson)
- `f40424393c` Update copyright year to 2023 (Marius Kjærstad)
- `90b219149a` cashlib: Export decoding of private key (Dagur Valberg Johannsson)
- `9d4352a2ee` electrum: Rename ElectrsCash -> rostrum (Dagur Valberg Johannsson)
- `a46a29779f` [dev] fix compiler and linker warnings (Andrea Suisani)
- `fef8d73470` More specific fast filter docs, no odd num of HFs (Awemany)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Awemany
- Dagur Valberg Johannsson
- Griffith
- Marius Kjærstad

We have backported a set of changes from Bitcoin Cash Nodes and Bitcoin Core.

Following all the indirect contributors whose work has been imported via the above backports:

- Axel Gembe
- Calin Culianu
- Wladimir J. van der Laan
- freetrader
- practicalswift
