# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from ctypes import *
from binascii import hexlify, unhexlify
from enum import IntEnum, IntFlag
import pdb
import hashlib
import decimal

SIGHASH_ALL = 1
SIGHASH_NONE = 2
SIGHASH_SINGLE = 3
SIGHASH_FORKID = 0x40
SIGHASH_ANYONECANPAY = 0x80

MAX_STACK_ITEM_LENGTH = 520

BCH = 100000000

cashlib = None

class Error(BaseException):
    pass


def init(libbitcoincashfile=None):
    global cashlib
    if libbitcoincashfile is None:
        libbitcoincashfile = "libbitcoincash.so"
        try:
            cashlib = CDLL(libbitcoincashfile)
        except OSError:
            import os
            dir_path = os.path.dirname(os.path.realpath(__file__))
            cashlib = CDLL(dir_path + os.sep + libbitcoincashfile)
    else:
        cashlib = CDLL(libbitcoincashfile)
    if cashlib is None:
        raise Error("Cannot find %s shared library", libbitcoincashfile)


# Serialization/deserialization tools
def sha256(s):
    """Return the sha256 hash of the passed binary data

    >>> hexlify(sha256("e hat eye pie plus one is O".encode()))
    b'c5b94099f454a3807377724eb99a33fbe9cb5813006cadc03e862a89d410eaf0'
    """
    return hashlib.new('sha256', s).digest()


def hash256(s):
    """Return the double SHA256 hash (what bitcoin typically uses) of the passed binary data

    >>> hexlify(hash256("There was a terrible ghastly silence".encode()))
    b'730ac30b1e7f4061346277ab639d7a68c6686aeba4cc63280968b903024a0a40'
    """
    return sha256(sha256(s))


def hash160(msg):
    """RIPEMD160(SHA256(msg)) -> bytes"""
    h = hashlib.new('ripemd160')
    h.update(hashlib.sha256(msg).digest())
    return h.digest()


def bin2hex(data):
    """convert the passed binary data to hex"""
    assert type(data) is bytes, "cashlib.bintohex requires parameter of type bytes"
    l = len(data)
    result = create_string_buffer(2 * l + 1)
    if cashlib.Bin2Hex(data, l, result, 2 * l + 1):
        return result.value.decode("utf-8")
    raise Error("cashlib bin2hex error")

def signData(data, key):
    if type(data) == str:
        data = unhexlify(data)
    elif type(data) != bytes:
        data = data.serialize()
    result = create_string_buffer(100)
    siglen = cashlib.SignData(data,len(data),key, result, 100)
    return result.raw[0:siglen]

def signTxInput(tx, inputIdx, inputAmount, prevoutScript, key, sigHashType=SIGHASH_FORKID | SIGHASH_ALL):
    """Signs one input of a transaction.  Signature is returned.  You must use this signature to construct the spend script
    Parameters:
    tx: Transaction in object, hex or binary format
    inputIdx: index of input being signed
    inputAmount: how many Satoshi's does this input add to the transaction?
    prevoutScript: the script that this input is spending.
    key: sign using this private key in binary format
    sigHashType: flags describing what should be signed (SIGHASH_FORKID | SIGHASH_ALL (default), SIGHASH_NONE, SIGHASH_SINGLE, SIGHASH_ANYONECANPAY)
    """
    assert (sigHashType & SIGHASH_FORKID) > 0, "Did you forget to indicate the bitcoin cash hashing algorithm?"
    if type(tx) == str:
        tx = unhexlify(tx)
    elif type(tx) != bytes:
        tx = tx.serialize()
    if type(prevoutScript) == str:
        prevoutScript = unhexlify(prevoutScript)
    if type(inputAmount) is decimal.Decimal:
        inputAmount = int(inputAmount * BCH)

    result = create_string_buffer(100)
    siglen = cashlib.SignTx(tx, len(tx), inputIdx, c_longlong(inputAmount), prevoutScript,
                            len(prevoutScript), sigHashType, key, result, 100)
    if siglen == 0:
        raise Error("cashlib signtx error")
    return result.raw[0:siglen]

def signTxInputSchnorr(tx, inputIdx, inputAmount, prevoutScript, key, sigHashType=SIGHASH_FORKID | SIGHASH_ALL):
    """Signs one input of a transaction.  Schnorr signature is returned.  You must use this signature to construct the spend script
    Parameters:
    tx: Transaction in object, hex or binary format
    inputIdx: index of input being signed
    inputAmount: how many Satoshi's does this input add to the transaction?
    prevoutScript: the script that this input is spending.
    key: sign using this private key in binary format
    sigHashType: flags describing what should be signed (SIGHASH_FORKID | SIGHASH_ALL (default), SIGHASH_NONE, SIGHASH_SINGLE, SIGHASH_ANYONECANPAY)
    """
    assert (sigHashType & SIGHASH_FORKID) > 0, "Did you forget to indicate the bitcoin cash hashing algorithm?"
    if type(tx) == str:
        tx = unhexlify(tx)
    elif type(tx) != bytes:
        tx = tx.serialize()
    if type(prevoutScript) == str:
        prevoutScript = unhexlify(prevoutScript)
    if type(inputAmount) is decimal.Decimal:
        inputAmount = int(inputAmount * BCH)

    result = create_string_buffer(100)
    siglen = cashlib.SignTxSchnorr(tx, len(tx), inputIdx, c_longlong(inputAmount), prevoutScript,
                            len(prevoutScript), sigHashType, key, result, 100)
    if siglen == 0:
        raise Error("cashlib signtx error")
    return result.raw[0:siglen]

def signHashSchnorr(key, hsh):
    """Signs a 32 byte message (presumably the hash of something).  A Schnorr signature is returned.  You must use this signature to construct the spend script
    Parameters:
    hsh: 32 bytes of data, hex, binary, or object (contains serialize member) format
    key: sign using this private key in binary format
    sigHashType: flags describing what should be signed (SIGHASH_FORKID | SIGHASH_ALL (default), SIGHASH_NONE, SIGHASH_SINGLE, SIGHASH_ANYONECANPAY)
    """
    if type(hsh) == str:
        hsh = unhexlify(hsh)
    elif type(hsh) != bytes:
        hsh = hsh.serialize()

    result = create_string_buffer(100)
    assert len(hsh) == 32
    siglen = cashlib.SignHashSchnorr(hsh, key, result, 100)
    if siglen == 0:
        raise Error("cashlib signtx error")
    return result.raw[0:siglen]


def randombytes(length):
    """Get cryptographically acceptable pseudorandom bytes from the OS"""
    result = create_string_buffer(length)
    worked = cashlib.RandomBytes(result, length)
    if worked != length:
        raise Error("cashlib randombytes error")
    return result.raw


def pubkey(key):
    """Given a private key, return its public key"""
    result = create_string_buffer(65)
    l = cashlib.GetPubKey(key, result, 65)
    return result.raw[0:l]


def addrbin(pubkey):
    """Given a public key, in binary format, return its binary form address (just the bytes, no type or checksum)"""
    h = hashlib.new('ripemd160')
    h.update(hashlib.sha256(pubkey).digest())
    return h.digest()


def txid(txbin):
    """Return a transaction id, given a transaction in hex, object or binary form.
       The returned binary txid is not reversed.  Do: hexlify(cashlib.txid(txhex)[::-1]).decode("utf-8") to convert to
       bitcoind's hex format.
    """
    if type(txbin) == str:
        txbin = unhexlify(txbin)
    elif type(txbin) != bytes:
        txbin = txbin.serialize()
    return sha256(sha256(txbin))


def spendscript(*data):
    """Take binary data as parameters and return a spend script containing that data"""
    ret = []
    for d in data:
        if type(d) is str:
            d = unhexlify(d)
        assert type(d) is bytes, "There can only be data in spend scripts (no opcodes allowed)"
        l = len(d)
        if l == 0:  # push empty value onto the stack
            ret.append(bytes([0]))
        elif l <= 0x4b:
            ret.append(bytes([l]))  # 1-75 bytes push # of bytes as the opcode
            ret.append(d)
        elif l < 256:
            ret.append(bytes([76]))  # PUSHDATA1
            ret.append(bytes([l]))
            ret.append(d)
        elif l < 65536:
            ret.append(bytes([77]))  # PUSHDATA2
            ret.append(bytes([l & 255, l >> 8]))  # little endian
            ret.append(d)
        else:  # bigger values won't fit on the stack anyway
            assert 0, "cannot push %d bytes" % l
    return b"".join(ret)

class ScriptError(IntEnum):
    SCRIPT_ERR_OK = 0
    SCRIPT_ERR_UNKNOWN_ERROR = 1
    SCRIPT_ERR_EVAL_FALSE = 2
    SCRIPT_ERR_OP_RETURN = 3
    SCRIPT_ERR_SCRIPT_SIZE = 4
    SCRIPT_ERR_PUSH_SIZE = 5
    SCRIPT_ERR_OP_COUNT = 6
    SCRIPT_ERR_STACK_SIZE = 7
    SCRIPT_ERR_SIG_COUNT = 8
    SCRIPT_ERR_PUBKEY_COUNT = 9
    SCRIPT_ERR_INVALID_OPERAND_SIZE = 10
    SCRIPT_ERR_INVALID_NUMBER_RANGE = 11
    SCRIPT_ERR_IMPOSSIBLE_ENCODING = 12
    SCRIPT_ERR_INVALID_SPLIT_RANGE = 13
    SCRIPT_ERR_INVALID_BIT_COUNT = 14
    SCRIPT_ERR_VERIFY = 15
    SCRIPT_ERR_EQUALVERIFY = 16
    SCRIPT_ERR_CHECKMULTISIGVERIFY = 17
    SCRIPT_ERR_CHECKSIGVERIFY = 18
    SCRIPT_ERR_CHECKDATASIGVERIFY = 19
    SCRIPT_ERR_NUMEQUALVERIFY = 20
    SCRIPT_ERR_BAD_OPCODE = 21

    SCRIPT_ERR_DISABLED_OPCODE = 22
    SCRIPT_ERR_INVALID_STACK_OPERATION = 23
    SCRIPT_ERR_INVALID_ALTSTACK_OPERATION = 24
    SCRIPT_ERR_UNBALANCED_CONDITIONAL = 25
    SCRIPT_ERR_DIV_BY_ZERO = 26
    SCRIPT_ERR_MOD_BY_ZERO = 27

    SCRIPT_ERR_INVALID_BITFIELD_SIZE = 28
    SCRIPT_ERR_INVALID_BIT_RANGE = 29
    SCRIPT_ERR_NEGATIVE_LOCKTIME = 30
    SCRIPT_ERR_UNSATISFIED_LOCKTIME = 31
    SCRIPT_ERR_SIG_HASHTYPE = 32
    SCRIPT_ERR_SIG_DER = 33
    SCRIPT_ERR_MINIMALDATA = 34
    SCRIPT_ERR_SIG_PUSHONLY = 35
    SCRIPT_ERR_SIG_HIGH_S = 36
    SCRIPT_ERR_PUBKEYTYPE = 37
    SCRIPT_ERR_CLEANSTACK = 38
    SCRIPT_ERR_SIG_NULLFAIL = 39
    SCRIPT_ERR_SIG_BADLENGTH = 40
    SCRIPT_ERR_MUST_USE_FORKID = 41
    SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS = 42
    SCRIPT_ERR_NONCOMPRESSED_PUBKEY = 43
    SCRIPT_ERR_NUMBER_OVERFLOW = 44
    SCRIPT_ERR_NUMBER_BAD_ENCODING = 45
    SCRIPT_ERR_ERROR_COUNT = 46

class ScriptFlags(IntFlag):
    SCRIPT_VERIFY_P2SH = 1
    SCRIPT_VERIFY_STRICTENC = 1 << 1
    SCRIPT_VERIFY_DERSIG = 1 << 2
    SCRIPT_VERIFY_LOW_S = 1 << 3
    SCRIPT_VERIFY_SIGPUSHONLY = (1 << 5)
    SCRIPT_VERIFY_MINIMALDATA = (1 << 6)
    SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS = (1 << 7)
    SCRIPT_VERIFY_CLEANSTACK = (1 << 8)
    SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY = (1 << 9)
    SCRIPT_VERIFY_CHECKSEQUENCEVERIFY = (1 << 10)
    SCRIPT_VERIFY_NULLFAIL = (1 << 14)
    SCRIPT_ENABLE_SIGHASH_FORKID = (1 << 16)
    SCRIPT_ENABLE_REPLAY_PROTECTION = (1 << 17)
    SCRIPT_ENABLE_CHECKDATASIG = (1 << 18)
    SCRIPT_DISALLOW_SEGWIT_RECOVERY = (1 << 20)
    SCRIPT_ENABLE_MUL_SHIFT_INVERT = (1 << 21)
    SCRIPT_ENABLE_OP_REVERSEBYTES = (1 << 23)

    MANDATORY_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC | SCRIPT_ENABLE_SIGHASH_FORKID | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_NULLFAIL;
    STANDARD_SCRIPT_VERIFY_FLAGS = MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_MINIMALDATA | SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS | SCRIPT_VERIFY_CLEANSTACK | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY | SCRIPT_ENABLE_CHECKDATASIG;


class ScriptMachine:
    STACK = 0
    ALTSTACK = 1


    def __init__(self, flags=-1, nocreate=False, tx=None, inputIdx=None, inputAmount=None):
        self.flags = flags
        if self.flags == -1:
            self.flags = ScriptFlags.STANDARD_SCRIPT_VERIFY_FLAGS
        result = create_string_buffer(100)
        if nocreate:
            self.smId = None
        else:
            if tx is None:
                self.smId = cashlib.CreateNoContextScriptMachine(self.flags)
            else:
                # If string (assumes hex) or object convert to binary serialization
                if type(tx) == str:
                    txbin = unhexlify(txbin)
                elif type(tx) != bytes:
                    txbin = tx.serialize()
                else:
                    txbin = tx
                self.smId = cashlib.CreateScriptMachine(self.flags, inputIdx, c_longlong(inputAmount), txbin, len(txbin))
        self.curPos = 0
        self.script = None

    def __del__(self):
        if hasattr(self, 'smId'):
            if self.smId: self.cleanup()

    def clone(self):
        sm = ScriptMachine(self.flags, nocreate=True)
        sm.smId = cashlib.SmClone(self.smId)
        sm.curPos = self.curPos
        sm.script = self.script
        return sm

    def cleanup(self):
        """Call to explicitly free the resources used by this script machine"""
        if self.smId:
            cashlib.SmRelease(self.smId)
            self.smId = 0
        else:
            raise Error("accessed inactive script machine")

    def reset(self):
        if self.smId==0: raise Error("accessed inactive script machine")
        cashlib.SmReset(self.smId)
        self.curPos = 0

    def eval(self, script):
        if self.smId==0: raise Error("accessed inactive script machine")
        if type(script) == str:
            script = unhexlify(script)
        ret = cashlib.SmEval(self.smId, script, len(script))
        return ret

    def begin(self, script):
        """Start stepping through the provided script"""
        if self.smId==0: raise Error("accessed inactive script machine")
        if type(script) == str:
            script = unhexlify(script)
        ret = cashlib.SmBeginStep(self.smId, script, len(script))
        self.curPos = 0
        self.script = script
        return ret

    def step(self):
        """Step forward 1 instruction"""
        if self.smId==0: raise Error("accessed inactive script machine")
        if self.curPos >= len(self.script):
            raise Error("stepped beyond end of script")
        ret = cashlib.SmStep(self.smId)
        if ret == 0:
            raise Error("execution error")
        self.curPos = cashlib.SmPos(self.smId)
        return self.curPos

    def error(self):
        if self.smId==0: raise Error("accessed inactive script machine")
        return (ScriptError(cashlib.SmGetError(self.smId)), cashlib.SmPos(self.smId))

    def pos(self):
        return self.curPos

    def end(self):
        """Call when script is complete to do final script checks"""
        if self.smId==0: raise Error("accessed inactive script machine")
        ret = cashlib.SmEndStep(self.smId)
        return ret

    def altstack(self):
        return self.stack(self.ALTSTACK)

    def stack(self, which = None):
        """Returns the machine's stack (main stack by default) as a list of byte arrays, index 0 is the stack top"""
        if self.smId==0: raise Error("accessed inactive script machine")
        if which is None: which = self.STACK
        stk = []
        idx  = 0
        item = create_string_buffer(MAX_STACK_ITEM_LENGTH)
        while 1:
            result = cashlib.SmGetStackItem(self.smId, which, idx, item)
            if result == -1: break
            stk.append(item[0:result])
            idx+=1
        return stk

    def setAltStackItem(self, idx, value):
        """Set an item on the stack to a value, index 0 is the top.  index -1 means push"""
        self.setStackItem(idx, value, self.ALTSTACK)

    def setStackItem(self, idx, value, which = None):
        """Set an item on the stack to a value, index 0 is the top.  index -1 means push"""
        if self.smId==0: raise Error("accessed inactive script machine")
        if which is None: which = self.STACK
        cashlib.SmSetStackItem(self.smId, which, idx, value, len(value))


def Test():
    assert bin2hex(b"123") == "313233"
    assert len(randombytes(10)) == 10
    assert randombytes(16) != randombytes(16)
