Release Notes for BCH Unlimited 1.9.1.1
======================================================

BCH Unlimited version 1.9.1.1 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/issues>

This is a bugs fix release of BCH Unlimited compatible with the upcoming protocol upgrade of the Bitcoin Cash network. You could find a detailed list of all the specifications here:

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

Main Changes in 1.9.1.1
-----------------------

This is list of the main changes that have been merged in this release:

- Properly handle bogus value for `datadir` param
- Fix node behaviour while running with `disablewallet=1`
- Align `getblock` API with BCHN's
- Add orphans transactions to `getrawtransaction` RPC call
- Fix shutdown during reindex
- Implement O(1) OP_IF/NOTIF/ELSE/ENDIF logic
- Improve transactions rate computation

Commit details
--------------

- `09305b313` Bump BCH Unlimited version to 1.9.1.1 (Andrea Suisani)
- `1e56049e5` directly enforce a maximum tx size as part of consensus (Griffith)
- `ea70dfac1` Shutdown during re-index was getting hung (Peter Tschipper)
- `e7cde23b0` Use a more general exceptions type (Andrea Suisani)
- `bbe14fdab` Remove bloom filter targeting for xthins (Peter Tschipper)
- `b6b737f05` Align our RPC for getblock() with the BCHN node (ptschip)
- `163c44301` getrawtransaction will now return orphans (Peter Tschipper)
- `e5aea9c0b` Remove descendantcount, descendantsize and descendantfees from EntryDescriptionString() (ptschip)
- `b92d0893c` Add a few uiIterface messages during startup (Peter Tschipper)
- `b43085e91` Create a RemoveForBlock for the orphan pool (Peter Tschipper)
- `7561fe8e5` remove tip is nullptr check during tip time validation on startup, tip is always at least the genesis block (Griffith)
- `046794fd5` Make sure to get an initial snapshot before doing txadmission (ptschip)
- `2f0fd3dc9` add bounds check for nToFetch. (Griffith)
- `3453b8dac` Trigger txn rate computation on a timer (Justaphf)
- `a5f52ada8` Fix a typo (Andrea Suisani)
- `94fc071cd` Fix "Unbound transactions chain" document URL (Andrea Suisani)
- `51b324e51` Move ConditionStack class defintion inside ScriptMachine class (Andrea Suisani)
- `2a1f03e44` Add clear() method to ConditionStack class (Andrea Suisani)
- `d14fc715d` Benchmark script verification with 100 nested IFs (Pieter Wuille)
- `73512f3a2` Implement O(1) OP_IF/NOTIF/ELSE/ENDIF logic (Pieter Wuille)
- `e85e8fc08` [refactor] interpreter: define interface for vfExec (Anthony Towns)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Greg Griffith
- Justin Holmes
- Peter Tschipper

We have backported an amount of changes from other projects, namely Bitcoin Core.

Following all the indirect contributors whose work has been imported via the above backports:

- Pieter Wuille
- Anthony Towns
