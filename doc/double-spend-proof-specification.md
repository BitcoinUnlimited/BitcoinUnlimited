<pre>
  Layer: p2p
  Title: Double Spend Proofs
  Author: Tom Zander <tomz@freedommail.ch>, @im\_uname
  Created: 2019-08-07
  Last Revised: 2019-08-07
  License: CC-BY-SA-4.0
</pre>

## Abstract

This document describes a way to inform participants of attempts of double
spending an unconfirmed transaction by providing cryptographic provable
evidence that one UTXO entry was spent twice by the owner(s) of the funds.

The ability to get informed of such an event can assist greatly in the
confident acceptance of unconfirmed transactions as payment. We expect this
will help with countering undetected attempts of double spends.

## Summary

A double spend attack can be used, for instance, to redirect payments meant for a specific
merchant to a different target and thus defraud the merchant we want to pay to. The basic
concept of a double spend is that (at least) one unspent output is spent
twice in different transactions which forces miners to pick one of them to mine.

At its most basic we can detect this by finding two signed inputs both
spending the same output. In the case of pay-to-public-key-hash (P2PKH)
this means two signatures signing the same public key.

Cryptographic signatures in Bitcoin Cash follow the 'fork-id' algorithm described
[here](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/replay-protected-sighash.md),
which explains a change made to the Satoshi designed algorithm, a change after which the containing transaction itself is not signed, but a unique hash of that
transaction is being signed. This gives us the opportunity to send only
the intermediate hashes instead of the whole transaction while allowing
receivers to still validate both signatures of the same public
key. And therefore prove that a double spend has taken place.

## Limitation and risks

Not all types and all combinations of transactions are supported. Wallets
and point-of-sale applications are suggested to give a rating of how secure
an unconfirmed transaction is based on various factors.

Transactions that spend all, confirmed, P2PKH outputs with all inputs
signed SIGHASH\_ALL without ANYONECANPAY, are double-spend-proof's
"protected transactions".

For more details see "*Using Double Spend Alerts: Merchant considerations*"
below.

## Motivation

The chance of defrauding a merchant by double spending the transaction that is being
paid to him is real and the main problem we have today is the fact that
without significant infrastructure the merchant won't find out until the
block is confirmed.

The low risk of getting caught will make this a problem as Bitcoin Cash
becomes more mainstream.

The double-spend-proof is a means with which network participants that have
the infrastructure to detect double spends can share that fact so merchants
can receive information on their payment app (typically SPV based) in short enough
time that the merchant can refuse to provide service or goods to their
customer.


## Network specification

A node that finds itself in possession of a correct double-spend-proof
shall notify its peers using the INV message, using a 'type' field with
number **0x94a0**. This will be changed to another number as this spec
is finalized.

The hash-ID for the double-spend-proof is a double sha256 over the entire
serialized content of the proof, as defined next.

In response to an INV any peer can issue a `getdata` message which will
cause a reply with the following message. The name of the message is **`dsproof-beta`**.


| Field Size | Description | Data Type  | Comments |
| -----------|:-----------:| ----------:|---------:|
| 32 | TxInPrevHash | sha256 | The txid being spent |
| 4  | TxInPrevIndex | int | The output being spent |
| | DoubleSpendA | spender | the first sorted spender |
| | DoubleSpendB | spender | the second spender |

A double-spend-proof essentially describes two inputs, both spending the
same output. As such the prev-hash and prev-index point to the output and
the spenders each describe inputs.

The details required to validate one input are provided in the spender field;

| Field Size | Description | Data Type  | Comments |
| -----------|:-----------:| ----------:|---------:|
| 4 | tx-version | unsigned int | Copy of the transactions version field |
| 4 | sequence | unsigned int | Copy of the sequence field of the input |
| 4 | locktime | unsigned int | Copy of the transactions locktime field |
| 32 | hash-prevoutputs | sha256 | Transaction hash of prevoutputs |
| 32 | hash-sequence | sha256 | Transaction hash of sequences |
| 32 | hash-outputs | sha256 | Transaction hash of outputs |
| 1-9 | list-size | var-int | Number of items in the push-data list |
|  | push-data | byte-array | Raw byte-array of a push-data. For instance a signature |

## Validation

It is required that nodes validate the proof before using it or forward it to other
nodes. Please check against the matching transaction in your mempool for
addresses so you can limit sending the proof only to interested nodes that
have registered a bloom filter.

Validation includes a short list of requirements;

1. The DSP message is well-formed and contains all fields. It is allowed (by
   nature of Bitcoin Cash) for some hashes to be all-zeros.
2. The two spenders are different, specifically the signature (push data)
   has to be different.
3. The first & double spenders are sorted with two hashes as keys.  
   Compare on the hash-outputs, and if those are equal, then compare on
   hash-prevoutputs.
   The sorting order is in numerically ascending order of the hash,
   interpreted as 256-bit little endian integers.
4. The double spent output is still available in the UTXO database,
   implying no spending transaction has been mined.
5. No other valid proof is known.

Further validation can be done by essentially validating the signature that
was copied from the inputs of both transactions against the output a node
should have in either its memory pool or its UTXO database.

To validate a spender of the proof, a node requires to have;

* The output being spent (mempool or UTXO)
* One of the transactions trying to spend the output.
* The double-spend-proof.

As the forkid
[specification](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/replay-protected-sighash.md)
details, the digest algorithm hashes 10 items in order to receive a sha256
hash, which is then signed.

These 10 items are;

1.  nVersion of the transaction (4-byte little endian)
2.  hashPrevouts (32-byte hash)
3.  hashSequence (32-byte hash)
4.  outpoint (32-byte hash + 4-byte little endian)
5.  scriptCode of the input (serialized as scripts inside CTxOuts)
6.  value of the output spent by this input (8-byte little endian)
7.  nSequence of the input (4-byte little endian)
8.  hashOutputs (32-byte hash)
9.  nLocktime of the transaction (4-byte little endian)
10. sighash type of the signature (4-byte little endian)

In the double spend message we include items: 1, 2, 3, 4, 7, 8, 9 and 10.

From the output we are trying to spend you can further obtain items: 5 & 6

The full transaction also spending the same output which you found in your
mempool, can be used to get the public key which you can use to validate
that the signature is actually correct.

When all rules are followed, the proof is valid.

## Deployment

There will be an immediate benefit to each extra node on the network
creating or propagating these messages.

As the amount of nodes that propagate them reaches around 20% then
statistically there is a very good chance of each node having at least one
participating peer leaving your node fully informed of all double spends.

### Using Double Spend Alerts: Merchant considerations

A merchant node which utilizes double-spend-proofs is advised to follow the following general procedure, when receiving a transaction that pays them whether through network or direct connection (e.g. BIP70):

1. Evaluate the transaction that pays them. If the transaction does not pay sufficient fee or is otherwise unfit as defined by the merchant independent of the criteria below, apply remedial action either by rejecting transaction or custom negotiations with the customer - fast transaction becomes irrelevant.

2. If the transaction does not fit any of the following criteria, do not rely on double-spend-proof, and instead either wait for confirmation or apply more stringent risk management:

The transaction must contain all P2PKH.  
The transaction must either be spending only from confirmed UTXOs, or all of its ancestors in mempool must also be all-P2PKH transactions (Optional, requires BIP62).  
All of the inputs in the relevant transaction, and its mempool ancestor chain (Optional, requires BIP62), must be signed SIGHASH\_ALL without ANYONECANPAY.

3. After affirming that the transaction fits the critera for applying double-spend-proofs, the merchant then waits T seconds, T being a variable adjusted to risk tolerance, for a proof to arrive. If a double-spend-proof corresponding to the paying transaction or any of its ancestors arrive, the merchant shall either decline the payment, or wait for confirmation. If no proof arrives within T seconds, the merchant hands out goods or services.

Note that in the case of an unconfirmed chain, if BIP62 fixes are not implemented in full, transactions can be malleated by third parties to invalidate descendents without triggering double-spend-proof - and any attempt at making proof against this will be vulnerable to false positive attacks. Double-spend-proof against unconfirmed proofs should therefore only be activated after implementation of the full set of BIP62 fixes (https://github.com/bitcoin/bips/blob/master/bip-0062.mediawiki).

## References

The 'forkid' spec Bitcoin Cash uses to validate signatures in transactions;
https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/replay-protected-sighash.md

