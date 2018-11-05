Release Notes for Bitcoin Unlimited Cash Edition 1.5.0.1
=========================================================

Bitcoin Unlimited Cash Edition version 1.5.0.1 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a major release version based of Bitcoin Unlimited compatible
with the Bitcoin Cash specifications you could find here:

- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/uahf-technical-spec.md (Aug 1st '17 Protocol Upgrade, bucash 1.1.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17 Protocol Upgrade, bucash 1.1.2.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/may-2018-hardfork.md (May 15th '18 Protocol Upgrade, bucash 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18 Protocol Upgrade, bucash 1.5.0.0, 1.5.0.1)

This release will also implement a set of consensus changes proposed by an alternative implementation, Bitcoin SV,
see [SV release notes for ver 0.1.0](https://github.com/bitcoin-sv/bitcoin-sv/blob/master/doc/release-notes.md) for more details.
Such set of features is **disabled by default**, the default policy is to activate the set of changes as defined by the bitcoincash.org
[specification](https://github.com/bitcoin-sv/bitcoin-sv/blob/master/doc/release-notes.md).

To configure your BUcash client so that it will activate the protocol upgrade proposed by SV you need to add `consensus.forkNov2018Time=0` and `consensus.svForkNov2018Time=1`
in your `bitcoin.conf` file. Trying to activate both protocol upgrades at the same time will lead to the client to exit with this error
message: `Both the SV and ABC forks are enabled.  You must choose one.`

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

- Implementation of Bitcoin SV November 2018 features (see the [SV release notes for ver 0.1.0](https://github.com/bitcoin-sv/bitcoin-sv/blob/master/doc/release-notes.md)), **disable by default**
    - OP_MUL, OP_INVERT, OP_LSHIFT, OP_RSHIFT
    - Increase max number of op_codes per script to 500
    - Increase max block size to 128MB
- Turn graphene on by default

Commit details
-------

- `94a9647f0` patch check that look at pointer address of labelPublic instead of size (Greg Griffith)
- `13d647e9b` Add lock on vNodes when checking size() in rpc getinfo() (Peter Tschipper)
- `b62f437b2` fix linker error (Andrew Stone)
- `b5984be52` Fix bug in dmg builder so that it actually reads in the configuration file (Don Patterson)
- `b9ded0885` build: Fix 'make deploy' for OSX (Cory Fields)
- `2178b1cfa` depends: mac deploy Py3 compatibility (Wladimir J. van der Laan)
- `b4ff18aec` Add RPC call 'logline' to add lines to debug.log (Awemany)
- `b3ef45ed9` Fix requestmanager_tests.cpp (Peter Tschipper)
- `7a84f29d2` Add Bitcoin SV seeders only if SV fork is activated (Andrea Suisani)
- `057295cc7` Turn miningSvForkTime and minigForkTime into CTweakRef (Andrea Suisani)
- `a85fe1650` fix format (Peter Tschipper)
- `3c4c5f166` Fix getminingcandidate (Peter Tschipper)
- `d63fee097` Reduce Transaction re-requests under heavy load (#1430) (Peter Tschipper)
- `472f47990` Some cleanup and also extend CTransactionRef into a few more areas (#1429) (Peter Tschipper)
- `7f16a286c` Fix ctor.py from failing (Peter Tschipper)
- `c64d44b74` clean up merge of new opcodes, add SV config and implement max instruction increase.  Add a config validator for the forks.  To do so, it was necessary to extend the CTweak to support validators like CTweakRef currently does.  Shorten QA test time (Andrew Stone)
- `a669fe85b` from SV: Added OP_INVERT functionality and tests (shaunOK)
- `724076254` from SV: Add OP_MUL functionality and tests (shaunOK)
- `7194b0b79` Merge from SV: Added LSHIFT and RSHIFT opcode functionality and test (shaunOK)
- `06abba7a1` Fix for #1416: can't compile without wallet unless bdb4.8 is installed (#1427) (Peter Tschipper)
- `760e6b271` Add getraworphanpool rpc (#1421) (Peter Tschipper)
- `ace8ee652` Turn graphene on by default (#1418) (Peter Tschipper)
- `83e3b7b7b` Remove run-bitcoind-for-test.sh.in (#1424) (awemany)
- `05688d3a7` Define NODE_CF service bit (#1422) (Chris Pacia)
- `e59a724d2` Fix BIP 135 voting guide URL (#1417) (Andrea Suisani)
- `41cb9be0e` build: python 3 compatibility (Wladimir J. van der Laan)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Awemany
- Chris Pacia
- Greg Griffith
- Justaphf
- Peter Tschipper

We have backported an amount of changes from other projects, namely Bitcoin SV and Bitcoin Core.

Following all the indirect contributors whose work has been imported via the above backports:

- Cory Fields
- Don Patterson
- shaunOK
- Wladimir J. van der Laan

