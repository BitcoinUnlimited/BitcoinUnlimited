# Electrum Server

## Overview

Bitcoin Unlimited includes support for electrum server (from version 1.6, Linux x64 only).

The server is maintained as a [separate piece of software](https://github.com/BitcoinUnlimited/ElectrsCash), but is integrated with the node software. Its process has the same lifetime as the bitcoind process, uses the same log files and adds functionality to the same RPC interface.

Running a private electrum server gives you an extended API interface, for example allowing you to lookup transaction history and balances of any address on the blockchain. Having a private electrum server also allows you to use wallet software such as Electron Cash, without compromising your privacy.

By [enabling SSL support](https://github.com/BitcoinUnlimited/ElectrsCash/blob/master/doc/usage.md) and making the server public, you're able to support the network light client infrastructure, which is run on volunteer basis (you may want to [add it to the server list](https://github.com/Electron-Cash/Electron-Cash/blob/master/lib/servers.json) in that case)

## Starting server

Add `electrum=1` to your bitcoin.conf file, or pass `-electrum=1` argument to bitcoind when from command line. You may want to also add `debug=electrum` to enable useful logging.

The RPC call `getelectruminfo` gives you runtime details, such as indexing status of the server. More detailed metrics are available via Prometheus, see monitoring section of the [usage document](https://github.com/BitcoinUnlimited/ElectrsCash/blob/master/doc/usage.md).

## Compiling from source

The latest stable release used by the node software can be compiled from the Bitcoin Unlimited source tree by running

```sh
make electrs
```

For more recent development versions, see the [usage document](https://github.com/BitcoinUnlimited/ElectrsCash/blob/master/doc/usage.md).

## Known issues

If you use RPC username/password rather than the default "RPC cookie", the credentials will be logged to `debug.log` by the electrs server.
