# Installing Bitcoin Unlimited

This document describes how to install and configure Bitcoin Unlimited.

# Downloading Bitcoin Unlimited

If you just want to run the Bitcoin Unlimited software go to the 
[Download](https://www.bitcoinunlimited.info/download) page and get the relevant 
files for your system.



## Windows

You can choose

- Download the setup file (exe), and run the setup program, or
- download the (zip) file, unpack the files into a directory, and then run bitcoin-qt.exe.


Installing binaries from Bitcoin Unlimited Official BU repositories
------------------------------------------------------------------

This is valid for Ubuntu Trusty 14.04 and Xenial 16.04. The repos will provide binaries and debug symbols for 4 different architectures: i386, amd64, armhf and arm64.


```sh
sudo apt-get install software-properties-common
sudo add-apt-repository ppa:bitcoin-unlimited/bu-ppa
sudo apt-get update
sudo apt-get install bitcoind bitcoin-qt (# on headlesse server just install bitcoind)
```

If you are moving from another Bitcoin compatible implementations (Core, Classic, XT) to BU, make sure to follow this plan before moving:

- backup your wallet (if any)
- make a backup of the `~/.bitcoin` dir
- if you have installed Core via apt using the ppa bitcoin core repo:
   - `sudo apt-get remove bitcoin*`
   - `sudo rm /etc/apt/sources.list.d/bitcoin-*.*`
- if you have compile Core from source:
   - `cd /path/where/the/code/is/stored`
   - `sudo make uninstall`



## Unix

Unpack the files into a directory and run:

- `bin/bitcoin-qt` (GUI) or
- `bin/bitcoind` (headless)

## macOS

Drag Bitcoin-Unlimited to your applications folder, and then run Bitcoin-Unlimited.

# Building Bitcoin Unlimited from source

See doc/build-*.md for detailed instructions on building the Bitcoin Unlimited software. Includes both info on building `bitcoind`, the intended-for-services, no-graphical-interface, 
implementation of Bitcoin and bitcoin-qt, the GUI.



See [README.md](README.md#quick-installation-instructions) for Quick Installation Instructions.



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


Compiling and installing binaries source:
---------------------------------------

This is valid for Ubuntu Trusty 14.04 and Xenial 16.04.

### Installing pre-reqs

If you need `bitcoind` and `bitcoin-cli` only:

```sh
sudo apt-get install git
sudo apt-get install build-essential libtool autotools-dev
sudo apt-get install automake pkg-config libssl-dev libevent-dev bsdmainutils libboost-all-dev
```

If you want also to install `bitcoin-qt`:

```sh
sudo apt-get install libqt4-dev libprotobuf-dev protobuf-compiler libqrencode
```

This is step is optional, to be done only if you need if you need wallet functionality and you want your wallet created with an earlier version of bitcoind

```sh
sudo apt-get install software-properties-common
sudo add-apt-repository ppa:bitcoin-unlimited/bu-ppa
sudo apt-get update
sudo apt-get install libdb4.8-dev libdb4.8++-dev
```

### Fetching the code and compile it


```sh
mkdir -p ~/src
cd ~/src
git clone https://github.com/BitcoinUnlimited/BitcoinUnlimited.git bu-src
cd bu-src
git checkout <desired_branch/tag>   # e.g. git checkout 0.12.1c
./autogen.sh
# if you want a plain bitcoind binary without GUI withou wallet support, use this configure line:
./configure --disable-wallet --without-gui
# otherwise if you need bitcoin-qt just issue a plain ./configure
export NUMCPUS=`grep -c '^processor' /proc/cpuinfo`
make -j$NUMCPUS
sudo make install #(will place them in /usr/local/bin, this is step is to be considered optional.)
```

### Miscellaneous


- `strip(1)` your binaries, bitcoind will get a lot smaller, from 73MB to 4.3MB)
- execute `bitcoind` using the `-daemon` option, bash will fork bitcoin process without cluttering the stdout



# Setup and initial blockchain download

Quick Startup and Initial Node operation
========================================

### QT or the command line:

There are two modes of operation, one uses the QT UI and the other runs as a daemon from the command line.  The QT version is bitcoin-qt or bitcoin-qt.exe, the command line version is bitcoind or bitcoind.exe. No matter which version you run, when you launch for the first time you will have to complete the intial blockchain sync.

### Initial Sync of the blockchain:

When you first run the node it must first sync the current blockchain.  All block headers are first retrieved and then each block is downloaded, checked and the UTXO finally updated.  This process can take from hours to *weeks* depending on the node configuration, and therefore, node configuration is crucial.

The most important configuration which impacts the speed of the initial sync is the `dbcache` setting.  The larger the dbcache the faster the initial sync will be, therefore, it is vital to make this setting as high as possible.  If you are running on a Windows machine there is an automatically adjusting dbcache setting built in; it will size the dbcache in such a way as to leave only 10% of the physical memory free for other uses.  On Linux and other OS's the sizing is such that one half the physical RAM will be used as dbcache. While these settings, particularly on non Windows setups, are not ideal they will help to improve the initial sync dramatically.

However, even with the automatic configuration of the dbcache setting it is recommended to set one manually if you haven't already done so (see the section below on Startup Configuration). This gives the node operator more control over memory use and in particular for non Windows setups, can further improve the performance of the initial sync.

### Startup configuration:

There are dozens of configuration and node policy options available but the two most important for the initial blockchain sync are as follows.

##### dbcache:

As stated above, this setting is crucial to a fast initial sync.  You can set this value from the command line by running
`bitcoind -dbcache=<your size in MB>`, for example, a 1GB dbcache would be `bitcoind -dbcache=1000`.  Similarly you can also add the setting to the bitcoin.conf file located in your installation folder. In the config file a simlilar entry would be `dbcache=1000`.  When entering the size
try to give it the maximum that your system can afford while still leaving enough memory for other processes.

##### maxoutconnections:

It is generally fine to leave the default outbound connection settings for doing a sync, however, at times some users
have reported issues with not being able to find enough useful connections. If that happens you can change this setting to override the default.
For instance `bitcoind -maxoutconnections=30` will give you 30 outbound connections and should be more than enough in the event that the
node is having difficulty.


# Getting help


