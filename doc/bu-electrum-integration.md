Electrum Server Basic integration.
==================================

Starting from version 1.6  BUcash it's possible to build a rust electrum server implementation binary (electrs)
and spawn the process during the startup phase. Then you could use this electum server
along with the electron cash wallet.

To build the [electrs](https://github.com/dagurval/electrs) binaries use:

```sh
make electrs
```

To make it so that `bitcoind` spawn `electrs` at startup you need to add those
lines to your `bitcoin.conf`:

```
electrum=1 # to start the electrum server
electrum.exec=path/to/bin/electrs # optional path to your electrum server binary
debug=electrum
```

Adding `debug=electrum` will enable more useful logging.

This is how a local electrum cash wallet could be connected to the electrum server:

```sh
./electron-cash -v --oneserver --server=127.0.0.1:60001:t --testnet
```

Known issue: your RPC password is shown in the electrs command line.
