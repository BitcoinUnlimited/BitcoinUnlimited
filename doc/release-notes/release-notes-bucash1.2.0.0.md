Release Notes for Bitcoin Unlimited Cash Edition 1.2.0.0
=========================================================

Bitcoin Unlimited Cash Edition version 1.2.0.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a minor release version based of Bitcoin Unlimited compatible
with the Bitcoin Cash specification you could find here:

https://github.com/Bitcoin-UAHF/spec/blob/master/uahf-technical-spec.md


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

In case you decide to downgrade from BUcash 1.2.0.0 to a version older than 1.1.2.0
will need to run the old release using `-reindex-chainstate` option so that the
UTXO will be rebuild using the previous format.

Main Changes
------------

The main changes of this release is the introduction of the cashaddr new address format.
The specification for this new address encoding could be find [here](https://github.com/Bitcoin-UAHF/spec/blob/master/cashaddr.md).
The old format continue to be supported and won't be deprecated, The old format will remain the default till January 14th, 2017, on
that day the new format will be used as default new format automatically.
A command line flag `-usecashaddr` and configuration parameter could be used to select which format to by default.

Add limited support to BitPay Bitcoin Cash [addresses format](https://support.bitpay.com/hc/en-us/articles/115004671663-BitPay-s-Adopted-Conventions-for-Bitcoin-Cash-Addresses-URIs-and-Payme), this new format is accepted in any time an address is requested, and you can show the BitPay address form using the `getaddressforms` RPC. However BitPay-form addresses are not displayed in the GUI or as responses to any other RPCs.

Other notable changes:

- Activate the new DAA by height instead of MTP (removal of `newdaaactivationtime` parameter)
- Cash net magic became mandatory. Peers incoming connections using the old magic would be rejected.
- Coin cache improvements
- `satoshi` field added to any RPC call that returns an `amount`. The `satoshi` field is the amount in satoshis (as an integer). This field makes scripting easier since script authors do not need to use "perfect" fraction libraries such as financial decimal or binary coded decimal numbers.
- Add two RPC calls allow importing multiple watch-only addresses (importaddresses) and private keys (importprivatekeys) and supercede importaddress and importprivkey. The wallet rescan required after an import can now take hours, causing the original commands to time out and render it very difficult to import many addresses or keys. These new commands allow batching of multiple addresses or private keys, allowing all addresses and private keys to be imported in one step. Also, the rescan operation occurs asynchronously, so the RPC command returns right away rather than timing out. Your script can determine the status of the rescan by running "getinfo" and looking at the new "status" field.
- Add verbose "getstat" that returns a timestamp for reported statistics. And report the correct statistics regardless of system time changes (on platforms that support `CLOCK_MONOTONIC`)

Commit details
--------------

- `14bc737e0` add green icons in pixmap directory (Andrew Stone)
- `2024e7bf2` Revert Transacation description back to previous behavior (fixes #838) (#892) (Peter Tschipper)
- `bad62d979` Devpool work: express amounts in satoshis in RPC commands (#882) (goodvdh)
- `3de6653e7` make win build point to the main repo (Andrew Stone)
- `8b077a796` some versions of osx does not contain CLOCK_MONOTONIC so use a different clock on osx (Andrew Stone)
- `a2e028cff` BitcoinCash change release version to 1.2.0.0 (Andrew Stone)
- `4f4317a03` change the default python node to behave as a BitcoinCash node on the BitcoinCash branch (Andrew Stone)
- `df97d043f` fix wallet.py P2SH test to work with cashaddr formatted addresses. (Andrew Stone)
- `1c69a43a9` [Qt] remove trailing output-index from transaction-id (Jonas Schnelli)
- `62559a533` Remove the & that was showing in front of the Pullic Label: in sendcoinsentry.ui (Peter Tschipper)
- `0bf87c6d8` Fix issue where only the first P2SH script was imported, and test P2SH script import. (Andrew Stone)
- `19aa0660b` [dev branch] Cherry  pick from BitcoinCash branch - remove CBitcoinAddress (#880) (Peter Tschipper)
- `f0179da42` add RPC call that shows all address forms given any address. (Andrew Stone)
- `f0179da42` Add limited support for bitpay address formats: accept bitpay addresses and add to the show RPC (#883) (Andrew Stone)
- `268c3a6ae` Add helper function to use in SendExpeditedBlock (Andrea Suisani)
- `8eeb35f2f` Prevent a race in Xpedited block processing (Awemany)
- `d67331b84` importprivatekeys and importaddresses RPC calls (#879) (Andrew Stone)
- `b3c021c60` Sanity check for max string to encode (#881) (Peter Tschipper)
- `5e864878c` [BitcoinCash branch] cashaddr (#873) (Peter Tschipper)
- `c035717a7` [dev branch] improve the performance of loading the block index (#878) (Peter Tschipper)
- `7cbda799a` Add init message for Reaccepting Wallet Transactions (#877) (Peter Tschipper)
- `431bce6db` Bug Fix for Coin Freeze when making payment request (#875) (Peter Tschipper)
- `fa40e5331` Add two new checkpoints for Cash main chain (#869) (Andrea Suisani)
- `03ce1d3c4` A few updates to free transaction creation and propagation (#864) (Peter Tschipper)
- `74515585b` sync primitives CSharedCriticalSection and CThreadCorral (#867) (Andrew Stone)
- `879f7e231` Green Bitcoin Cash logo (#868) (singularity87)
- `4b29606de` Fix compile issue on some platforms. (#870) (Peter Tschipper)
- `405622277` Trim coin cache by coin height (#812) (Peter Tschipper)
- `9df389cd0` Improve uncaching of coins (#817) (Peter Tschipper)
- `11ff23650` Activate the new DAA by height instead of MTP (#854) (Andrea Suisani)
- `b94048198` New Logging (#847) (johan)
- `d08fb2c63` wallet: Remove unnecessary mempool lock in ReacceptWalletTransactions (#866) (Peter Tschipper)
- `51fd3ca7d` Fix spurious failures in coins_tests.cpp (#865) (Peter Tschipper)
- `fede78c80` Set the default block size on the BitcoinCash chain to 2MB (#861) (Peter Tschipper)
- `701147ba3` fix seg fault that happens sometimes in regtest if you generate a block using bitcoin-qt before you've done anything else with the wallet (#862) (Andrew Stone)
- `e0377b58f` use the proper name of the clang-format program instead of just clang-format (#860) (Andrew Stone)
- `c131ac7b5` Always lock the wallet when adding to the wallet (#859) (Peter Tschipper)
- `84e19fdb1` socks5 QA tests: Make "Address already in use" errors less likely (#858) (awemany)
- `1e006c02b` Change lockIBDstate to fInitialSyncComplete and add comments. (#818) (Peter Tschipper)
- `38d6b0a26` Use CASH net magic by default when establishing outgoing conns (#796) (Andrea Suisani)
- `78d31e89a` remove unnecessary zmq_tests, add invalid value check to SetMerkleBranch (Andrew Stone)
- `e2e4a0e9c` Minor: Replace some GetBoolArg default arguments with constants in main.h (Awemany)
- `13a59b1c1` create a boilerplate .py test that people can use to create their own tests -- this is NOT actually a test (Andrew Stone)
- `65206a139` to support out-of-source builds, we need to also add the generated out-of-source directory (Andrew Stone)
- `d8c948499` handling invalid (wrong fork) headers: revise an unclear comment, and add additional comment (Andrew Stone)
- `c4bab19d3` switch sense of fSendTrickle so the english matches the action (this is not a semantic change) (Andrew Stone)
- `d6df9e0b8` remove unrelated partial optimization (Andrew Stone)
- `8d194e54c` prepend C to class name, remove accidental merge from configure.ac (Andrew Stone)
- `9e1620e5f` Make null pointer check in ContextualCheckBlock explicit (Awemany)
- `cfa29f3d7` remove default values from SyncWallets because it is confusing the compiler on win64 (Andrew Stone)
- `7e230ccf7` BTC/BCC -> BCH take two. (Andrea Suisani)
- `38a03eefe` Update url and hostname for Bitnodes which is now at bitnodes.earn.com. (Simon)
- `241b1f6a0` pass the transaction index into the SyncTransaction notification so that handlers to not need to search through the block to find it (Andrew Stone)
- `30cfee19a` Remove trailing trailing spaces (Andrea Suisani)
- `2367475bb` Move gdb debugger extensions script in `contrib` area (Andrea Suisani)
- `6131b6e8a` giga_perf: use internal random number to withhold invs rather than an expensive hash (Andrew Stone)
- `2e095fd2e` giga_perf: add configure flags that enable gprof and mutrace optimization tools (Andrew Stone)
- `8fd119b36` Download all valid headers up to an invalid header (Peter Tschipper)
- `17cbe337d` remove classic only flags that slipped thru during the pull of this file (Andrew Stone)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Awemany
- goodvdh
- johan
- Peter Tschipper
- Simon
- singularity87

We have backported a significant amount of changes from other projects, namely Bitcoin Core and ABC.

Following all the indirect contributors whose work has been imported via the above backports:

- dooglus
- dagurval
- luke-jr
- Shammah Chancellor (schancel)
- Amaury Séchet (deadalnix)
- Pieter Wuille (sipa)
