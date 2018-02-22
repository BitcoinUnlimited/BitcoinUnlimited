# Enable oracle-based data import via OP_DATASIGVERIFY

Version 0.1, 2018-02-12 - DRAFT FOR DISCUSSION

## Introduction

OP_DATASIGVERIFY allows signed data to be imported into a script.  This data can then have many uses, depending on the rest of the script, such as deciding spendability of several possible addresses.  This opcode therefore enables the powerful blockchain concept of an "oracle" -- an entity that publishes authoritative statements about extra-blockchain events -- to be used in the Bitcoin Cash blockchain.  For an example use of how this opcode can be used to enable binary contracts on any security or betting on any quantitatively decidable event (such as a sports match) please [click here](https://medium.com/@g.andrew.stone/bitcoin-scripting-applications-decision-based-spending-8e7b93d7bdb9).  But this is just one example; as the Bitcoin Cash Script language grows in expressiveness, it is anticipated that this opcode will be used in many other applications.


## OP_DATASIGVERIFY Specification

*OP_DATASIGVERIFY uses a new opcode number*

    Opcode (decimal): 187
    Opcode (hex): 0xbb

### Implementation details:
When OP_DATASIGVERIFY is executed, the stack should look like:

*top of stack*
* pubkeyhash
* signature
* data

If there are less then 3 items on the stack, the script fails.  If the pubkeyhash field is not 20 bytes, the script fails.  If the signature field is not 65 bytes, the script fails.

OP_DATASIGVERIFY first computes the double-sha256 hash of the byte string "Bitcoin Signed Message:\n" prepended to the supplied data (stack top - 2). This is the same operation as the Bitcoin Cash message signing RPC, and is the same algorithm as is used in OP_CHECKSIGVERIFY.  It then computes a pubkey from this hash and the provided signature (stack top - 1).  If the pubkey cannot be recovered, the script fails.  It then compares the hash160 (same as OP_HASH160) of the recovered pubkey to the provided pubkeyhash (stack top).  If the comparison fails, the script fails.

Otherwise, the top 2 items are popped off the stack (leaving "data" on the top of the stack), and opcode succeeds.

The signature must be a recoverable signature encoded in bitcoin's "compact" format.  This format is used in the standard "signmessage" RPC and its authoritative specification is beyond the scope of this document, but in essence it is comprised of a 1 byte recovery ID, 32 bytes "r", 32 bytes "s".

## Examples

* Simplest DATASIGVERIFY:
  * output script:  `pubkeyhash OP_DATASIGVERIFY`
  * spend script: `data sig`

* Realistic example: Verify that `data` is signed by `dpubkeyhash` (signature `daddrsig`) and equals `x`.  Also do normal p2pkh transaction verification on `pubkeyhash` (with signature `txSig` and public key `pubkey`):
  * output script: `dpubkeyhash OP_DATASIGVERIFY x OP_EQUALVERIFY OP_DUP OP_HASH160 pubkeyhash OP_EQUALVERIFY OP_CHECKSIG
  * spend script: `txSig pubkey data daddrsig`

## Discussion

### Why is the non-verify version (e.g. OP_CHECKDATASIG) unnecessary?

An instruction like OP_CHECKDATASIG would validate the signature on data and push a true/false result on the stack, analguous to OP_CHECKSIG vs OP_CHECKSIGVERIFY.

A Bitcoin Cash script is not exactly like a normal program.  Its fundamental and only purpose is to encumber spendability -- your input script must meet the output script's requirements in order to spend the coins.  Therefore, any program sequence that does not narrow the group of possible spenders is equivalent to a simple choice.  In the OP_CHECKDATASIG case, any potential spender can create a spend script that fails OP_CHECKDATASIG by pushing random bytes rather than a valid signature, so the "false" result is equivalent to (but less efficient than) an if statement on a pushed constant.

Formally, any script with encumberances x, y, z and solutions x', y', and z', of the form:

`pubkeyhash OP_CHECKDATASIG OP_IF x OP_ELSE y OP_ENDIF z`

if-branch spend: `z' x' data sig`

else-branch spend: `z' y' data badsig`

Can be replaced by the script:

`OP_IF pubkeyhash OP_DATASIGVERIFY x OP_ELSE y OP_ENDIF z`

if-branch spend:  `z' x' data sig 1`

else-branch spend: `z' y' 0`


## Reference Implementation

Please refer to [this github branch](https://github.com/gandrewstone/BitcoinUnlimited/tree/op_datasigverify) for a complete implementation.  But the opcode implementation is short enough to include here:
```c++
// This code sits inside the interpreter's opcode processing case statement
case OP_DATASIGVERIFY:
{
    if (stack.size() < 3)
        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

    valtype &data = stacktop(-3);
    valtype &vchSig = stacktop(-2);
    valtype &vchAddr = stacktop(-1);

    if (vchAddr.size() != 20)
        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic << data;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return set_error(serror, SCRIPT_ERR_VERIFY);
    CKeyID id = pubkey.GetID();
    if (id != uint160(vchAddr))
        return set_error(serror, SCRIPT_ERR_VERIFY);
    popstack(stack);
    popstack(stack);
}
break;

```
