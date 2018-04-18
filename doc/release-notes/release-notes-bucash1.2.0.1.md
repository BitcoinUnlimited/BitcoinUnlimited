Release Notes for Bitcoin Unlimited Cash Edition 1.2.0.1
=========================================================

Bitcoin Unlimited Cash Edition version 1.2.0.1 is now available from:

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

In case you decide to downgrade from BUcash 1.2.0.1 to a version older than 1.1.2.0
will need to run the old release using `-reindex-chainstate` option so that the
UTXO will be rebuild using the previous format.

Main Changes
------------

- new and more performant logging system
- optimize and strengthen connections handling code
- update icons and images to reflect Bitcoin Cash style/layout
- Stop using NODE_BITCOIN_CASH service bit for preferential peering
- New RPC command rollbackchain
- update univalue and tidyformat subtrees
- simplify code around Aug 1st 17 and Nov 13th 17 upgrades

Commit details
--------------

- `72a18bde3` Reinstate CASH service bit. (sickpig)
- `85f5bd69e` Last pass in removing BITCOIN_CASH ifdef (sickpig)
- `e10a26950` Always enforce LOW_S and NULLFAIL if 13th Nov '17 HF is active (sickpig)
- `395ad4137` Remove all BITCOIN_CASH ifdefs from Qt related code (sickpig)
- `7c4c4208c` Support only CASH nodes seeders (mainnet and testnet) (sickpig)
- `1acbb5f97` Remove last instance fo BITCOIN_CASH from unlimited.h (sickpig)
- `22d268c36` Remove BITCOIN_CASH_FORK_HEIGHT from unlimited.h enum (sickpig)
- `af45aa670` Always use BCH as ticker in estimatesmartfee RPC call (sickpig)
- `80e499922` Remove newdaaactivationtime from allowed args list (sickpig)
- `118a0afd1` Remove fUsesCashMagic logic (sickpig)
- `ade8a0fbe` Remove NODE_BITCOIN_CASH service bit (sickpig)
- `7bf9e20d7` update yml files for date and version (gandrewstone)
- `144a325c6` improved the LOG macro and cleaned up a few things (goodvdh)
- `6f83dce74` Icons (#940) (gandrewstone)
- `514d59f0e` Source image location and splash screen (#938) (gandrewstone)
- `b30522b62` Add a test for rollbackchain to blockchain.py (ptschip)
- `34fe76f6e` clangformatted files that has poor formatting of (mostly) LOG statements (goodvdh)
- `29280640a` Continue to the next node after getting a socket error (#933) (ptschip)
- `e2160321c` New RPC feature : rollbackchain (#931) (ptschip)
- `d936ae5b6` fix bitcoin-qt compilation error and send LOGA logs to the debug.log file (gandrewstone)
- `c08b894dc` Simplify UAHF activation code even further (sickpig)
- `e6baaff5a` Change launchpad link from bu-ppa to bucash (Søren Bredlund Caspersen)
- `d2d054d86` Changed to new log system: Removed old log functions. Changed category labels. Modified log functions to work with new labels. (goodvdh)
- `587b2c67f` update version to 1.2.0.1 (gandrewstone)
- `cb2956de9` Temporarily disable bitnodes seeding on the BitcoinCash chain (ptschip)
- `06e5f5c7d` [Logging] - Move socket errors below net category (Justaphf)
- `bf3dd00ca` Add unit tests for time-based fork helper functions (gandrewstone)
- `9741e7752` Add a CTweaks to set timestamp to use in time based fork (sickpig)
- `1c5a4f6f4` Add helper functions to implement time based fork (sickpig)
- `87b381902` Substitute any remaining BUIP055 comment string instances with UAHF (sickpig)
- `81b710ce1` Rename buip055_test.cpp into uahf_test.cpp (sickpig)
- `de53e84d5` Rename buip055fork.cpp and buip055fork.h into uahf_fork.{cpp,h} (sickpig)
- `28729e896` Substitute BUIP055 with UAHF in all fork helpers name (sickpig)
- `1978a9d63` Rename buip055ChainBlock boolean helper to uahfChainBlock (sickpig)
- `70dba3cf9` Run UAHF and new DAA hardforks code unconditionally (sickpig)
- `9d2f09762` Rename fork helpers to indicate they're related to UAHF (sickpig)
- `c9d0d9d7d` Remove unused forkTime and onlyRelayForkSig tweaks (sickpig)
- `30bdcd703` do not rely on onlyRelayForkSig tweaks (sickpig)
- `2870a0b0f` Reject transactions that won't work on the fork by height (sickpig)
- `bd16c3c1c` Move for helper from CBlockIndex to buip055fork.{cpp,h} (sickpig)
- `10eda7ce5` Add uahfHeight to the consensus parameters. (sickpig)
- `81bd27014` Add some comments to further explain fork activation helpers (sickpig)
- `fd1549ba2` Replace BOOST_FOREACH and NULL with C++11 notation, in src/net.cpp (ptschip)
- `46fbffbb4` Replace BOOST_FOREACH with C++11 for loops in ThreadSocketHandler() (ptschip)
- `01cd1c98c` Use a set to track socket handles (ptschip)
- `e90fd2c7d` Create .gitattributes file for auto-formatting of files (#631) (awemany)
- `15be0a8a1` Initialize pindexNewTip at declaration time (sickpig)
- `750d2dcf9` Cleanup Bitcoinunits within the Qt code (#910) (Greg Griffith)
- `07bad16e9` removed the define in policy as requested (Greg Griffith)
- `356a910c7` fix formatting and add two new files to .formatted-files (ptschip)
- `63faf8ad2` Use C++11 for instead of BOOST_FOREACH (ptschip)
- `8bd06eb74` MOVEONLY: tx functions to consensus/tx_verify.o (Jorge Timón)
- `dde2d83c4` Tidy up Transaction Details and clearly indicate what the label is. (#907) (ptschip)
- `ad23cdb34` no longer a need for this switch (Greg Griffith)
- `caa2c065e` Format `src/util.h` according to BU coding style rules (sickpig)
- `47ba4dac3` Port Core's #11573: [Util] Update tinyformat.h (laanwj)
- `db75b3d3c` Declare single-argument (non-converting) constructors "explicit" (practicalswift)
- `50fd5ca5f` Port Core's #9963: util: Properly handle errors during log message formatting (laanwj)
- `ded146f67` Port Core's #9417: Do not evaluate hidden LogPrint arguments (laanwj)
- `6ba1f97cd` Port of Core #8274: util: Update tinyformat (laanwj)
- `1b39d1568` Don't format tinyformat.h (#902) (sickpig)
- `43899c009` Fix for #872 - Unable to edit address or label (#898) (ptschip)
- `cc951d4ff` Encoding issue (#896) (sickpig)
- `350c39cbe` Port copyright tool from Core repo. (#895) (sickpig)
- `37576085c` Fix for #846: remove extra space between year and month in debug ui. (ptschip)
- `16e55d2df` Add Satoshi Nakamoto copyright back. (sickpig)
- `c268e28ad` Port of core #11952: [qa] univalue: Bump subtree (laanwj)
- `817813397` Port of Core #11420: Bump univalue subtree and fix json formatting in tests (laanwj)
- `3da975bc2` add formatting for consensus and policy directories (BitcoinUnlimited Janitor)
- `c1efeb61a` format files as per our standard (BitcoinUnlimited Janitor)
- `bbfe2b47f` Add release notes for BUCash 1.2.0.0 (sickpig)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani (sickpig)
- Andrew Stone (gandrewstone)
- awemany (awemany)
- goodvdh (goodvdh)
- Greg Griffith
- Justaphf (Justaphf)
- Peter Tschipper (ptschip)
- Søren Bredlund Caspersen (Søren Bredlund Caspersen)

We have backported an amount of changes from other projects, namely Bitcoin Core.

Following all the indirect contributors whose work has been imported via the above backports:

- Wladimir J. van der Laan (laanwj)
- Jorge Timón (Jorge Timón)
- practicalswift (practicalswift)
