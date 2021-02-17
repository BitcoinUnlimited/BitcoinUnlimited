[Website](https://www.bitcoinunlimited.info)  | [Download](https://www.bitcoinunlimited.info/download) | [Setup](README.md)   |   [Miner](doc/miner.md)  |  [ElectronCash](doc/bu-electrum-integration.md)  |  [UnconfirmedChains](doc/unconfirmedTxChainLimits.md)

[![Build Status](https://gitlab.com/bitcoinunlimited/BCHUnlimited/badges/dev/pipeline.svg?key_text=Build%20Status%20%28dev%29&key_width=110)](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/pipelines)

# What is Bitcoin Cash?

Bitcoin Cash is an experimental new digital currency that enables instant payments to
anyone, anywhere in the world. Bitcoin Cash uses peer-to-peer technology to operate
with no central authority: managing transactions and issuing money are carried
out collectively by the network. Bitcoin Unlimited is the name of open source
software which enables the use of this currency.

For more information, as well as an immediately useable, binary version of
the Bitcoin Unlimited software, see https://www.bitcoinunlimited.info/download, or read the
[original whitepaper](https://www.bitcoinunlimited.info/resources/bitcoin.pdf).



# What is Bitcoin Unlimited?

Bitcoin Unlimited is an implementation of the Bitcoin client software that is based on Bitcoin Core.
However, Bitcoin Unlimited has a very different philosophy than Core.

It follows a philosophy and is administered by a formal process described in the [Articles of Federation](https://www.bitcoinunlimited.info/resources/BUarticles.pdf).
In short, we believe in market-driven decision making, emergent consensus, and giving our users choices.


# Installing

For info on installing Bitcoin Unlimited see [INSTALL.md](INSTALL.md)

# Building

For info on building Bitcoin Unlimited from sources, see
- [Dependencies](doc/dependencies.md)
- [Unix Build Notes](doc/build-unix.md)
- [Unix Build Notes (RPM)](doc/build-unix-rpm.md)
- [Windows Build Notes](doc/build-windows.md)
- [OpenBSD Build Notes](doc/build-openbsd.md)
- [macOS Build Notes](doc/build-macos.md)
- [Deterministic macOS DMG Notes](doc/README_macos.md)
- [Gitian Building Guide](doc/gitian-building.md)

They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

# Running / setup

- [Running large unconfirmed transaction chain limits](doc/unconfirmedTxChainLimits.md)
- [Running an electron cash protocol server](doc/bu-electrum-integration.md)
- [Getting the most out of Xtreme thinblocks](bu-xthin.md)
- [Setting up an Xpedited Relay Network](bu-xpedited-forwarding.md)
- [Tor Support](doc/tor.md)
- [Init Scripts (systemd/upstart/openrc)](doc/init.md)
- [Using Bitcoin Unlimited for Mining](doc/miner.md)

# Development

- [Developer Notes](doc/developer-notes.md)
- [Contributing](CONTRIBUTING.md)
- [BUIP, BIP and Bitcoin Cash Specifications](doc/bips-buips-specifications.md)
- [Bitcoin Unlimited Improvement Proposal Archive](https://github.com/BitcoinUnlimited/BUIP)
- [Multiwallet Qt Development](doc/multiwallet-qt.md)
- [Release Notes](doc/release-notes.md)
- [Release Process](doc/release-process.md)
- [Translation Process](doc/translation_process.md)
- [Translation Strings Policy](doc/translation_strings_policy.md)
- [Unit Tests](doc/unit-tests.md)
- [Unauthenticated REST Interface](doc/REST-interface.md)
- [Shared Libraries](doc/shared-libraries.md)
- [Assets Attribution](contrib/debian/copyright)
- [Files](doc/files.md)
- [Fuzz-testing](doc/fuzzing.md)


# Online resources

 - [Issue Tracker](https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues)
 - [The Bitcoin Forum](https://www.bitco.in/forum)
 - [Reddit /r/btc](https://www.reddit.com/r/btc)
 - [Reddit /r/bitcoin_unlimited](https://www.reddit.com/r/bitcoin_unlimited)
 - [Slack Channel](https://bitcoinunlimited.slack.com/)



# License

Bitcoin Unlimited is released under the terms of the [MIT software license](http://www.opensource.org/licenses/mit-license.php). See [COPYING](COPYING) for more
information.
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
