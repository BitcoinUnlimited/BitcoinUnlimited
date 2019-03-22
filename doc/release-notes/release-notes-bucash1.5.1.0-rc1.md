Release Notes for Bitcoin Unlimited Cash Edition 1.5.1.0-rc1
=========================================================

Bitcoin Unlimited Cash Edition release candidate 1 for version 1.5.1.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a main release candidate of Bitcoin Unlimited compatible
with the Bitcoin Cash specifications you could find here:

- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/uahf-technical-spec.md (Aug 1st '17 Protocol Upgrade, bucash 1.1.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17 Protocol Upgrade, bucash 1.1.2.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/may-2018-hardfork.md (May 15th '18 Protocol Upgrade, bucash 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18 Protocol Upgrade, bucash 1.5.0.0, 1.5.0.1, 1.5.0.2)

This release candidate is compatible with both [Bitcoin Cash](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md) and [SV](https://github.com/bitcoin-sv/bitcoin-sv/blob/master/doc/release-notes.md) changes to the consensus rules.
SV features set is **disabled by default**, the default policy is to activate the set of changes as defined by the bitcoincash.org.

Although this software has passed normal QA tests, it is currently undergoing longevity testing to validate stability.  If you are looking for the most stable software, use the last Official Release (i.e. 1.5.0.2).

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

If you are upgrading from a release older than 1.1.2.0, your UTXO database will be converted
to a new format. This step could take a variable amount of time that will depend
on the performance of the hardware you are using.

Other than that upgrading from a version lower than 1.5.0.1 your client is probably stuck
on a minority chain and need some manual intervention to make so that it follow the majority
chain after the upgrade. For more detail please look at `reconsidermostworkchain` RPC commands.

Main Changes
------------

- 10x transaction processing performance
- Extended version message: [xversion](https://github.com/BitcoinUnlimited/BitcoinUnlimited/blob/release/doc/xversionmessage.md)
- adding a checkpoint at height 556767 both for the SV and the BCH chain

Commit details
-------


- `9f9b2a019` bump version to 1.5.1 (Andrew Stone)
- `f0a7354a9` Refactor p2p messaging (#1483) (Greg Griffith)
- `a53d1f1d7` add checkpoints for BCH and BSV chains (#1478) (Greg Griffith)
- `44307cc45` Optimization 3 - give mapBlockIndex its own lock rather than using cs_main & related cleanup (#1487) (Andrew Stone)
- `ef6bbfd0a` Optimization 2 - Remove cs_main locking around nodestate (replace with finer grained lock).  Make txCommitQ a pointer so that it can be efficiently swapped out.  Allow requestManager to make 10k burst and 5k sustained requests per second. (#1485) (Andrew Stone)
- `e78c216db` if parent data structure is corrupt, assert in debug code, and clear it out and continue in release code (Andrew Stone)
- `1952c26df` Need to add FORCE when building the xversionkeys.h in Makefile.am (Peter Tschipper)
- `b5068568f` add a small test for zero checksums, tolerate them in the mininode (if configured), and simplify handling of common xversion integer value (Andrew Stone)
- `23de6af99` fix makefile to properly compile xversionkeys.h on windows (Greg Griffith)
- `57824c815` fix the way transactions from committed blocks are removed from the mempool (Andrew Stone)
- `3c5d41f71` Change settings in leveldb to prevent write pauses during reindex (#1459) (Peter Tschipper)
- `99ac7c54d` Temporarily disable win32 cross compilation and unit tests on Travis (Andrea Suisani)
- `7c11bcddc` Allow BU nodes to pass xversion config to drop checksums from passed messages.  These checksums inefficient and are not necessary for several reasons -- TCP already has message checksums, for one (Andrew Stone)
- `72759ccd0` Improve performance of wallet-hd.py (#1431) (Peter Tschipper)
- `5dfcf187a` Log message for REJECT_WAITING was not printing out reason code (#1475) (Peter Tschipper)
- `93675012c` Extended version message (xversion) (#1236) (awemany)
- `2a1afafef` fix qa tests for fork changes (Andrew Stone)
- `3de9bd56a` LTOR fixes for unit tests (#1479) (Greg Griffith)
- `91037d1e0` Remove DS_Store WindowBounds bytes object (Jonas Schnelli)
- `948d333f7` BUCash 1.5.0.2 release notes (Andrea Suisani)
- `3a8853125` Remove excessiveBlockSize and Acceptance Depth from QT (Peter Tschipper)


Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- awemany
- Greg Griffith
- Peter Tschipper

We have backported an amount of changes from other projects, namely Bitcoin Core.

Following all the indirect contributors whose work has been imported via the above backports:

- Jonas Schnelli
