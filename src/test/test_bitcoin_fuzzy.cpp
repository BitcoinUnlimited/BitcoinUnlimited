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
#include "graphene.h"
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

#include <cstdio>
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
    std::string name;
    FuzzTest(const std::string &_name) : name(_name)
    {
        assert(registry.count(name) == 0);
        registry[name] = this;
        registry_seq.push_back(this);
    }
    ~FuzzTest() {}
    //! initialize with input data before testing
    virtual bool init(const std::vector<char> &_buffer)
    {
        buffer = _buffer;
        output.clear();
        return true;
    }
    //! run the fuzz test once - calls internal virtual method run()
    virtual int operator()(const bool produce_output)
    {
        run(produce_output);
        return 0;
    }

    std::vector<char> output;

protected:
    std::vector<char> buffer;

    //! override this with the actual test
    virtual void run(bool produce_output) = 0;
};

/*! fuzz test that uses network message decoding
  and cleanly shuts down for std::ios_base::failures. */
class FuzzTestNet : public FuzzTest
{
public:
    FuzzTestNet(const std::string &name) : FuzzTest(name), ds(nullptr) {}
    bool init(const std::vector<char> &buffer)
    {
        FuzzTest::init(buffer);
        if (ds != nullptr)
            delete ds;
        ds = new CDataStream(buffer, SER_NETWORK, INIT_PROTO_VERSION);
        try
        {
            int nVersion;
            *ds >> nVersion;
            ds->SetVersion(nVersion);
        }
        catch (const std::ios_base::failure &e)
        {
            // ignore test case
            return false;
        }
        return true;
    }
    ~FuzzTestNet()
    {
        delete ds;
        ds = nullptr;
    }
    virtual int operator()(const bool produce_output)
    {
        try
        {
            run(produce_output);
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
    void run(const bool produce_output)
    {
        T value;
        *ds >> value;
        // FIXME: trying reserialization here, as well
        if (produce_output)
        {
            CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);
            out << value;
            output.insert(output.begin(), out.begin(), out.end());
        }
    }
};

class FuzzBlockMerkleRoot : FuzzTestNet
{
public:
    FuzzBlockMerkleRoot() : FuzzTestNet("cblockmerkleroot_deser") {}
protected:
    void run(const bool produce_output)
    {
        CBlock block;
        *ds >> block;
        bool mutated;
        const uint256 result = BlockMerkleRoot(block, &mutated);
        if (produce_output)
        {
            CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);
            out << result;
            output.insert(output.begin(), out.begin(), out.end());
        }
    }
};


class FuzzCMessageHeader : FuzzTestNet
{
public:
    FuzzCMessageHeader() : FuzzTestNet("cmessageheader_deser") {}
protected:
    void run(const bool produce_output)
    {
        CMessageHeader::MessageStartChars pchMessageStart = {0x00, 0x00, 0x00, 0x00};
        CMessageHeader mh(pchMessageStart);
        *ds >> mh;
        mh.IsValid(pchMessageStart);

        if (produce_output)
        {
            CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);
            out << mh;
            output.insert(output.begin(), out.begin(), out.end());
        }
    }
};

class FuzzCTxOutCompressor : FuzzTestNet
{
public:
    FuzzCTxOutCompressor() : FuzzTestNet("ctxoutcompressor_deser") {}
protected:
    void run(const bool produce_output)
    {
        CTxOut to;
        CTxOutCompressor toc(to);
        *ds >> toc;

        if (produce_output)
        {
            CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);
            out << to;
            out << toc;
            output.insert(output.begin(), out.begin(), out.end());
        }
    }
};

class FuzzWildmatch : FuzzTest
{
public:
    FuzzWildmatch() : FuzzTest("wildmatch") {}
protected:
    void run(const bool produce_output)
    {
        std::vector<char>::iterator splitpoint = std::find(buffer.begin(), buffer.end(), '\0');
        std::string pattern(buffer.begin(), splitpoint);
        std::string test;
        if (splitpoint + 1 <= buffer.end())
            test = std::string(splitpoint + 1, buffer.end());
        bool result = wildmatch(pattern, test);
        if (produce_output)
        {
            CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);
            out << result;
            output.insert(output.begin(), out.begin(), out.end());
        }
    }
};

class FuzzCashAddrEncDec : FuzzTest
{
public:
    FuzzCashAddrEncDec() : FuzzTest("cashaddr_encdec") {}
protected:
    void run(const bool produce_output)
    {
        std::vector<char>::iterator splitpoint = std::find(buffer.begin(), buffer.end(), '\0');
        std::string prefix(buffer.begin(), splitpoint);
        std::vector<uint8_t> values;
        if (splitpoint + 1 <= buffer.end())
            values = std::vector<uint8_t>(splitpoint + 1, buffer.end());
        std::string encoded = cashaddr::Encode(prefix, values);

        std::pair<std::string, std::vector<uint8_t> > dec = cashaddr::Decode(encoded, prefix);

        if (produce_output)
        {
            CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);
            out << dec.first;
            out << dec.second;
            output.insert(output.begin(), out.begin(), out.end());
        }
    }
};

class FuzzCashAddrDecode : FuzzTest
{
public:
    FuzzCashAddrDecode() : FuzzTest("cashaddr_decode") {}
protected:
    void run(const bool produce_output)
    {
        std::vector<char>::iterator splitpoint = std::find(buffer.begin(), buffer.end(), '\0');
        std::string prefix(buffer.begin(), splitpoint);
        std::string test;
        if (splitpoint + 1 <= buffer.end())
            test = std::string(splitpoint + 1, buffer.end());
        std::pair<std::string, std::vector<uint8_t> > dec = cashaddr::Decode(test, prefix);

        if (produce_output)
        {
            CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);
            out << dec.first;
            out << dec.second;
            output.insert(output.begin(), out.begin(), out.end());
        }
    }
};

class FuzzParseMoney : FuzzTest
{
public:
    FuzzParseMoney() : FuzzTest("parsemoney") {}
protected:
    void run(const bool produce_output)
    {
        std::string test(buffer.begin(), buffer.end());
        CAmount nRet;
        bool success = ParseMoney(test, nRet);

        if (produce_output)
        {
            CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);
            if (success)
                out << nRet;
            else
                out << std::string("failure");
            output.insert(output.begin(), out.begin(), out.end());
        }
    }
};


class FuzzParseFixedPoint : FuzzTest
{
public:
    FuzzParseFixedPoint() : FuzzTest("parsefixedpoint") {}
protected:
    void run(const bool produce_output)
    {
        if (buffer.begin() == buffer.end())
            return;
        int decimals = buffer[0];
        std::string test(buffer.begin() + 1, buffer.end());
        int64_t amount_out;
        bool success = ParseFixedPoint(test, decimals, &amount_out);

        if (produce_output)
        {
            CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);
            if (success)
                out << amount_out;
            else
                out << std::string("failure");
            output.insert(output.begin(), out.begin(), out.end());
        }
    }
};


class FuzzVerifyScript : FuzzTestNet
{
public:
    FuzzVerifyScript() : FuzzTestNet("verifyscript") {}
protected:
    void run(const bool produce_output)
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
        const bool result =
            VerifyScript(script_sig, script_pubkey, flags, BaseSignatureChecker(), &error, &sighashtype);

        if (produce_output)
        {
            CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);
            out << result;
            out << sighashtype;
            out << scriptsig_raw;
            out << scriptpubkey_raw;
            output.insert(output.begin(), out.begin(), out.end());
        }
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

class FuzzTester : public FuzzTest
{
public:
    FuzzTester() : FuzzTest("tester") {}
protected:
    void run(const bool produce_output)
    {
        // Just a very simple test to make sure that the AFL
        // drill down heuristics works for the given Build
        std::string test(buffer.begin(), buffer.end());

        if (test[0] == 'a' && test[1] == 'b' && test[2] == 'c')
            abort();

        if (test[0] == 'd' && test[1] == 'e' && test[2] == 'f')
            while (1)
                ;

        if (produce_output)
            throw std::exception();
    }
};

class FuzzAPICIblt : FuzzTestNet
{
public:
    FuzzAPICIblt() : FuzzTestNet("api_iblt") {}
protected:
    void run(const bool produce_output)
    {
        CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);

        uint8_t cmd;
        *ds >> cmd;
        std::shared_ptr<CIblt> iblt;

        uint16_t num_entries = 0;
        uint8_t value_size = 0;
        uint64_t k = 0;
        std::vector<uint8_t> v;

        std::set<std::pair<uint64_t, std::vector<uint8_t> > > negative, positive;

        switch (cmd)
        {
        case 0:
            iblt = std::make_shared<CIblt>();
            break;
        default:
            *ds >> num_entries;
            *ds >> value_size;
            iblt = std::make_shared<CIblt>(num_entries, value_size);
            break;
        }

        while (!ds->empty())
        {
            *ds >> cmd;
            switch (cmd)
            {
            case 0:
                iblt->reset();
                break;
            case 1:
                out << iblt->size();
                break;
            case 2:
                if (!iblt->isModified())
                {
                    *ds >> num_entries;
                    *ds >> value_size;
                    iblt->resize(num_entries, value_size);
                }
                break;
            case 3:
                *ds >> k;
                *ds >> v;
                iblt->insert(k, v);
                break;
            case 4:
                *ds >> k;
                *ds >> v;
                iblt->erase(k, v);
                break;
            case 5:
                *ds >> k;
                out << iblt->get(k, v);
                out << v;
                break;
            case 6:
                out << iblt->getValueSize();
                break;
            case 7:
                out << iblt->getNHash();
                break;
            case 8:
                out << iblt->listEntries(positive, negative);
                for (auto entry : positive)
                {
                    out << entry.first;
                    out << entry.second;
                }
                for (auto entry : negative)
                {
                    out << entry.first;
                    out << entry.second;
                }
                break;
            case 9:
                // fixme subtract iblts
                break;
            case 10:
                out << iblt->DumpTable();
                break;
            case 11:
                *ds >> *iblt;
            default:
                break;
            }
        }
        if (produce_output)
        {
            output.insert(output.begin(), out.begin(), out.end());
        }
    }
};

class FuzzAPICGrapheneSet : FuzzTestNet
{
public:
    FuzzAPICGrapheneSet() : FuzzTestNet("api_graphene_set") {}
protected:
    void run(const bool produce_output)
    {
        CDataStream out(output, SER_NETWORK, INIT_PROTO_VERSION);

        uint8_t cmd;
        std::shared_ptr<CGrapheneSet> gs;

        uint16_t nReceiverUniverseItems; // note: artificially constrained
        std::vector<uint256> itemHashes;
        bool ordered;
        bool fDeterministic;
        *ds >> nReceiverUniverseItems;
        *ds >> itemHashes;
        *ds >> ordered;
        *ds >> fDeterministic;

        if (nReceiverUniverseItems < itemHashes.size() - 1)
            return;

        try
        {
            gs = std::make_shared<CGrapheneSet>(nReceiverUniverseItems, itemHashes, ordered, fDeterministic);

            while (!ds->empty())
            {
                *ds >> cmd;
                switch (cmd)
                {
                case 0:
                {
                    uint64_t nBlockTx = 0;
                    uint16_t nReceiverPoolTx = 0; // note: artificially constrained
                    *ds >> nBlockTx;
                    *ds >> nReceiverPoolTx;
                    if ((nReceiverPoolTx >= nBlockTx - 1))
                        out << gs->OptimalSymDiff(nBlockTx, nReceiverPoolTx);
                }
                break;
                case 1:
                {
                    std::vector<uint256> receiverItemHashes;
                    *ds >> receiverItemHashes;
                    out << gs->Reconcile(receiverItemHashes);
                }
                break;
                default:
                    break;
                }
            }
        }
        catch (std::exception &e)
        {
            // ignore
        }
        if (produce_output)
        {
            output.insert(output.begin(), out.begin(), out.end());
        }
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

    FuzzDeserNet<CMemPoolInfo> fuzz_cmempoolinfo("cmempoolinfo");
    FuzzDeserNet<CGrapheneBlock> fuzz_grapheneblock("cgrapheneblock");
    FuzzDeserNet<CGrapheneBlockTx> fuzz_grapheneblocktx("cgrapheneblocktx");
    FuzzDeserNet<CRequestGrapheneBlockTx> fuzz_requestgrapheneblocktx("crequestgrapheneblocktx");
    FuzzDeserNet<CGrapheneSet> fuzz_grapheneset("cgrapheneset");

    FuzzAPICIblt fuzz_api_iblt;
    FuzzAPICGrapheneSet fuzz_api_graphene_set;

    // command line arguments can be used to constrain more and
    // more specifically to a particular test
    // (only the test name at the moment)
    int argn = 1;
    FuzzTest *ft = nullptr;

    bool specific = false;

    bool produce_output = false;

    if (argn < argc)
    {
        std::string testname = argv[argn];

        // produce output also if first character of test name is a plus sign
        if (testname[0] == '+')
        {
            testname = testname.substr(1);
            produce_output = true;
        }
        if (testname.size() > 1)
        { // single +? -> then just non-specific test with output
            if (testname == "list_tests")
            {
                int idx = 0;
                for (const auto &ft2 : registry_seq)
                {
                    printf("%4d %s\n", idx, ft2->name.c_str());
                    idx++;
                }
                return 0;
            }
            if (registry.count(testname) == 0)
            {
                printf("Test %s not known.\n", testname.c_str());
                return 1;
            }
            ft = registry[testname];
            specific = true;
        }
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

        if (!specific)
        {
            if (buffer.size() < sizeof(uint32_t))
            {
                continue;
            }

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
            if (registry_seq[test_id] == &fuzz_tester)
            {
                printf("Test that breaks on purpose is disabled for fuzz-all mode.\n");
                continue;
            }
            ft = registry_seq[test_id];
        }
        if (ft->init(buffer))
            (*ft)(produce_output);

        if (produce_output)
        {
            fwrite(ft->output.data(), 1, ft->output.size(), stdout);
            fflush(stdout);
        }
    }
    return 0;
}
