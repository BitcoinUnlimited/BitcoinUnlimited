# Enable oracle-based data import via OP_DATASIGVERIFY

Version 0.2, 2018-02-22

## Introduction

OP_DATASIGVERIFY allows signed data to be imported into a script.  This data can then have many uses, depending on the rest of the script, such as deciding spendability of several possible addresses.  This opcode therefore enables the powerful blockchain concept of an "oracle" -- an entity that publishes authoritative statements about extra-blockchain events -- to be used in the Bitcoin Cash blockchain.  For an example use of how this opcode can be used to enable binary contracts on any security or betting on any quantitatively decidable event (such as a sports match) please [click here](https://medium.com/@g.andrew.stone/bitcoin-scripting-applications-decision-based-spending-8e7b93d7bdb9).  But this is just one example; as the Bitcoin Cash Script language grows in expressiveness, it is anticipated that this opcode will be used in many other applications.

## Requirements on miners, full nodes, and clients

Miners, full nodes, and clients will implement the OP_DATASIGVERIFY opcode and activate it during the May 2018 hard fork.

## OP_DATASIGVERIFY Specification

*OP_DATASIGVERIFY uses a new opcode number*

    Opcode (decimal): 187
    Opcode (hex): 0xbb

### Implementation details:
When OP_DATASIGVERIFY is executed, the stack should look like:

*top of stack*
* *pubkeyhash*
* *type and signature*
* *data*

If there are less then 3 items on the stack, the script fails.  If the pubkeyhash field is not 20 bytes, the script fails.  If the *'type and signature'* field is not 66 bytes, the script fails.  If the last byte of the *'type and signature'* field is not DATASIG_COMPACT_ECDSA (1), the script fails.

The last byte of the *'type and signature'* (stack top - 1) field defines the signature type, and previous bytes are the actual signature.  Note that this format is different from the OP_CHECKSIG format -- its SIGHASH flag byte has no relationship to this signature type byte.

OP_DATASIGVERIFY looks at this signature type byte to determine the signature validation algorithm and then executes that algorithm.  There is currently one defined algorithm:

#### value 1: DATASIG_COMPACT_ECDSA

This algorithm is the same operation as the Bitcoin Cash message verification RPC call (e.g signmessage/verifymessage).  The signature must therefore be a pubkey-recoverable signature encoded in bitcoin's "compact" format.  The authoritative specification of this signature is beyond the scope of this document, but in essence it is comprised of a 1 byte recovery ID, 32 bytes "r", 32 bytes "s".

The algorithm first computes the double-sha256 hash of the byte string "Bitcoin Signed Message:\n" prepended to the supplied *data* (stack top - 2).  It then computes a pubkey from this hash and the provided signature (stack top - 1, without the first byte).  If the pubkey cannot be recovered, the script fails.  It then compares the hash160 (same as OP_HASH160) of the recovered pubkey to the provided *pubkeyhash* (stack top).  If the comparison fails, the script fails.

Otherwise, the top 2 items are popped off the stack (leaving *data* on the top of the stack), and the opcode succeeds.

## Examples

* Simplest DATASIGVERIFY:
  * output script:  `pubkeyhash OP_DATASIGVERIFY`
  * spend script: `data sig`

* Realistic example: Verify that `data` is signed by `dpubkeyhash` (signature `daddrsig`) and equals `x`.  Also do normal p2pkh transaction verification on `pubkeyhash` (with signature `txSig` and public key `pubkey`):
  * output script: `dpubkeyhash OP_DATASIGVERIFY x OP_EQUALVERIFY OP_DUP OP_HASH160 pubkeyhash OP_EQUALVERIFY OP_CHECKSIGVERIFY
  * spend script: `txSig pubkey data daddrsig`

## Discussion

### Why is the non-verify version unnecessary?

OP_DATASIGVERIFY fails the script if signature validation fails.  But one could imagine another opcode, let's call it OP_CHECKDATASIG, that would instead push true or false onto the stack depending on whether signature validation suceeded or failed.  Therefore additional opcode would relate to OP_DATASIGVERIFY in the same way OP_CHECKSIG is to OP_CHECKSIGVERIFY.

But, this instruction is not necessary because a Bitcoin Cash script is not exactly like a normal program.  Its fundamental and only purpose is to encumber spendability -- your input script must meet the output script's requirements in order to spend the coins.  Therefore, any program sequence that does not narrow the group of possible spenders is equivalent to a simple choice.  In the OP_CHECKDATASIG case, any potential spender can create a spend script that fails OP_CHECKDATASIG by pushing random bytes rather than a valid signature, so the "false" result is equivalent to (but less efficient than) an if statement on a pushed constant.

Formally, any script with encumberances x, y, z and solutions x', y', and z', of the form:

`pubkeyhash OP_CHECKDATASIG OP_IF x OP_ELSE y OP_ENDIF z`

if-branch spend: `z' x' data sig`

else-branch spend: `z' y' data badsig`

Can be replaced by the script:

`OP_IF pubkeyhash OP_DATASIGVERIFY x OP_ELSE y OP_ENDIF z`

if-branch spend:  `z' x' data sig 1`

else-branch spend: `z' y' 0`


## Reference Implementation

Please refer to [this github branch](https://github.com/gandrewstone/BitcoinUnlimited/tree/op_datasigverify) for a complete implementation.  Implementation is in src/test/interpreter.cpp/h and unit tests are located at src/test/script_tests.cpp.

The opcode implementation is short enough to include here:
```c++
                case OP_DATASIGVERIFY:
                {
                    if (!enableDataSigVerify)
                        return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
                    if (stack.size() < 3)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                    valtype &data = stacktop(-3);
                    valtype &vchSigAndType = stacktop(-2);
                    valtype &vchAddr = stacktop(-1);

                    if (vchAddr.size() != 20)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                    if (vchSigAndType.size() != 66)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                    if (vchSigAndType[65] == DATASIG_COMPACT_ECDSA)
                    {
                        vchSigAndType.resize(65); // chop off the type byte
                        CHashWriter ss(SER_GETHASH, 0);
                        ss << strMessageMagic << data;

                        CPubKey pubkey;
                        if (!pubkey.RecoverCompact(ss.GetHash(), vchSigAndType))
                            return set_error(serror, SCRIPT_ERR_VERIFY);
                        CKeyID id = pubkey.GetID();
                        if (id != uint160(vchAddr))
                            return set_error(serror, SCRIPT_ERR_VERIFY);
                    }
                    else // No other signature types currently supported
                    {
                        return set_error(serror, SCRIPT_ERR_VERIFY);
                    }
                    popstack(stack);
                    popstack(stack);
                }
                break;

```

