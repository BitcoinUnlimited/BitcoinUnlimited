Release Notes for Bitcoin Unlimited Cash Edition 1.5.1.0-rc2
=========================================================

Bitcoin Unlimited Cash Edition release candidate 2 for version 1.5.1.0 is now available from:

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

Bug Fixed in 1.5.1.0-rc2
------------------------

- Making Xversion more permissive in the initial handshake
- Set Excessive Block Size (EB) properly while in SV mode
- Ensure that blk files storage operate on an append-only basis
- Various minor fixes/improvements to the python QA framework
- Reduce the last of thinblock/preferential timer to 1 sec from 10 sec.
- Fixed connection drop bug where a ping that happened soon after initial connection would be ignored, causing an eventual ping timeout.

Main Changes in 1.5.1
---------------------

- 10x transaction processing performance
- Extended version message: [xversion](https://github.com/BitcoinUnlimited/BitcoinUnlimited/blob/release/doc/xversionmessage.md)
- adding a checkpoint at height 556767 both for the SV and the BCH chain

Commit details
-------

`4699c035f` Add rc tag to BU version if rc is higher than 0 (#1519) (Andrea Suisani)
`3918ad3ea` Allow early pings and include a functional test for that (#1517) (awemany)
`068467377` Set EB and max script ops for the SV chain on startup. (#1518) (Peter Tschipper)
`2ab8b61d0` Add more log info to "missing or spent" error condition in ConnectBlockDependencyOrdering (#1513) (Andrea Suisani)
`ded3d6258` Remove old version code (#1486) (Greg Griffith)
`7e93e1242` Integration tests: Factor out some common boilerplate code (#1512) (awemany)
`705d745c6` .gitignore: Add output from running txPerf (#1508) (awemany)
`5664663ad` Making xversion more permissive (#1515) (awemany)
`cdb1eb498` Reduce preferential Graphene/Xthin timers to 1 second each (#1511) (Jonathan Toomim)
`c891c1478` Make checkpoints modifiable and modify them for the BCH/SV chains (#1509) (Peter Tschipper)
`f11092713` add missing DB_LAST_BLOCK write in WriteBatchSync (#1504) (Greg Griffith)
`fb330abb1` Replace FORCE rule in src/Makefile.am (#1500) (awemany)
`8dd66ed4d` Simplify RPC tests by cleaning up connect_nodes_bi calls (#1506) (awemany)
`1c774653c` BUCash release notes for version 1.5.1.0 Release Candidate 1 (Andrea Suisani)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- awemany
- Greg Griffith
- Jonathan Toomim
- Peter Tschipper
