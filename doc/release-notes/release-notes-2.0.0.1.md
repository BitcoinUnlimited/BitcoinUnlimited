Release Notes for BCH Unlimited 2.0.0.1
======================================================

BCH Unlimited version 2.0.0.1 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/issues>

This is a bug fix release of BCH Unlimited.

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

If after the upgrades your client is still stuck at block 831715, please run the following RPC commands:

```sh
bitcoin-cli invaludateblock 00000000000000000017eff72dd2fa9f913e8caf44e4b30589743bf18bb50526
bitcoin-cli reconsiderblock 00000000000000000017eff72dd2fa9f913e8caf44e4b30589743bf18bb50526
```

Main Changes in 2.0.0.1
-----------------------

This is list of the main changes that have been merged in this release:

- Fix ECDSA multisig verification issue ([2714](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2714))
- Bump rostrum to 10.0.0 ([2715](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/merge_requests/2715))


Commit details
--------------

- `2736a4f44c`  Add release notes for BCH Unlimited 2.0.0.1 (Andrea Suisani)
- `1bd0ff43df` Bump BCHU version to 2.0.0.1 (Andrea Suisani)
- `1f0042f284` [depends] Fix Qt 5.9.8 fetch URL (Andrea Suisani)
- `5d2c1d1e83` [depends] update rust to last stable version, 1.73.0 (Andrea Suisani)
- `6564f6af09` Pin rostrum electrum server to v10.0.0 (Andrea Suisani)
- `7fedf0bd65` [ci] use python 3.11 to run rostrum QA functional tests (Andrea Suisani)
- `54d03f5779` ECDSA multisig verification issue (Andrea Suisani)
- `83fec9c4a6` AssertWriteLock now checks if the current thread has the exclusive lock (Griffith)
- `6f25a8ce72` resolve test lockordering issues (Andrew Stone)
- `b1b5149005` cast nHeight to int32_t to match the VarIntMode expected type/value (Griffith)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Griffith

I also wanted to personally thanks @dagurval, freetrader and  @jldqt for the work done in analysing the bug and finding the root cause.

