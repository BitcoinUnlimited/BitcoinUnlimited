# BIP135 Guide

There is no consensus for the November hardfork. Both BitcoinABC (ABC) and BitcoinSV (SV) plan to activate a set of specific rule changes on November 15th.  Although Bitcoin Unlimited has been involved in the development of some of these features, the impetus to force contentious hard forks is coming from the ABC and SV groups, so in this document we will call them the _ABC feature set_ and the _SV feature set_ for simplicity, without implying sole authorship.

Bitcoin Unlimited (BU) provides an option to compromise. The interim release (1.5.0.0) enables BIP135 voting for each of the proposed changes, and contains the ABC feature set.  The next release will soon enable further configuration options and deliver the SV feature set.

BIP135 voting is only meant for miners, since it only affects the **_version_** field in mined blocks.


## BIP135 VOTING

With BIP135 a miner can vote for individual features of the ABC and SV November forks. This is meant as an option to let miners compromise on the incompatible forks, for example by activating uncontroversial features but delaying controversial changes.

It is important to understand that a BIP135 vote for a feature is a vote of **_NO_** for the November fork, but a vote of **_Yes_** for that feature to be activated via BIP135 at some later time.  

**If you do not support contentious hard forks, please start using BIP135 voting!**  Even if you are mining with a different client, your mining pool can do so by changing the block‘s **_version_** field.

The voting is configured to require a 3 month period of 75% hash power voting yes, and then a 3 month "grace" period (where developers can implement the feature if they have not already done so).  So the earliest a feature can activate is 6 months from now.

The configuration is defined in `config/forks.csv` in the source tree, and `share/forks.csv` in the binary distribution. 

chainname | bit | name | starttime | timeout | windowsize | threshold | minlockedblocks | minlockedtime | gbtforce
--- | --- | --- | --- | --- | --- | --- | --- | --- | ---
main | 1 | block_max_size_128mb | 1535760000 | 1567296000 | 12960 | 9720 | 0 | 7776000 | true
main | 2 | opcodes_mul_shift_invert | 1535760000 | 1567296000 | 12960 | 9720 | 0 | 7776000 | true
main | 3 | unrestricted_script_instructions | 1535760000 | 1567296000 | 12960 | 9720 | 0 | 7776000 | true
main | 4 | op_checkdatasig | 1535760000 | 1567296000 | 12960 | 9720 | 0 | 7776000 | true
main | 5 | tx_min_size_100 | 1535760000 | 1567296000 | 12960 | 9720 | 0 | 7776000 | true
main | 6 | enforce_CTOR | 1535760000 | 1567296000 | 12960 | 9720 | 0 | 7776000 | true
main | 7 | enforce_scriptsig_push_only | 1535760000 | 1567296000 | 12960 | 9720 | 0 | 7776000 | true

To enable the configuration:
Copy forks.csv to `~/.bitcoin` (your bitcoin directory). Set the following in your bitcoin.conf:
`mining.vote=<feature>,<feature>,...`

For example to vote for 128MB and CDS:
`mining.vote=op_checkdatasig,block_max_size_128mb`

You can also enable and disable the bits with: 
`./bitcoin-cli set "mining.vote=block_max_size_128mb, enforce_scriptsig_push_only"`

To disable all votes:
```./bitcoin-cli set mining.vote=```

BU has created a method to enable a feature right now as an emergency intervention to stay on the mainchain. In 1.5.0.0 it is only active for CTOR, but configuration will be available for the other features in a subsequent release:
`consensus.enableCanonicalTxOrder=1`

### ABC HARD FORK

The default setting complies with the November fork time as specified by ABC. This fork will be activated by ABC miners at November 15th. 

Today, ABC miners represent the majority of the hash rate. 

This is expressed in the default setting:
`consensus.forkNov2018Time=1542300000`
1542300000 being the timestamp of Thu Nov 15 17:40:00 UTC 2018

You can disable it (so your node will NOT follow the Nov 15 fork) in your config-file: 
`consensus.forkNov2018Time=0`

Or you can enable / disable it in real time via `bitcoin-cli set consensus.forkNov2018Time=...`

It is perhaps confusing that the ABC feature set configuration flag is called `consensus.forkNov2018Time`, but SV may also be forking in November.  Unfortunately this is because the ABC fork is the continuation of a series of bi-annual previously non-contentious forks and so the fork date was set long ago.   

### SV HARD FORK

It has not yet been announced when or how SV will fork, to the specificity required for implementation.  Enabling the SV hard fork will be delivered in a subsequent release, as soon as we learn the details.

## COMPATIBILITY OF FORK-SETTING AND BIP135 VOTES

BIP135 will not activate any feature at November 15th. It has a 3 month voting window and a 3 month grace period. 

If the November-Fork is activated, voting for a feature of it with BIP135 will activate it again 6 month later.  This will have no effect since the feature is already activated.

Disabling the November-Fork but enabling the November-fork features with BIP135 is a vote AGAINST the November hard-fork as a bundle, but FOR the specific feature to be activated via bip135 at some later time. 

Disabling the November-Fork and not voting for a feature with BIP135 is a vote against the November hard-fork and AGAINST the feature to be activated via BIP135 later.

BU aims to enable miners to vote about feature before the hard fork and to prevent a chain-split or a centralized enforcement of a fork. Both can have desastrous effects on the market and the community. A feature activation with BIP135 is prefered by BU and BitcoinXT. It enables the market to not just decide between _ABC_ and _SV_, but take features of both.


Attachment: BIP135-based feature voting
--------------------------------------------------

BIP135 is an enhancement of BIP9, allowing miners to vote for features by setting certain bits in the version field of solved blocks.

The definition of the meaning of the bits in the version field changes and is found in config/forks.csv.  You may define your own bits, however such a definition is not valuable unless the vast majority of miners agree to honor those bit definitions.

Detailed information is available in doc/bip135-genvoting.md.

Miners may enable voting for certain features via the "mining.vote" configuration parameter.  Provide a comma separate list of feature names.

For example, if forks.csv defines three features "f0", "f1" and "f2", you might vote for "f1" and "f2" via the following configuration setting:


This parameter can be accessed or changed at any time via the "get" and "set" RPC calls.
