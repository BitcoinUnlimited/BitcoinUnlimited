#!/usr/bin/env python3
# blocktools.py - utilities for manipulating blocks and transactions
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import pdb
import binascii
import random

from .mininode import *
from .script import CScript, OP_TRUE, OP_CHECKSIG, OP_DROP, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG, OP_RETURN, OP_NOP
from .util import BTC

# Minimum size a transaction can have.
MIN_TX_SIZE = 100

# Maximum bytes in a TxOut pubkey script
MAX_TXOUT_PUBKEY_SCRIPT = 10000

# Create a block (with regtest difficulty)
def create_block(hashprev, coinbase, nTime=None, txns=None, ctor=True):
    block = CBlock()
    if nTime is None:
        import time
        block.nTime = int(time.time()+600)
    else:
        if type(nTime) is not int:
            raise ValueError("nTime should be int, got {}".format(type(nTime)))
        block.nTime = nTime
    if type(hashprev) is str:
        hashprev = int(hashprev, 16)
    block.hashPrevBlock = hashprev
    block.nBits = 0x207fffff # Will break after a difficulty adjustment...
    if coinbase:
        block.vtx.append(coinbase)
    if txns:
        if ctor:
            txns.sort(key=lambda x: x.hash)
        block.vtx += txns
    block.hashMerkleRoot = block.calc_merkle_root()
    block.calc_sha256()
    return block

def make_conform_to_ctor(block):
    for tx in block.vtx:
        tx.rehash()
    block.vtx = [block.vtx[0]] + \
        sorted(block.vtx[1:], key=lambda tx: tx.getHash())

def serialize_script_num(value):
    r = bytearray(0)
    if value == 0:
        return r
    neg = value < 0
    absvalue = -value if neg else value
    while (absvalue):
        r.append(int(absvalue & 0xff))
        absvalue >>= 8
    if r[-1] & 0x80:
        r.append(0x80 if neg else 0)
    elif neg:
        r[-1] |= 0x80
    return r

# Create a coinbase transaction, assuming no miner fees.
# If pubkey is passed in, the coinbase output will be a P2PK output;
# otherwise an anyone-can-spend output.
def create_coinbase(height, pubkey = None, scriptPubKey = None):
    assert not (pubkey and scriptPubKey), "cannot both have pubkey and custom scriptPubKey"
    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff),
                ser_string(serialize_script_num(height)), 0xffffffff))
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = 50 * COIN
    halvings = int(height/150) # regtest
    coinbaseoutput.nValue >>= halvings
    if (pubkey != None):
        coinbaseoutput.scriptPubKey = CScript([pubkey, OP_CHECKSIG])
    else:
        if scriptPubKey is None:
            scriptPubKey = CScript([OP_NOP])
        coinbaseoutput.scriptPubKey = CScript(scriptPubKey)
    coinbase.vout = [ coinbaseoutput ]

    # Make sure the coinbase is at least 100 bytes
    coinbase_size = len(coinbase.serialize())
    if coinbase_size < 100:
        coinbase.vin[0].scriptSig += b'x' * (100 - coinbase_size)

    coinbase.calc_sha256()
    return coinbase

# Create a transaction with an anyone-can-spend output, that spends the
# nth output of prevtx.  pass a single integer value to make one output,
# or a list to create multiple outputs
PADDED_ANY_SPEND =  b'\x61'*50 # add a bunch of OP_NOPs to make sure this tx is long enough
def create_transaction(prevtx, n, sig, value, out=PADDED_ANY_SPEND):
    prevtx.calc_sha256()
    if not type(value) is list:
        value = [value]
    tx = CTransaction()
    assert(n < len(prevtx.vout))
    tx.vin.append(CTxIn(COutPoint(prevtx.sha256, n), sig, 0xffffffff))
    for v in value:
        tx.vout.append(CTxOut(v, out))
    tx.calc_sha256()
    return tx


def bitcoinAddress2bin(btcAddress):
    """convert a bitcoin address to binary data capable of being put in a CScript"""
    # chop the version and checksum out of the bytes of the address
    return decodeBase58(btcAddress)[1:-4]


B58_DIGITS = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'


def decodeBase58(s):
    """Decode a base58-encoding string, returning bytes"""
    if not s:
        return b''

    # Convert the string to an integer
    n = 0
    for c in s:
        n *= 58
        if c not in B58_DIGITS:
            raise InvalidBase58Error('Character %r is not a valid base58 character' % c)
        digit = B58_DIGITS.index(c)
        n += digit

    # Convert the integer to bytes
    h = '%x' % n
    if len(h) % 2:
        h = '0' + h
    res = binascii.unhexlify(h.encode('utf8'))

    # Add padding back.
    pad = 0
    for c in s[:-1]:
        if c == B58_DIGITS[0]:
            pad += 1
        else:
            break
    return b'\x00' * pad + res

def createWastefulOutput(btcAddress):
    """ Warning: Creates outputs that can't be spent by bitcoind"""
    data = b"""this is junk data. this is junk data. this is junk data. this is junk data. this is junk data.
this is junk data. this is junk data. this is junk data. this is junk data. this is junk data.
this is junk data. this is junk data. this is junk data. this is junk data. this is junk data."""
    ret = CScript([data, OP_DROP, OP_DUP, OP_HASH160, bitcoinAddress2bin(btcAddress), OP_EQUALVERIFY, OP_CHECKSIG])
    return ret


def p2pkh(btcAddress):
    """ create a pay-to-public-key-hash script"""
    ret = CScript([OP_DUP, OP_HASH160, bitcoinAddress2bin(btcAddress), OP_EQUALVERIFY, OP_CHECKSIG])
    return ret


def createrawtransaction(inputs, outputs, outScriptGenerator=p2pkh):
    """
    Create a transaction with the exact input and output syntax as the bitcoin-cli "createrawtransaction" command.
    If you use the default outScriptGenerator, this function will return a hex string that exactly matches the
    output of bitcoin-cli createrawtransaction.

    But this function is extended beyond bitcoin-cli in the following ways:
    inputs can have a "sig" field which is a binary hex string of the signature script
    outputs can be a list of tuples rather than a dictionary.  In that format, they can pass complex objects to
    the outputScriptGenerator (use a tuple or an object), be a list (that is passed to CScript()), or a callable
    """
    if not type(inputs) is list:
        inputs = [inputs]

    tx = CTransaction()
    for i in inputs:
        sigScript = i.get("sig", b"")
        tx.vin.append(CTxIn(COutPoint(i["txid"], i["vout"]), sigScript, 0xffffffff))
    pairs = []
    if type(outputs) is dict:
        for addr, amount in outputs.items():
            pairs.append((addr,amount))
    else:
        pairs = outputs

    for addr, amount in pairs:
        if callable(addr):
            tx.vout.append(CTxOut(amount * BTC, addr()))
        elif type(addr) is list:
            tx.vout.append(CTxOut(amount * BTC, CScript(addr)))
        elif addr == "data":
            tx.vout.append(CTxOut(0, CScript([OP_RETURN, unhexlify(amount)])))
        else:
            tx.vout.append(CTxOut(amount * BTC, outScriptGenerator(addr)))
    tx.rehash()
    return hexlify(tx.serialize()).decode("utf-8")


def pad_tx(tx, pad_to_size=MIN_TX_SIZE):
    """
    Pad a transaction with op_return junk data until it is at least pad_to_size, or
    leave it alone if it's already bigger than that.
    """
    curr_size = len(tx.serialize())
    if curr_size >= pad_to_size:
        # Bail early txn is already big enough
        return

    # This code attempts to pad a transaction with opreturn vouts such that
    # it will be exactly pad_to_size.  In order to do this we have to create
    # vouts of size x (maximum OP_RETURN size - vout overhead), plus the final
    # one subsumes any runoff which would be less than vout overhead.
    #
    # There are two cases where this is not possible:
    # 1. The transaction size is between pad_to_size and pad_to_size - extrabytes
    # 2. The transaction is already greater than pad_to_size
    #
    # Visually:
    # | .. x  .. | .. x .. | .. x .. | .. x + desired_size % x |
    #    VOUT_1     VOUT_2    VOUT_3    VOUT_4
    # txout.value + txout.pk_script bytes + op_return
    extra_bytes = 8 + 1 + 1
    required_padding = pad_to_size - curr_size
    while required_padding > 0:
        # We need at least extra_bytes left over each time, or we can't
        # subsume the final (and possibly undersized) iteration of the loop
        padding_len = min(required_padding,
                          MAX_TXOUT_PUBKEY_SCRIPT - extra_bytes)
        assert padding_len >= 0, "Can't pad less than 0 bytes, trying {}".format(
            padding_len)
        # We will end up with less than 1 UTXO of bytes after this, add
        # them to this txn
        next_iteration_padding = required_padding - padding_len - extra_bytes
        if next_iteration_padding > 0 and next_iteration_padding < extra_bytes:
            padding_len += next_iteration_padding

        # If we're at exactly, or below, extra_bytes we don't want a 1 extra byte padding
        if padding_len <= extra_bytes:
            tx.vout.append(CTxOut(0, CScript([OP_RETURN])))
        else:
            # Subtract the overhead for the TxOut
            padding_len -= extra_bytes
            padding = random.randrange(
                1 << 8 * padding_len - 2, 1 << 8 * padding_len - 1)
            tx.vout.append(
                CTxOut(0, CScript([OP_RETURN, padding])))

        curr_size = len(tx.serialize())
        required_padding = pad_to_size - curr_size
    assert curr_size >= pad_to_size, "{} !>= {}".format(curr_size, pad_to_size)
    tx.rehash()

def pad_raw_tx(rawtx_hex, min_size=MIN_TX_SIZE):
    """
    Pad a raw transaction with OP_RETURN data until it reaches at least min_size
    """
    tx = CTransaction()
    FromHex(tx, rawtx_hex)
    pad_tx(tx, min_size)
    return ToHex(tx)

def create_tx_with_script(prevtx, n, script_sig=b"",
                          amount=1, script_pub_key=CScript()):
    """Return one-input, one-output transaction object
       spending the prevtx's n-th output with the given amount.

       Can optionally pass scriptPubKey and scriptSig, default is anyone-can-spend output.
    """
    tx = CTransaction()
    assert(n < len(prevtx.vout))
    tx.vin.append(CTxIn(COutPoint(prevtx.sha256, n), script_sig, 0xffffffff))
    tx.vout.append(CTxOut(amount, script_pub_key))
    pad_tx(tx)
    tx.calc_sha256()
    return tx

