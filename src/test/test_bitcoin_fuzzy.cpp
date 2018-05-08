// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "addrman.h"
#include "cashaddr.h"
#include "chain.h"
#include "coins.h"
#include "compressor.h"
#include "consensus/merkle.h"
#include "net.h"
#include "primitives/block.h"
#include "protocol.h"
#include "pubkey.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "streams.h"
#include "undo.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "version.h"

#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <map>
//#include <iostream>
#include <vector>

class FuzzTest;

static std::map<std::string, FuzzTest *> registry;
static std::vector<FuzzTest *> registry_seq;

class FuzzTest
{
public:
    FuzzTest(const std::string &name)
    {
        assert(registry.count(name) == 0);
        registry[name] = this;
        registry_seq.push_back(this);
    }
    ~FuzzTest() {}
    //! initialize with input data before testing
    virtual void init(const std::vector<char> &_buffer) { buffer = _buffer; }
    //! run the fuzz test once - calls internal virtual method run()
    virtual int operator()()
    {
        run();
        return 0;
    }

protected:
    std::vector<char> buffer;
    //! override this with the actual test
    virtual void run() = 0;
};

/*! fuzz test that uses network message decoding
  and cleanly shuts down for std::ios_base::failures. */
class FuzzTestNet : public FuzzTest
{
public:
    FuzzTestNet(const std::string &name) : FuzzTest(name), ds(nullptr) {}
    void init(const std::vector<char> &buffer)
    {
        FuzzTest::init(buffer);
        if (ds != nullptr)
            delete ds;
        ds = new CDataStream(buffer, SER_NETWORK, INIT_PROTO_VERSION);
        int nVersion;
        *ds >> nVersion;
        ds->SetVersion(nVersion);
    }
    ~FuzzTestNet()
    {
        delete ds;
        ds = nullptr;
    }
    virtual int operator()()
    {
        try
        {
            run();
            return 0;
        }
        catch (const std::ios_base::failure &e)
        {
            return 0;
        }
    }

protected:
    CDataStream *ds;
};

template <class T>
class FuzzDeserNet : public FuzzTestNet
{
public:
    FuzzDeserNet(std::string classname) : FuzzTestNet(classname + "_deser") {}
protected:
    void run()
    {
        T value;
        *ds >> value;
        // FIXME: trying reserialization here, as well
    }
};

class FuzzBlockMerkleRoot : FuzzTestNet
{
public:
    FuzzBlockMerkleRoot() : FuzzTestNet("cblockmerkleroot_deser") {}
protected:
    void run()
    {
        CBlock block;
        *ds >> block;
        bool mutated;
        BlockMerkleRoot(block, &mutated);
    }
};


class FuzzCMessageHeader : FuzzTestNet
{
public:
    FuzzCMessageHeader() : FuzzTestNet("cmessageheader_deser") {}
protected:
    void run()
    {
        CMessageHeader::MessageStartChars pchMessageStart = {0x00, 0x00, 0x00, 0x00};
        CMessageHeader mh(pchMessageStart);
        *ds >> mh;
        mh.IsValid(pchMessageStart);
    }
};

class FuzzCTxOutCompressor : FuzzTestNet
{
public:
    FuzzCTxOutCompressor() : FuzzTestNet("ctxoutcompressor_deser") {}
protected:
    void run()
    {
        CTxOut to;
        CTxOutCompressor toc(to);
        *ds >> toc;
    }
};

class FuzzWildmatch : FuzzTest
{
public:
    FuzzWildmatch() : FuzzTest("wildmatch") {}
protected:
    void run()
    {
        std::vector<char>::iterator splitpoint = std::find(buffer.begin(), buffer.end(), '\0');
        std::string pattern(buffer.begin(), splitpoint);
        std::string test;
        if (splitpoint + 1 <= buffer.end())
            test = std::string(splitpoint + 1, buffer.end());
        wildmatch(pattern, test);
    }
};

class FuzzCashAddrEncDec : FuzzTest
{
public:
    FuzzCashAddrEncDec() : FuzzTest("cashaddr_encdec") {}
protected:
    void run()
    {
        std::vector<char>::iterator splitpoint = std::find(buffer.begin(), buffer.end(), '\0');
        std::string prefix(buffer.begin(), splitpoint);
        std::vector<uint8_t> values;
        if (splitpoint + 1 <= buffer.end())
            values = std::vector<uint8_t>(splitpoint + 1, buffer.end());
        std::string encoded = cashaddr::Encode(prefix, values);
        // FIXME: this is a quite special test and needs to be made more geneal
        std::pair<std::string, std::vector<uint8_t> > dec = cashaddr::Decode(encoded, prefix);
        // assert(dec.first == prefix);
        // assert(dec.second == values);
    }
};

class FuzzCashAddrDecode : FuzzTest
{
public:
    FuzzCashAddrDecode() : FuzzTest("cashaddr_decode") {}
protected:
    void run()
    {
        std::vector<char>::iterator splitpoint = std::find(buffer.begin(), buffer.end(), '\0');
        std::string prefix(buffer.begin(), splitpoint);
        std::string test;
        if (splitpoint + 1 <= buffer.end())
            test = std::string(splitpoint + 1, buffer.end());
        std::pair<std::string, std::vector<uint8_t> > dec = cashaddr::Decode(test, prefix);
    }
};

class FuzzParseMoney : FuzzTest
{
public:
    FuzzParseMoney() : FuzzTest("parsemoney") {}
protected:
    void run()
    {
        std::string test(buffer.begin(), buffer.end());
        CAmount nRet;
        ParseMoney(test, nRet);
    }
};


class FuzzParseFixedPoint : FuzzTest
{
public:
    FuzzParseFixedPoint() : FuzzTest("parsefixedpoint") {}
protected:
    void run()
    {
        int decimals = buffer[0];
        std::string test(buffer.begin() + 1, buffer.end());
        int64_t amount_out;
        ParseFixedPoint(test, decimals, &amount_out);
    }
};


class FuzzVerifyScript : FuzzTestNet
{
public:
    FuzzVerifyScript() : FuzzTestNet("verifyscript") {}
protected:
    void run()
    {
        std::vector<std::vector<unsigned char> > stack;
        std::vector<unsigned char> scriptsig_raw, scriptpubkey_raw;
        unsigned int flags;

        *ds >> flags;
        *ds >> stack;
        *ds >> scriptsig_raw;
        *ds >> scriptpubkey_raw;

        if ((flags & SCRIPT_VERIFY_CLEANSTACK) != 0)
            flags |= SCRIPT_VERIFY_P2SH;

        ScriptError error;
        unsigned char sighashtype;
        CScript script_sig(scriptsig_raw), script_pubkey(scriptpubkey_raw);
        VerifyScript(script_sig, script_pubkey, flags, BaseSignatureChecker(), &error, &sighashtype);
    }
};


bool read_stdin(std::vector<char> &data)
{
    char buffer[1024];
    ssize_t length = 0;
    while ((length = read(STDIN_FILENO, buffer, 1024)) > 0)
    {
        data.insert(data.end(), buffer, buffer + length);

        if (data.size() > (1 << 20))
            return false;
    }
    return length == 0;
}

class FuzzTester : FuzzTest
{
public:
    FuzzTester() : FuzzTest("tester") {}
protected:
    void run()
    {
        // Just a very simple test to make sure that the AFL
        // drill down heuristics works for the given Build
        std::string test(buffer.begin(), buffer.end());

        if (test[0] == 'a' && test[1] == 'b' && test[2] == 'c')
            abort();

        if (test[0] == 'd' && test[1] == 'e' && test[2] == 'f')
            while (1)
                ;
    }
};

int main(int argc, char **argv)
{
    ECCVerifyHandle globalVerifyHandle;

    FuzzDeserNet<CBlock> fuzz_cblock("cblock");
    FuzzDeserNet<CTransaction> fuzz_ctransaction("ctransaction");
    FuzzDeserNet<CBlockLocator> fuzz_cblocklocator("cblocklocator");
    FuzzBlockMerkleRoot fuzz_blockmerkleroot;
    FuzzDeserNet<CAddrMan> fuzz_caddrman("caddrman");
    FuzzDeserNet<CBlockHeader> fuzz_cblockheader("cblockheader");
    FuzzDeserNet<CBanEntry> fuzz_cbanentry("cbanentry");
    FuzzDeserNet<CTxUndo> fuzz_ctxundo("ctxundo");
    FuzzDeserNet<CBlockUndo> fuzz_cblockundo("cblockundo");
    FuzzDeserNet<Coin> fuzz_coin("coin");
    FuzzDeserNet<CNetAddr> fuzz_cnetaddr("cnetaddr");
    FuzzDeserNet<CService> fuzz_service("cservice");
    FuzzCMessageHeader fuzz_cmessageheader;
    FuzzDeserNet<CAddress> fuzz_caddress("caddress");
    FuzzDeserNet<CInv> fuzz_cinv("cinv");
    FuzzDeserNet<CBloomFilter> fuzz_cbloomfilter("cbloomfilter");
    FuzzDeserNet<CDiskBlockIndex> fuzz_diskblockindex("cdiskblockindex");
    FuzzCTxOutCompressor fuzz_ctxoutcompressor;
    FuzzWildmatch fuzz_wildmatch;
    FuzzCashAddrEncDec fuzz_cashaddrencdec;
    FuzzCashAddrDecode fuzz_cashaddrdecode;
    FuzzParseMoney fuzz_parsemoney;
    FuzzParseFixedPoint fuzz_parsefixedpoint;
    FuzzVerifyScript fuzz_verifyscript;

    FuzzTester fuzz_tester;

    // command line arguments can be used to constrain more and
    // more specifically to a particular test
    // (only the test name at the moment)
    int argn = 1;
    FuzzTest *ft = nullptr;

    bool specific = false;

    if (argn < argc)
    {
        std::string testname = argv[argn];
        specific = true;
        if (testname == "list_tests")
        {
            for (const auto &entry : registry)
            {
                printf("%s\n", entry.first.c_str());
            }
            return 0;
        }
        if (registry.count(testname) == 0)
        {
            printf("Test %s not known.\n", testname.c_str());
            return 1;
        }
        ft = registry[testname];
        argn++;
    }

// use persistent mode if available (when compiled with afl-clang-fast(++))
#ifdef __AFL_LOOP
    while (__AFL_LOOP(1000))
    {
#else
#ifdef __AFL_INIT
    __AFL_INIT();
#endif
    bool once = true;
    while (once)
    {
        once = false;
#endif
        std::vector<char> buffer;

        if (!read_stdin(buffer))
        {
            continue;
        }

        if (buffer.size() < sizeof(uint32_t))
        {
            continue;
        }

        if (!specific)
        {
            // no test id given, get it from the stream
            uint32_t test_id = 0xffffffff;
            memcpy(&test_id, &buffer[0], sizeof(uint32_t));
            buffer.erase(buffer.begin(), buffer.begin() + sizeof(uint32_t));

            if (test_id >= registry_seq.size())
            {
                // test not available
                printf("Test no. %d not available.\n", test_id);
                continue;
            }
            ft = registry_seq[test_id];
        }
        ft->init(buffer);
        (*ft)();
    }
    return 0;
}
