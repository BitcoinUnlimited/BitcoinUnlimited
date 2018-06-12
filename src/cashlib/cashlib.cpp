#include <openssl/rand.h>
#include <string>
#include <vector>

#include "compat.h"
#include "base58.h"
#include "random.h"
#include "streams.h"
#include "utilstrencodings.h"

static bool sigInited = false;

// stop the logging
int LogPrintStr(const std::string &str) { return str.size(); }
namespace Logging
{
    uint64_t categoriesEnabled = 0; // 64 bit log id mask.
};

// helper functions
namespace
{
#if 0
CKey LoadKey(uint256 val)
{
    CKey secret;
    unsigned char *src = val.begin();
    secret.Set(src, src+32, true);
    return secret;
}
#endif

CKey LoadKey(unsigned char *src)
{
    CKey secret;
    secret.Set(src, src + 32, true);
    return secret;
}

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

#if 0
bool DecodeHexTx(CTransaction &tx, const char* cHexTx)
{
    std::string strHexTx(cHexTx);
    return DecodeHexTx(tx, strHexTx);
}

std::string GetHexReversed(const std::vector<unsigned char>& data)
{
    unsigned int len = data.size();
    char psz[len * 2 + 1];
    for (unsigned int i = 0; i < len; i++)
        sprintf(psz + i * 2, "%02x", data[len - i - 1]);
    return std::string(psz, psz + len * 2);
}

std::string GetHex(const std::vector<unsigned char>& data)
{
    unsigned int len = data.size();
    char psz[len * 2 + 1];
    for (unsigned int i = 0; i < len; i++)
        sprintf(psz + i * 2, "%02x", data[i]);
    return std::string(psz, psz + len * 2);
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

#if 0
extern "C" int Base58Key2Bin(char* b58key, unsigned char* result)
{
    CBitcoinSecret s;
    s.SetString(b58key);
    CKey key = s.GetKey();

    unsigned char *src = (unsigned char *)key.begin();
    memcpy(result, src, 32);
    return 32;
}
#endif

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
    if (!sigInited)
    {
        sigInited = true;
        ECC_Start();
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
    uint256 sighash = SignatureHashBitcoinCash(priorScript, tx, inputIdx, nHashType, inputAmount, &nHashedOut);
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
