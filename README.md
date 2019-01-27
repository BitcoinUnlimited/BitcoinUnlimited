[Website](https://www.bitcoinunlimited.info)  | [Download](https://www.bitcoinunlimited.info/download) | [Setup](doc/README.md)  |  [Xthin](doc/bu-xthin.md)  |  [Xpedited](doc/bu-xpedited-forwarding.md)  |   [Miner](doc/miner.md)

[![Build Status](https://travis-ci.org/BitcoinUnlimited/BitcoinUnlimited.svg?branch=dev)](https://travis-ci.org/BitcoinUnlimited/BitcoinUnlimited)

# What is Bitcoin?

Bitcoin is an experimental new digital currency that enables instant payments to
anyone, anywhere in the world. Bitcoin uses peer-to-peer technology to operate
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
- [Windows Build Notes](doc/build-windows.md)
- [OpenBSD Build Notes](doc/build-openbsd.md)
- [macOS Build Notes](doc/build-macos.md)
- [Deterministic macOS DMG Notes](doc/README_macos.md)
- [Gitian Building Guide](doc/gitian-building.md)

They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

# Running / setup

- [Getting the most out of Xtreme thinblocks](bu-xthin.md)
- [Setting up an Xpedited Relay Network](bu-xpedited-forwarding.md)


# Development


The Bitcoin repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Multiwallet Qt Development](multiwallet-qt.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Unit Tests](unit-tests.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)


## Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)


# License

Bitcoin Unlimited is released under the terms of the [MIT software license](http://www.opensource.org/licenses/mit-license.php). See [COPYING](COPYING) for more
information.
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
