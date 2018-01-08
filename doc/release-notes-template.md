Bitcoin Unlimited version x.y.z.k is now available from:

  <https://bitcoinunlimited.info/download>

This is a new minor version release, including ........,
various bugfixes and updated translations.

<<<<<<< HEAD
Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

Upgrading and downgrading
=========================

How to Upgrade
--------------

....

Downgrade warning
-----------------

### Downgrade to a version < 0.12.0

Because release 0.12.0 and later will obfuscate the chainstate on every
fresh sync or reindex, the chainstate is not backwards-compatible with
pre-0.12 versions of Bitcoin Core or other software.

If you want to downgrade after you have done a reindex with 0.12.0 or later,
you will need to reindex when you first start Bitcoin Core version 0.11 or
earlier.

Notable changes
===============


### RPC and REST

### Configuration and command-line options

### Block and transaction handling

### P2P protocol and network code

### Validation

### Build system

### Wallet

Add support for the new cashaddr format. The `-usecashaddr` flag can be used to select which format is used when presenting addresses to users. By default, This client will keep using the old format until Jan, 14 and then switch to the new format. Both format are now accepted as input.

### GUI

### Tests and QA

### Miscellaneous

Credits
=======

Thanks to everyone who directly contributed to this release:


As well as everyone that helped translating on [Transifex](https://www.transifex.com/projects/p/bitcoin/).
