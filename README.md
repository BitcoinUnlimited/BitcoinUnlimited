[Website](https://www.bitcoinunlimited.info)  | [Download](https://www.bitcoinunlimited.info/download) | [Setup](doc/README.md)  |  [Xthin](doc/bu-xthin.md)  |  [Xpedited](doc/bu-xpedited-forwarding.md)  |   [Miner](doc/miner.md)

[![Build Status](https://travis-ci.org/BitcoinUnlimited/BitcoinUnlimited.svg?branch=dev)](https://travis-ci.org/BitcoinUnlimited/BitcoinUnlimited)

What is Bitcoin?

Bitcoin is an experimental new digital currency that enables instant payments to
anyone, anywhere in the world. Bitcoin uses peer-to-peer technology to operate
with no central authority: managing transactions and issuing money are carried
out collectively by the network. Bitcoin Unlimited is the name of open source
software which enables the use of this currency.

For more information, as well as an immediately useable, binary version of
the Bitcoin Unlimited software, see https://www.bitcoinunlimited.info/download, or read the
[original whitepaper](https://www.bitcoinunlimited.info/resources/bitcoin.pdf).

License
-------

Bitcoin Unlimited is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

What is Bitcoin Unlimited?
=====================================

Bitcoin Unlimited is an implementation of the Bitcoin client software that is based on Bitcoin Core.
However, Bitcoin Unlimited has a very different philosophy than Core.

It follows a philosophy and is administered by a formal process described in the [Articles of Federation](https://www.bitcoinunlimited.info/resources/BUarticles.pdf).
In short, we believe in market-driven decision making, emergent consensus, and giving our users choices.

Quick installation Instructions
====================================

If you're running an Ubuntu system:

```sh
sudo apt-get install software-properties-common
sudo add-apt-repository ppa:bitcoin-unlimited/bu-ppa
sudo apt-get update
sudo apt-get install bitcoind bitcoin-qt
```
If you're compiling from source:

```sh
sudo apt-get install git build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils libboost-all-dev
## optional: only needed if you want bitcoin-qt
sudo apt-get install qttools5-dev-tools qttools5-dev libprotobuf-dev protobuf-compiler libqrencode-dev
## optional: only needed if your wallet use the old format
sudo apt-get install software-properties-common

## this not needed if your wallet will use the new
## format, or if you're not going to use a wallet at all
sudo add-apt-repository ppa:bitcoin-unlimited/bu-ppa
sudo apt-get update
sudo apt-get install libdb4.8-dev libdb4.8++-dev

mkdir -p ~/src
cd ~/src
git clone https://github.com/BitcoinUnlimited/BitcoinUnlimited.git bu-src
cd bu-src
git checkout release
./autogen.sh
./configure
make
sudo make install
```

For more detailed explanations on how compile from source just look at doc/build-*.md files (e.g. [here](doc/quick-install.md))

Quick Startup and Initial Node operation
========================================

### QT or the command line:

There are two modes of operation, one uses the QT UI and the other runs as a daemon from the command line.  The QT version is bitcoin-qt or bitcoin-qt.exe, the command line version is bitcoind or bitcoind.exe. No matter which version you run, when you launch for the first time you will have to complete the intial blockchain sync.

### Initial Sync of the blockchain:

When you first run the node it must first sync the current blockchain.  All block headers are first retrieved and then each block is downloaded, checked and the UTXO finally updated.  This process can take from hours to `weeks` depending on the node configuration, and therefore, node configuration is crucial.

The most important configuration which impacts the speed of the initial sync is the `dbcache` setting.  The larger the dbcache the faster the initial sync will be, therefore, it is vital to make this setting as high as possible.  If you are running on a Windows machine there is an automatically adjusting dbcache setting built in; it will size the dbcache in such a way as to leave only 10% of the physical memory free for other uses.  On Linux and other OS's the sizing is such that one half the physical RAM will be used as dbcache. While these settings, particularly on non Windows setups, are not ideal they will help to improve the initial sync dramatically.

However, even with the automatic configuration of the dbcache setting it is recommended to set one manually if you haven't already done so (see the section below on Startup Configuration). This gives the node operator more control over memory use and in particular for non Windows setups, can further improve the performance of the initial sync.

### Startup configuration:

There are dozens of configuration and node policy options available but the two most important for the initial blockchain sync are as follows.

##### dbcache: As stated above, this setting is crucial to a fast initial sync.  You can set this value from the command line by running
`bitcoind -dbcache=<your size in MB>`, for example, a 1GB dbcache would be `bitcoind -dbcache=1000`.  Similarly you can also add the setting to the bitcoin.conf file located in your installation folder. In the config file a simlilar entry would be `dbcache=1000`.  When entering the size
try to give it the maximum that your system can afford while still leaving enough memory for other processes.

##### maxoutconnections: It is generally fine to leave the default outbound connection settings for doing a sync, however, at times some users
have reported issues with not being able to find enough useful connections. If that happens you can change this setting to override the default.
For instance `bitcoind -maxoutconnections=30` will give you 30 outbound connections and should be more than enough in the event that the
node is having difficulty.
