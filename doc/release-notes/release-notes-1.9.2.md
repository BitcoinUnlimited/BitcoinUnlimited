Release Notes for BCH Unlimited 1.9.2
======================================================

BCH Unlimited version 1.9.2 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/issues>

This is a major release of BCH Unlimited compatible with the upcoming protocol upgrade of the Bitcoin Cash network. You could find a detailed list of all the specifications here:

- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/uahf-technical-spec.md (Aug 1st '17, ver 1.1.0.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17, ver 1.1.2.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/may-2018-hardfork.md (May 15th '18, ver 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18, ver 1.5.0.0, 1.5.0.1, 1.5.0.2, 1.5.1.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2019-05-15-upgrade.md (May 15th '19, ver 1.6.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2019-11-15-upgrade.md (Nov 15th '19, ver 1.7.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2020-05-15-upgrade.md (May 15th '20, ver 1.8.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2020-11-15-upgrade.md (Nov 15th '20, ver 1.9.0, 1.9.0.1, 1.9.1)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2021-05-15-upgrade.md (May 15th '21, ver 1.9.2)

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

Main Changes in 1.9.2
-----------------------

This is list of the main changes that have been merged in this release:

- [Disable intelligent forwarding](https://gitlab.com/bitcoinunlimited/BCHUnlimited/-/blob/dev/doc/unconfirmedTxChainLimits.md) on or after May 15, 2021, as per BCH network-wide upgrade agreement.
- Accept transactions with multiple OP_RETURN outputs as standard on or after May 15, 2021, as per BCH network-wide upgrade agreement.

Commit details
--------------

- `a7064a4ef` Add release notes for BCH Unlimited 1.9.2 (Andrea Suisani)
- `85dace460` Adjust to use isMay2021Enabled check (Nicolai Skye)
- `b64c42682` Make multiple OP_RETURN standard (Nicolai Skye)
- `c3cf650fe` Bump BCH Unlimited verions to 1.9.2 (Andrea Suisani)
- `6956d9e8e` Add an in memory block cache (Peter Tschipper)
- `a062c90f7` Use 120 wait instead of 60 in wallet.py (Peter Tschipper)
- `738bc3d4f` Remove the call to IsMay2021Next() when setting unconfPushAction. (Peter Tschipper)
- `68594909c` Revert back to the 4hr orphan timeout after the fork is activated. (Peter Tschipper)
- `681a40dbe` Add tests to mempool_push.py (Peter Tschipper)
- `a719bb1b0` Adjustments after rebasing on sickpigs activation code (Peter Tschipper)
- `22d51b0c7` Adapt the long chain code for the May 15, 2021 hard fork. (Peter Tschipper)
- `9463b562f` Remove misbehavior for invalid dsproof and throw if spenders are the same (Peter Tschipper)
- `38b12091a` Cleanup parallel.cpp/.h and remove the redundancy with regards to clearing the orphanpool (Peter Tschipper)
- `7b9a1a415` Output where the txn is found when doing a getrawtransaction rpc (Peter Tschipper)
- `84142123a` Activate Nov 2020 upgrade by height, add helpers from May 2021 (Andrea Suisani)
- `34eefe7a3` Set the RPC warmup earlier when reindexing (Peter Tschipper)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Nicolai Skye
- Peter Tschipper

We have backported an amount of changes from other projects, namely Bitcoin Cash Nodes

Following all the indirect contributors whose work has been imported via the above backports:

- BigBlocksIfTrue
