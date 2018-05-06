Release Notes for Bitcoin Unlimited Cash Edition 1.3.0.1
=========================================================

Bitcoin Unlimited Cash Edition version 1.3.0.1 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a minor release version based of Bitcoin Unlimited compatible
with the Bitcoin Cash specifications you could find here:

https://github.com/bitcoincashorg/spec/blob/master/uahf-technical-spec.md (Aug 1st Protocol Upgrade, bucash 1.1.0.0)
https://github.com/bitcoincashorg/spec/blob/master/nov-13-hardfork-spec.md (Nov 13th Protocol Upgrade, bucash 1.1.2.0)
https://github.com/bitcoincashorg/spec/blob/master/may-2018-hardfork.md (May 15th Protocol Upgrade, bucash 1.3.0.0, 1.3.0.1)


Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

If you are upgrading from a release older than 1.1.2.0, your UTXO database will be converted
to a new format. This step could take a variable amount of time that will depend
on the performance of the hardware you are using.

Downgrade
---------

In case you decide to downgrade from BUcash 1.2.0.1, or greater, to a version older than 1.1.2.0
will need to run the old release using `-reindex` option so that the
UTXO will be rebuild using the previous format. Mind you that downgrading to version
lower than 1.1.2.0 you will be split from the rest of the network that are following
the rules activated Nov 13th 2017 protocol upgrade.

Main Changes
------------

- provide test infrastructure to create transactions with more complex script
- various fix and improvements applied to BU request manager
- fix socket select bug on windows platform
- fix a problem with incoming connections due to node IP address discoverability
- Ensure that EB and datacarrier size are set properly after a node restart
- fix spurious disconnect due to block download crowding out getheaders
- Prune slow peers during initial sync

Commit details
--------------

- `cc4fb9726` formatting and remove unused var (Andrew Stone)
- `22d029b1f` add more disconnect logs (Andrew Stone)
- `6de38ce65` formatting, locks, add ping timeout reset when block arrives (Andrew Stone)
- `9a7eda016` fix spurious disconnect due to block download crowding out getheaders.  Convert all logs to GetLogName() (Andrew Stone)
- `6d789cfe5` Remove unnecessary brackets and then fix indentation (no code logic was changed) (Peter Tschipper)
- `987b98b1f` Remove unnecessary code for retrieving headers (Peter Tschipper)
- `aa28febb2` remove unnecessary inclusion of clientversion.h (Andrew Stone)
- `fa99115b0` make "bitcoin-cli get" == "bitcoin-cli get *" and a few small changes to test_template (#1067) (Andrew Stone)
- `ce467731d` mining block size set once at the moment of the fork (#1066) (Andrew Stone)
- `1e65c14cb` Remove std::move(pnode->AddrName) and just use pnode->AddrName (#1065) (Peter Tschipper)
- `137e3e371` Do not shadow variable addr in PushAddress or AddAddressKnown (#1064) (Peter Tschipper)
- `24c4608c3` Make nBlockSize private in block.h (#1063) (Peter Tschipper)
- `121f48abe` add a python test to create interesting and different transactions. (#1062) (Andrew Stone)
- `4219bb28a` Bump version to 1.3.0.1 (#1057) (Andrea Suisani)
- `e0f17b765` Prune slow peers during initial sync (#1048) (Peter Tschipper)
- `9eaa895ff` remove cs_main from requestmanager block requesting (#1036) (Peter Tschipper)
- `4c469aece` Allow a nonstandard tx to be submitted via the sendrawtransaction RPC call (#1060) (Andrew Stone)
- `5ca5df1ff` test limits. (#1040) (Andrew Stone)
- `afa6fca94` updated with flowee (stckwok)
- `5a36a2034` Ensure that EB and datacarrier size are set properly after a node restart (#1050) (Peter Tschipper)
- `7125aba0e` Merge #8784: Copyright headers for build scripts (Wladimir J. van der Laan)
- `3f751e71c` Fix forkAtNextBlock comments (Andrea Suisani)
- `c8b50fb80` change location of assert to right after pindexprev is first assigned a value (Peter Tschipper)
- `1dd32fb6a` Prevent allowing more connections than FD_SETSIZE on Windows (Justaphf)
- `561ff7b4c` Correct Windows builds using the wrong FD_SETSIZE (Justaphf)
- `23d8b49a3` Add basic sanity checking for script pushdata size (Amaury Séchet)
- `e361e0dc2` Refactor ParseScript logic to remove `else` conditions (Amaury Séchet)
- `9ae8ff040` Give more descriptive error when parsing partial-byte hex values (Jason B. Cox)
- `10b53d35f` Fix May 15th MTP UTC timestamp in release notes (Andrea Suisani)
- `c68999257` add consts for floweethehub (stckwok)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani (sickpig)
- Andrew Stone (gandrewstone)
- Justaphf
- Peter Tschipper (ptschip)
- Samuel Kwok (stckwok)

We have backported an amount of changes from other projects, namely Bitcoin Core and  Bitcoin ABC.

Following all the indirect contributors whose work has been imported via the above backports:

- Amaury Séchet
- Jason B. Cox
- Wladimir J. van der Laan
