// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/* clang-format off */
// must be first for windows
#include "compat.h"

/* clang-format on */
#include "base58.h"
#include "random.h"
#include "streams.h"
#include "util.h"
#include "utilstrencodings.h"

#include <openssl/rand.h>
#include <string>
#include <vector>

static bool sigInited = false;

ECCVerifyHandle *verifyContext = nullptr;

// stop the logging
int LogPrintStr(const std::string &str) { return str.size(); }
namespace Logging
{
uint64_t categoriesEnabled = 0; // 64 bit log id mask.
};

// helper functions
namespace
{
CKey LoadKey(unsigned char *src)
{
    CKey secret;
    secret.Set(src, src + 32, true);
    return secret;
}

#if 0
// This function is temporarily removed since it is not used.  However it will be needed for interfacing to
// languages that handle binary data poorly, since it allows transaction information to be communicated via hex strings

// From core_read.cpp #include "core_io.h"
bool DecodeHexTx(CTransaction &tx, const std::string &strHexTx)
{
    if (!IsHex(strHexTx))
        return false;

    std::vector<unsigned char> txData(ParseHex(strHexTx));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    try
    {
        ssData >> tx;
    }
    catch (const std::exception &)
    {
        return false;
    }

    return true;
}
#endif

static const char *hexxlat = "0123456789ABCDEF";
std::string GetHex(unsigned char *data, unsigned int len)
{
    std::string ret;
    ret.reserve(2 * len);
    for (unsigned int i = 0; i < len; i++)
    {
        unsigned char val = data[i];
        ret.push_back(hexxlat[val >> 4]);
        ret.push_back(hexxlat[val & 15]);
    }
    return ret;
}
}

/** Convert binary data to a hex string.  The provided result buffer must be 2*length+1 bytes.
 */
extern "C" int Bin2Hex(unsigned char *val, int length, char *result, unsigned int resultLen)
{
    std::string s = GetHex(val, length);
    if (s.size() >= resultLen)
        return 0; // need 1 more for /0
    strncpy(result, s.c_str(), resultLen);
    return 1;
}


/** Return random bytes from cryptographically acceptable random sources */
extern "C" int RandomBytes(unsigned char *buf, int num)
{
    if (RAND_bytes(buf, num) != 1)
    {
        memset(buf, 0, num);
        return 0;
    }
    return num;
}

/** Given a private key, return its corresponding public key */
extern "C" int GetPubKey(unsigned char *keyData, unsigned char *result, unsigned int resultLen)
{
    if (!sigInited)
    {
        sigInited = true;
        ECC_Start();
        verifyContext = new ECCVerifyHandle();
    }

    CKey key = LoadKey(keyData);
    CPubKey pubkey = key.GetPubKey();
    unsigned int size = pubkey.size();
    if (size > resultLen)
        return 0;
    std::copy(pubkey.begin(), pubkey.end(), result);
    return size;
}


/** Sign one input of a transaction
    All buffer arguments should be in binary-serialized data.
    The transaction (txData) must contain the COutPoint (tx hash and vout) of all relevant inputs,
    however, it is not necessary to provide the spend script.
*/
extern "C" int SignTx(unsigned char *txData,
    int txbuflen,
    unsigned int inputIdx,
    int64_t inputAmount,
    unsigned char *prevoutScript,
    uint32_t priorScriptLen,
    uint32_t nHashType,
    unsigned char *keyData,
    unsigned char *result,
    unsigned int resultLen)
{
    DbgAssert(nHashType & SIGHASH_FORKID, return 0);

    if (!sigInited)
    {
        sigInited = true;
        ECC_Start();
        verifyContext = new ECCVerifyHandle();
    }

    CTransaction tx;
    result[0] = 0;

    CDataStream ssData((char *)txData, (char *)txData + txbuflen, SER_NETWORK, PROTOCOL_VERSION);
    try
    {
        ssData >> tx;
    }
    catch (const std::exception &)
    {
        return 0;
    }

    if (inputIdx >= tx.vin.size())
        return 0;

    CScript priorScript(prevoutScript, prevoutScript + priorScriptLen);
    CKey key = LoadKey(keyData);

    size_t nHashedOut = 0;
    uint256 sighash = SignatureHash(priorScript, tx, inputIdx, nHashType, inputAmount, &nHashedOut);
    std::vector<unsigned char> sig;
    if (!key.Sign(sighash, sig))
    {
        return 0;
    }
    sig.push_back((unsigned char)nHashType);
    unsigned int sigSize = sig.size();
    if (sigSize > resultLen)
        return 0;
    std::copy(sig.begin(), sig.end(), result);
    return sigSize;
}


/*
Since the ScriptMachine is often going to be initialized, called and destructed within a single stack frame, it
does not make copies of the data it is using.  But higher-level language and debugging interaction use the
ScriptMachine across stack frames.  Therefore it is necessary to create a class to hold all of this data on behalf
of the ScriptMachine.
 */
class ScriptMachineData
{
public:
    ScriptMachineData() : sm(nullptr), tx(nullptr), checker(nullptr), script(nullptr) {}
    ScriptMachine *sm;
    std::shared_ptr<CTransaction> tx;
    std::shared_ptr<BaseSignatureChecker> checker;
    std::shared_ptr<CScript> script;

    ~ScriptMachineData()
    {
        if (sm)
        {
            delete sm;
            sm = nullptr;
        }
    }
};

// Create a ScriptMachine with no transaction context -- useful for tests and debugging
// This ScriptMachine can't CHECKSIG or CHECKSIGVERIFY
extern "C" void *CreateNoContextScriptMachine(unsigned int flags)
{
    ScriptMachineData *smd = new ScriptMachineData();
    smd->checker = std::make_shared<BaseSignatureChecker>();
    smd->sm = new ScriptMachine(flags, *smd->checker);
    return (void *)smd;
}

// Create a ScriptMachine operating in the context of a particular transaction and input.
// The transaction, input index, and input amount are used in CHECKSIG and CHECKSIGVERIFY to generate the hash that
// the signature validates.
extern "C" void *CreateScriptMachine(unsigned int flags,
    unsigned int inputIdx,
    int64_t inputAmount,
    unsigned char *txData,
    int txbuflen)
{
    if (!sigInited)
    {
        sigInited = true;
        ECC_Start();
        verifyContext = new ECCVerifyHandle();
    }

    ScriptMachineData *smd = new ScriptMachineData();
    smd->tx = std::make_shared<CTransaction>();

    CDataStream ssData((char *)txData, (char *)txData + txbuflen, SER_NETWORK, PROTOCOL_VERSION);
    try
    {
        ssData >> *smd->tx;
    }
    catch (const std::exception &)
    {
        delete smd;
        return 0;
    }

    // Its ok to get the bare tx pointer: the life of the CTransaction is the same as TransactionSignatureChecker
    smd->checker = std::make_shared<TransactionSignatureChecker>(smd->tx.get(), inputIdx, inputAmount, flags);
    smd->sm = new ScriptMachine(flags, *smd->checker);
    return (void *)smd;
}

// Release a ScriptMachine context
extern "C" void SmRelease(void *smId)
{
    ScriptMachineData *smd = (ScriptMachineData *)smId;
    if (!smd)
        return;
    delete smd;
}

// Copy the provided ScriptMachine, returning a new ScriptMachine id that exactly matches the current one
extern "C" void *SmClone(void *smId)
{
    ScriptMachineData *from = (ScriptMachineData *)smId;
    ScriptMachineData *to = new ScriptMachineData();
    to->script = from->script;
    to->checker = from->checker;
    to->tx = from->tx;
    to->sm = new ScriptMachine(*from->sm);
    return (void *)to;
}


// Evaluate a script within the context of this script machine
extern "C" bool SmEval(void *smId, unsigned char *scriptBuf, unsigned int scriptLen)
{
    ScriptMachineData *smd = (ScriptMachineData *)smId;

    CScript script(scriptBuf, scriptBuf + scriptLen);
    bool ret = smd->sm->Eval(script);
    return ret;
}

// Step-by-step interface: start evaluating a script within the context of this script machine
extern "C" bool SmBeginStep(void *smId, unsigned char *scriptBuf, unsigned int scriptLen)
{
    ScriptMachineData *smd = (ScriptMachineData *)smId;
    // shared_ptr will auto-release the old one
    smd->script = std::make_shared<CScript>(scriptBuf, scriptBuf + scriptLen);
    bool ret = smd->sm->BeginStep(*smd->script);
    return ret;
}

// Step-by-step interface: execute the next instruction in the script
extern "C" unsigned int SmStep(void *smId)
{
    ScriptMachineData *smd = (ScriptMachineData *)smId;
    unsigned int ret = smd->sm->Step();
    return ret;
}

// Step-by-step interface: get the current position in this script, specified in bytes offset from the script start
extern "C" int SmPos(void *smId)
{
    ScriptMachineData *smd = (ScriptMachineData *)smId;
    return smd->sm->getPos();
}


// Step-by-step interface: End script evaluation
extern "C" bool SmEndStep(void *smId)
{
    ScriptMachineData *smd = (ScriptMachineData *)smId;
    bool ret = smd->sm->EndStep();
    return ret;
}


// Revert the script machine to initial conditions
extern "C" void SmReset(void *smId)
{
    ScriptMachineData *smd = (ScriptMachineData *)smId;
    smd->sm->Reset();
}


// Get a stack item, 0 = stack, 1 = altstack,  pass a buffer at least 520 bytes in size
// returns length of the item or -1 if no item.  0 is the stack top
extern "C" void SmSetStackItem(void *smId,
    unsigned int stack,
    int index,
    const unsigned char *value,
    unsigned int valsize)
{
    ScriptMachineData *smd = (ScriptMachineData *)smId;

    const std::vector<StackDataType> &stk = (stack == 0) ? smd->sm->getStack() : smd->sm->getAltStack();
    if (((int)stk.size()) <= index)
        return;
    if (stack == 0)
    {
        smd->sm->setStackItem(index, StackDataType(value, value + valsize));
    }
    else if (stack == 1)
    {
        smd->sm->setAltStackItem(index, StackDataType(value, value + valsize));
    }
}

// Get a stack item, 0 = stack, 1 = altstack,  pass a buffer at least 520 bytes in size
// returns length of the item or -1 if no item.  0 is the stack top
extern "C" int SmGetStackItem(void *smId, unsigned int stack, unsigned int index, unsigned char *result)
{
    ScriptMachineData *smd = (ScriptMachineData *)smId;

    const std::vector<StackDataType> &stk = (stack == 0) ? smd->sm->getStack() : smd->sm->getAltStack();
    if (stk.size() <= index)
        return -1;
    index = stk.size() - index - 1; // reverse it so 0 is stack top
    int sz = stk[index].size();
    memcpy(result, stk[index].data(), sz);
    return sz;
}

// Returns the last error generated during script evaluation (if any)
extern "C" unsigned int SmGetError(void *smId)
{
    ScriptMachineData *smd = (ScriptMachineData *)smId;
    return (unsigned int)smd->sm->getError();
}
