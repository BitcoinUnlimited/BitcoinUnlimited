# OP_GROUP Tokens

Version 0.1, 2018-02-04 - DRAFT

Author: Andrew Stone
Contact: g.andrew.stone@gmail.com, keybase.io: andrewstone

## Introduction

OP_GROUP tokens are a method for implementing representative tokens -- also named "colored coins" within the bitcoin community.

OP_GROUP tokens differ in significant aspects from other existing or proposed "colored coins" techniques:

* Implemented as a single opcode within Bitcoin Cash's Script language.
	 + Integration with Script means that OP_GROUP functionality will grow as Script grows, and maintenance is minimal compared to colored coins that use "piggy-back" blockchains.

* Is miner validated
	 + Like BCH transactions, light wallets can rely on the idea that transactions committed some blocks behind the blockchain tip are valid

* Does not require knowledge of the blockchain history to determine token grouping
	 + Compatible with pruning and forthcoming UTXO commitment techniques

* Simple "light" wallet implementation
	 + Can use the same merkle proof technique (SPV or "simplified payment validation") used for transactions today.

* Can be enabled via soft fork
	+ Wallets do not need to add OP_GROUP functionality immediately.  Wallets that are not upgraded or choose not to support OP_GROUP will likely not recognise tokens sent to the wallet and cannot accidentally spend these tokens as BCH in "normal" scripts.

## The format of this document

This document uses the term **MUST** to indicate a rule that must be followed by all implementations.
The term **MAY** indicates optional functionality. However, if optional functionality is implemented it is highly recommended that be implemented as defined here for interoperability with other software.

*[This document places the "why" -- the discussion concerning the reasons and justifications of certain decisions -- in bracketed italics.  Text in italics is informative, not normative]*

For the purposes of discussion of this draft additional notes are contained throughout in marked "DISCUSSION" and formatted in bold brackets **[DISCUSSION: like this]**.  These are
intended to be removed from the final version of this document.


## Risks and philosophical approach

The purpose of OP_GROUP is to enable colored coins on the Bitcoin Cash blockchain and in SPV (phone) wallets, covering major use cases with a fraction of the development effort and maintenance of other token proposals.  These use cases include ICOs (initial coin offerings), representative tokens for stocks, bonds and currencies, and other uses I haven't considered.  But these use cases notably do not include "smart" contracts, since the introduction of a sophisticated programming language would add tremendous complexity and competes with the evolving Bitcoin Cash Script language.

This proposal also limits itself to exactly one opcode.  It is possible to include additional functionality via additional opcodes but I believe that the discussion and decision around these possible features should occur once OP_GROUP is successfully deployed and employed. 

## Definitions

* *GP2PKH* - Group pay to public key hash script.  Specifically: `<group id> OP_GROUP OP_DROP OP_DUP OP_HASH160 <address> OP_EQUALVERIFY OP_CHECKSIG`
* *GP2SH* - Group pay to script hash script.  Specifically: `<group id> OP_GROUP OP_DROP OP_HASH160 <address> OP_EQUAL`
* *mint-melt address* - An address that can be used to mint or melt tokens.  This is actually the same number as the group identifier.
* *group identifier* - A number used to identify a group.  This is the same number as the group's mint-melt address, but it uses a cashaddr type of 2.
* *bitcoin cash group* - A special-case group that includes all transactions with no explicit group id.  This group represents the "native" BCH tokens during transaction analysis.
* *mint* - Move ungrouped satoshis into a group, thereby creating new tokens
* *melt* - Move tokens back to the native satoshis, thereby destroying existing tokens

## Theory of Operation

Please refer to [https://medium.com/@g.andrew.stone/bitcoin-scripting-applications-representative-tokens-ece42de81285](https://medium.com/@g.andrew.stone/bitcoin-scripting-applications-representative-tokens-ece42de81285).

## Specification

As a soft fork, this specification is divided into 2 sections: miner (consensus) requirements, and wallet implementations

### Miner (Consensus) Requirements

#### Deployment
Miners **MUST** enforce OP_GROUP semantics after the May 2018 hard fork.

#### Validation

A new Script opcode **MUST** be created, named OP_GROUP and replacing OP_NOP7 (0xb6).

This opcode **MUST** behave as a no-op during script evaluation. It therefore may legally appear anywhere in a script. *[Its simpler and less error prone to let it appear anywhere and ignore it (as per the OP_NOP7 semantics today) than to add a consensus rule enforcing its proper appearance.  Having such a rule would be a place where implementation could fall out of consensus.  I believe that consensus rules should be minimal and be those that protect or enhance the monetary function of BCH.  Limiting extraneous appearances of OP_GROUP does neither]*

This opcode comes into play during transaction validation, and ensures that the quantity of input and outputs within every group balance.

First a *"mint-melt group"* and a *"group identifier"* are identified for each input and output.

The *group identifier* is the token group that this input or output belongs to.  Transactions that do not use OP_GROUP are put in a special group called the "bitcoin cash group" that designates the "native" BCH token.

Inputs may also have a *mint-melt group* depending on their construction.  The *mint-melt group* indicates the ability to either mint or melt tokens into or from the corresponding group.

Identification proceeds in the following manner:

**For all inputs:**

The mint-melt group and group identifier is the same as that of the "previous output" (specified in the input by its transaction id and index).
*[The prevout is already needed for script validation and transaction signing, and is located in the UTXO database so this backpointer adds no additional data storage requirements on nodes or wallets]*

**For all outputs:**

To specify a *group identifier*, a script **MUST** begin with the following exact format:  `<group identfier> OP_GROUP ...`

In words, If a script begins with any form of data push (i.e. length codes 1 through 0x4b, OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4) followed by OP_GROUP, the *group identifier* is the data pushed.  This sequence **MUST** begin the script and there **MUST NOT** be other opcodes between the *group identifier* data push and the OP_GROUP instruction. 

If the script does not meet the above specification, its *group identifier* is the bitcoin cash group.

*[It is necessary to identify the group without executing the script so the group cannot be located within conditional code.  The simplest solution is to put it first]*

If a script is a P2PKH, P2SH, or GP2PKH (group P2PKH) or GP2SH, the *mint-melt group* is the public key hash or script hash.  For complete clarity, these are the only scripts that have a *mint-melt group*:
> **P2PKH**: `OP_DUP  OP_HASH160  <mint-melt group>  OP_EQUALVERIFY  OP_CHECKSIG`

> **P2SH**: `OP_HASH160  <mint-melt group>  OP_EQUAL`

> **GP2PKH**: `<data>  OP_GROUP  OP_DROP  OP_DUP  OP_HASH160  <mint-melt group>  OP_EQUALVERIFY  OP_CHECKSIG`

> **GP2SH**: `<data>  OP_GROUP  OP_DROP  OP_HASH160  <mint-melt group>  OP_EQUAL`

As you can see, the mint melt group is another name for the script hash, and public key hash (for these scripts only).

*[It makes OP_GROUP operation much simpler if the mint-melt group can be identified without executing the script.  Therefore, we limit the mintable or meltable input transaction types to well-known script templates.  This does not limit functionality.  Tokens that exist in a nonstandard script can be minted or melted in 2 transactions by first sending them to an output that uses one of the above scripts, and then issuing the mint/melt transaction]*

If a script does not match one of the above templates, it **MUST** have no *mint-melt group*.

##### Transaction Validation algorithm

*[The algorithm below is described pedantically to make it easy to understand how it actually succeeds at balancing group inputs and outputs and how it correctly applies mint and melt coins.  It should be possible to write it much more succinctly -- for example, there is no need for an "input" field.  Instead, you could subtract the output field down to 0]*

Create a data structure (let's call it GroupBalances) that associates group identifiers with 4 amounts named "mintable", "meltable", "input" and "output".  Initialize these to 0.

First we'll find the final quantity of tokens in every group.  We need this so we can match it with inputs to balance the transaction:

* For every transaction output (vout):
  * Identify its *group identifier*.
  * Add the vout's value to the group's "output" field.
  * Do not forget to assign all "ungrouped" output's values to the special "bitcoin cash group"


Next we go through every transaction input (vin) and add its value to the vin group's "input" field, in the same way we did for the output.  However, there is one caveat -- we need to identify whether the value of that input could be used as a mint or melt. In that case we add to the "mintable" or "meltable" field rather than the "input" field:


* For every transaction input (vin), find its *group identifier*, *mint-melt group*, and value:
  * If the *group identifier* is the bitcoin cash group:
    * see if its *mint-melt group* is in GroupBalances.   
      * If yes, add the value to the *mint-melt group*'s "mintable" field.
      * If no, add the value to the the bitcoin cash group's "input" field.
  * If the *group identifier* is NOT the bitcoin cash group:
    * If the *group identifier* is equal to the *mint-melt group*: *[this is a possible melt]*
      *  if the GroupBalances contains that group:
          *  add the value to the GroupBalances *group identifier* "meltable" field
          *  otherwise add the value to the bitcoin cash group's "input" field *[it's a guaranteed melt because the group doesn't exist as an output]*
    * If the *group identifier* is not equal to the *mint-melt group*
      * add the value to the GroupBalances *group identifier* input field *[no melting is possible so this is just a intra-group transfer]*.
      * if the GroupBalances doesn't have a *group identifier* entry, FAIL *[we don't have permission to melt, but the transaction attempts to do it]*

The "mintable" and "meltable" quantities can either apply to their associated group, or to the bitcoin cash group.  So next, we apply the mintable and meltable coins to each group to equalize inputs and outputs within the group.  We place any excess into the bitcoin cash group:

* For every group in GroupBalances (except the bitcoin cash group), compare the "input" to the "output":
  * If the "input" is less, equalize it by moving value from "mintable".  If there is not enough FAIL.
  * If the "output" is less FAIL.
  * Move any left over "mintable" and "meltable" into the "input" field of the bitcoin cash group.

So we've balanced all the groups except the bitcoin cash group.  Last step:

* Check the bitcoin cash group.
  * If its input is less than its output FAIL.
  * Otherwise, the transaction balances, SUCCESS.
  * *[Note that the output may be less than the input... just like normal transactions, what's left over is the mining fee]*


Algorithms with a lot of nested "ifs" are simpler to view in code.  For an example implementation please refer to the CheckTokenGroup() function in this reference implementation:

https://github.com/gandrewstone/BitcoinUnlimited/tree/opgroup_consensus/src/tokengroups.cpp


### Wallet Requirements

#### Group Identifier

Group identifiers can be any number in theory, but in practice it is impossible to mint any token whose identifier is not a bitcoin cash address.  Therefore a group identifier is a 160 bit number today.  In the future, it is likely that P2SH scripts will be redefined to something like "OP_HASH256 [32-byte-hash-value] OP_EQUAL", so wallets should be prepared to accept 256 bit group identifiers in the future.

Group identifiers displayed in cashaddr format **MUST** use the "type" byte as 2.  This results in a cashaddr prefix of "z" for example:
`bitcoincash:zrmn5e26cfkd0j97kx5jdm3jrrzv5l6a0upqxjxkw2`

#### Token Description Document

Additional data **MAY** be placed in an OP_RETURN output and included in a transaction containing a mint operation.  This information **MAY** be used by wallets to provide a more user-friendly interface, and to provide additional information to users, including legal documents.

This data should only be placed in a transaction that mints tokens to a single group.  Otherwise which group the OP_RETURN data applies to is ambiguous, and should be ignored by wallets. *[we could allow multiple pieces of data in OP_RETURN and use a canonical ordering, but this is just the sort of little-used detail that many wallets won't implement.  Its better to keep it simple]*
This data should only be included in one (presumably, but not necessarily, the first) transaction.  Light wallets looking for information on a particular group can be provided with it via a SPV proof of the inclusion of the data-bearing transaction.

The contents of the OP_RETURN output are defined as null separated strings:

`<ticker> <0> <name> <0> <uri>`

It is possible to have an empty field.  For example,

`MYCOIN\0\0http://www.mycoin.com/info`

The `<URI>` field supports http and https protocols and references an "application/json" type document in [I-JSON format](https://tools.ietf.org/html/rfc7493) with fields defined as follows:

```json
[{
  "ticker": "string, required",
  "name": "string, optional",
  "summary": "string, optional",
  "description": "string, optional",
  "legal": "string, optional",
  "creator": "string, optional",
  "contact": { "method": "string, optional", "method2": "string, optional" }
 },
"<signature>"]
```

**signature**: The signature field contains the signature of the preceding dictionary using the group identifier, from open brace to close brace inclusive.  Validators must check this signature against the exact bytes of the document so that spacing is not changed. **[DISCUSSION: how hard is this to implement in various languages?  Is there a better way?]**   The signature algorithm is what is implemented in the Satoshi client's "signmessage" RPC function.  The following description is informative, not authorative:
* Compute the message hash by using the double SHA-256 of the string "Bitcoin Signed Message:\n" + message
* Create a compact ECDSA signature
* Convert to text using base 64 encoding with the following charset: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
*[Wallets and users use this signature to show that the creator of the token is affirming the information in this json document.  And this signature also proves that this document and URL is associated with this group.  By hosting this document, the owner of a domain name is associated with this token.  It is recommended that token issuers use https for the uri so man-in-the-middle attacks cannot be used to make it look like your domain is hosting a token description document]*

**ticker**: Short token name.  Should be 6 characters or less

**name**: Full token name

**summary**: Paragraph description

**description**: Full description

**legal**: Full legal contract between users of this token and the creator, if applicable. **[DISCUSSION: should we allow markdown syntax?]**

**creator**: Who created this token

**contact**: Optional contact information for the creator, for example:

  "contact": { "email": "satoshin@gmx.com" }
  
  Defined methods are: "address", "email", "phone", "twitter.com", "facebook.com"
  
  Authors may add their own methods.  If its a web-enabled service, use the domain name to identify the method.  If its a URI protocol, use the protocol name including the final colon (e.g. "http:": "//www.mywebsite.com/contactform")

Authors may add additional fields not defined in this document to the initial dictionary.

#### Illegal ticker names

If a token claims the following ticker names, it is strongly recommended that wallets refuse to display the ticker and instead use "???":

BCH, mBCH, uBCH

If a token claims the following ticker names, it is recommended that wallets refuse to display the ticker and instead use "???":

Any ISO4217 currency code, any NYSE ticker, any NASDAQ ticker, any symbol from your locale's national securities exchange, other popular crypto-currencies.


*[If 3rd party company or individual is creating "representative tokens" their ticker should reflect that.]*

*[Since it is possible to allow actual currency and securities to be issued on the bitcoin cash blockchain and we should reserve the nationally and internationally known ticker symbols for this future use.  Doing so securely is likely not hard.  By accessing the token description document via a URL, we already have a binding between the token and a domain name secured by SSL Certificates.  All that remains is to bind the domain name to a ticker.  Since this is a relatively small amount of slowly changing information, it could simply be a data file in the wallet]*

## Reference implementation

Consensus:
https://github.com/gandrewstone/BitcoinUnlimited/tree/opgroup_consensus

Full node (adds wallet and RPC functionality to the above consensus branch):
https://github.com/gandrewstone/BitcoinUnlimited/tree/opgroup_fullnode

