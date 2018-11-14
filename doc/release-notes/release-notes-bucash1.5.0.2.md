Release Notes for Bitcoin Unlimited Cash Edition 1.5.0.2
=========================================================

Bitcoin Unlimited Cash Edition version 1.5.0.2 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a minor bugs fix only release version based of Bitcoin Unlimited compatible
with the Bitcoin Cash specifications you could find here:

- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/uahf-technical-spec.md (Aug 1st '17 Protocol Upgrade, bucash 1.1.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17 Protocol Upgrade, bucash 1.1.2.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/may-2018-hardfork.md (May 15th '18 Protocol Upgrade, bucash 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18 Protocol Upgrade, bucash 1.5.0.0, 1.5.0.1, 1.5.0.2)

This release also provides an RPC called 'signdata' to generate signatures compatible with the CHECKDATASIG opcode. Like 1.5.0.1
it is compatible with both [Bitcoin Cash](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md) and [SV](https://github.com/bitcoin-sv/bitcoin-sv/blob/master/doc/release-notes.md) changes to the consensus rules.
SV features set is **disabled by default**, the default policy is to activate the set of changes as defined by the bitcoincash.org.

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

- Fix gitian build for macOS
- Improve the script fuzz testing
- In GBT, match fees and sigops with the correct tx
- Improve propagation of non-final and too-long-mempool-chain transactions by deferring them until the relevant block arrives
- New RPC: `signdata` to generate signatures compatible with the CHECKDATASIG opcode
- Improve documentation (docker, SV activation)

Commit details
-------

- `617fd9998` Bump BUCash version to 1.5.0.2 (#1471) (Andrea Suisani)
- `d693dfc40` very basic docker instructions and files (#1448) (Greg Griffith)
- `268a41fc6` [TEST ONLY] Script fuzzer (#1460) (Andrew Stone)
- `73a3dda28` In GBT, match fees and sigops with the correct tx (#1470) (Andrew Stone)
- `8f8509ff9` Test framework improvements (part 1) (#1456) (awemany)
- `102e7185d` Improve propagatation of non-final and too-long-mempool-chain (#1419) (Peter Tschipper)
- `5cdec3c53` remove a few cs_main locks in QT that are no longer needed (#1469) (Peter Tschipper)
- `fbcd53fbe` Fix a couple of potential log  errors in thinblocks and graphene. (#1466) (Peter Tschipper)
- `5b2440582` Add missing cs_vNodes lock in requestmanager_tests.cpp (#1468) (Peter Tschipper)
- `5f7c242ac` Don't print rpcuser or rpcpassword to the logfile. (Peter Tschipper)
- `faccfbe06` Remove ban if merkle root incorrect for graphene block (Peter Tschipper)
- `d63ceda7d` 'signdata' RPC call (#1458) (Andrew Stone)
- `dee3a88b6` Use SV upgrade specifications rather than SV 0.1.0 release notes (#1454) (Andrea Suisani)
- `2e0859704` Add instructions on how to activate SV upgrade (#1455) (Andrea Suisani)
- `4ccef23b0` depends: biplist 1.0.3 (fanquake)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Awemany
- Greg Griffith
- Peter Tschipper

We have backported an amount of changes from other projects, namely Bitcoin Core.

Following all the indirect contributors whose work has been imported via the above backports:

- fanquake
