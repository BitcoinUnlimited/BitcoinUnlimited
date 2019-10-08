[Website](https://www.bitcoinunlimited.info)  | [Download](https://www.bitcoinunlimited.info/download) | [Setup](../README.md)   |   [Miner](miner.md)  |  [ElectronCash](bu-electrum-integration.md)  |  [UnconfirmedChains](unconfirmedTxChainLimits.md)

# Unconfirmed Transaction Chain Limits

This release of Bitcoin Unlimited is aware of the mempool admission policies of other Bitcoin Cash nodes pertaining to unconfirmed transaction chain limits.  If enabled, Bitcoind will only forward transactions to nodes that will accept them based on the communicated limits.  Limits are communicated via the XVERSION (extended version and configuration) protocol message.  If a full node does not support XVERSION, the code assumes that it is using the BCH-standard transaction chain limits of 25 ancestors or descendants, and 101K total ancestor or descendant bytes.

Once a transaction is acceptable to a peer (if some of its parents are confirmed, for example) this node will forward the transaction to that peer.  Depending on configuration, it may do so via an INV message or by sending a TX message.  While the INV method saves network bandwidth, the direct TX message method is faster.  Some BCH full nodes also check for "recently seen" INV messages and reject repeat messages without further analysis.  However, this may cause the node to reject an INV that it should not.  If the INV is sent too soon, the node will first reject the transaction due to its mempool admission policies.  Later, when its mempool would accept the transaction, it never analyzes it because it rejects the INV as "recently seen".  It is therefore recommended to use the direct TX message technique for most applications.

## Configuration

To enable this feature, an operator would add configuration into bitcoin.conf (or on the command line, similarly to any other configuration option).
Configuration to set unconfirmed transaction chain limits has existed for years. In review:
```
limitancestorsize=<KB of RAM>
limitdescendantsize=<KB of RAM>
limitancestorcount=<number of allowed ancestors>
limitdescendantcount=<number of allowed descendants>
```

The descendant size and count fields are not relevant for this application because a transaction that is being considered for mempool inclusion cannot have any descendants in the mempool.  It is recommended to set the ancestor fields to the same values as the descendant fields.

Configuration to disable "intelligent" unconfirmed transaction forwarding.  This is the default:

```
net.unconfChainResendAction=0
```

Configuration to enable "INV" based forwarding:
```
net.unconfChainResendAction=1
```

Configuration to enable "TX" based forwarding.  This is the recommended value, if enabled:
```
net.unconfChainResendAction=2
```
