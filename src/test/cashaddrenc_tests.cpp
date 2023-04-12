// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2017-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "cashaddr.h"
#include "cashaddrenc.h"
#include "chainparams.h"
#include "random.h"
#include "script/standard.h"
#include "test/data/cashaddr_token_types.json.h"
#include "test/jsonutil.h"
#include "test/test_bitcoin.h"
#include "tinyformat.h"
#include "uint256.h"

#include <boost/test/unit_test.hpp>

#include <array>
#include <stdexcept>

namespace
{
std::vector<std::string> GetNetworks()
{
    return {CBaseChainParams::MAIN, CBaseChainParams::TESTNET, CBaseChainParams::REGTEST};
}

uint160 insecure_GetRandUInt160(FastRandomContext &rand)
{
    uint160 n;
    for (uint8_t *c = n.begin(); c != n.end(); ++c)
    {
        *c = static_cast<uint8_t>(rand.rand32());
    }
    return n;
}

std::vector<uint8_t> insecure_GetRandomByteArray(FastRandomContext &rand, size_t n)
{
    std::vector<uint8_t> out;
    out.reserve(n);

    for (size_t i = 0; i < n; i++)
    {
        out.push_back(uint8_t(rand.randbits(8)));
    }
    return out;
}

class DstTypeChecker : public boost::static_visitor<void>
{
public:
    void operator()(const CKeyID &id) { isKey = true; }
    void operator()(const ScriptID &id) { isScript = true; }
    void operator()(const CNoDestination &) {}
    static bool IsScriptDst(const CTxDestination &d)
    {
        DstTypeChecker checker;
        boost::apply_visitor(checker, d);
        return checker.isScript;
    }

    static bool IsKeyDst(const CTxDestination &d)
    {
        DstTypeChecker checker;
        boost::apply_visitor(checker, d);
        return checker.isKey;
    }

private:
    DstTypeChecker() : isKey(false), isScript(false) {}
    bool isKey;
    bool isScript;
};

// Map all possible size bits in the version to the expected size of the
// hash in bytes.
const std::array<std::pair<uint8_t, uint32_t>, 8> valid_sizes = {
    {{0, 20}, {1, 24}, {2, 28}, {3, 32}, {4, 40}, {5, 48}, {6, 56}, {7, 64}}};

} // namespace

BOOST_FIXTURE_TEST_SUITE(cashaddrenc_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(encode_decode_all_sizes)
{
    FastRandomContext rand(true);
    const CChainParams &params = Params(CBaseChainParams::MAIN);

    for (auto ps : valid_sizes)
    {
        std::vector<uint8_t> data = insecure_GetRandomByteArray(rand, ps.second);
        CashAddrContent content = {PUBKEY_TYPE, data};
        std::vector<uint8_t> packed_data = PackCashAddrContent(content);

        // Check that the packed size is correct
        BOOST_CHECK_EQUAL(packed_data[1] >> 2, ps.first);
        std::string address = cashaddr::Encode(params.CashAddrPrefix(), packed_data);

        // Check that the address decodes properly
        CashAddrContent decoded = DecodeCashAddrContent(address, params);
        BOOST_CHECK_EQUAL_COLLECTIONS(
            std::begin(content.hash), std::end(content.hash), std::begin(decoded.hash), std::end(decoded.hash));
    }
}

BOOST_AUTO_TEST_CASE(check_packaddr_throws)
{
    FastRandomContext rand(true);

    for (auto ps : valid_sizes)
    {
        std::vector<uint8_t> data = insecure_GetRandomByteArray(rand, ps.second - 1);
        CashAddrContent content = {PUBKEY_TYPE, data};
        BOOST_CHECK_THROW(PackCashAddrContent(content), std::runtime_error);
    }
}

BOOST_AUTO_TEST_CASE(encode_decode)
{
    std::vector<CTxDestination> toTest = {CNoDestination{}, CKeyID(uint160S("badf00d")), ScriptID(uint160S("f00dbad"))};

    for (auto dst : toTest)
    {
        for (auto net : GetNetworks())
        {
            const auto netParams = Params(net);
            for (int tokenAware = 0; tokenAware < 2; ++tokenAware)
            {
                std::string encoded = EncodeCashAddr(dst, netParams, tokenAware);
                bool decodedTokenAware{};
                CTxDestination decoded = DecodeCashAddr(encoded, netParams, &decodedTokenAware);
                BOOST_CHECK(dst == decoded);
                if (IsValidDestination(decoded))
                {
                    BOOST_CHECK_EQUAL(bool(tokenAware), decodedTokenAware);
                }
            }
        }
    }
}

// Check that an encoded cash address is not valid on another network.
BOOST_AUTO_TEST_CASE(invalid_on_wrong_network)
{
    const CTxDestination dst = CKeyID(uint160S("c0ffee"));
    const CTxDestination invalidDst = CNoDestination{};

    for (auto net : GetNetworks())
    {
        for (auto otherNet : GetNetworks())
        {
            if (net == otherNet)
                continue;

            for (int tokenAware = 0; tokenAware < 2; ++tokenAware)
            {
                const auto netParams = Params(net);
                std::string encoded = EncodeCashAddr(dst, netParams, tokenAware);

                const auto otherNetParams = Params(otherNet);
                CTxDestination decoded = DecodeCashAddr(encoded, otherNetParams);
                BOOST_CHECK(decoded != dst);
                BOOST_CHECK(decoded == invalidDst);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(random_dst)
{
    FastRandomContext rand(true);

    const size_t NUM_TESTS = 5000;
    const CChainParams &params = Params(CBaseChainParams::MAIN);

    for (size_t i = 0; i < NUM_TESTS; ++i)
    {
        uint160 hash = insecure_GetRandUInt160(rand);
        const CTxDestination dst_key = CKeyID(hash);
        const CTxDestination dst_scr = ScriptID(hash);

        for (int tokenAware = 0; tokenAware < 2; ++tokenAware)
        {
            const std::string encoded_key = EncodeCashAddr(dst_key, params, tokenAware);
            bool decodedTokenAware{};
            const CTxDestination decoded_key = DecodeCashAddr(encoded_key, params, &decodedTokenAware);
            BOOST_CHECK_EQUAL(bool(tokenAware), decodedTokenAware);

            const std::string encoded_scr = EncodeCashAddr(dst_scr, params, tokenAware);
            const CTxDestination decoded_scr = DecodeCashAddr(encoded_scr, params, &decodedTokenAware);
            BOOST_CHECK_EQUAL(bool(tokenAware), decodedTokenAware);

            std::string err("cashaddr failed for hash: ");
            err += hash.ToString();

            BOOST_CHECK_MESSAGE(dst_key == decoded_key, err);
            BOOST_CHECK_MESSAGE(dst_scr == decoded_scr, err);

            BOOST_CHECK_MESSAGE(DstTypeChecker::IsKeyDst(decoded_key), err);
            BOOST_CHECK_MESSAGE(DstTypeChecker::IsScriptDst(decoded_scr), err);
        }
    }
}

/**
 * Cashaddr payload made of 5-bit nibbles. The last one is padded. When
 * converting back to bytes, this extra padding is truncated. In order to ensure
 * cashaddr are cannonicals, we check that the data we truncate is zeroed.
 */
BOOST_AUTO_TEST_CASE(check_padding)
{
    uint8_t version = 0;
    std::vector<uint8_t> data = {version};
    for (size_t i = 0; i < 33; ++i)
    {
        data.push_back(1);
    }

    BOOST_CHECK_EQUAL(data.size(), 34UL);

    const CTxDestination nodst = CNoDestination{};
    const CChainParams params = Params(CBaseChainParams::MAIN);

    for (uint8_t i = 0; i < 32; i++)
    {
        data[data.size() - 1] = i;
        std::string fake = cashaddr::Encode(params.CashAddrPrefix(), data);
        const CTxDestination dst = DecodeCashAddr(fake, params);

        // We have 168 bits of payload encoded as 170 bits in 5 bits nimbles. As
        // a result, we must have 2 zeros.
        if (i & 0x03)
        {
            BOOST_CHECK(dst == nodst);
        }
        else
        {
            BOOST_CHECK(!(dst == nodst));
        }
    }
}

/**
 * We ensure type is extracted properly from the version.
 */
BOOST_AUTO_TEST_CASE(check_type)
{
    std::vector<uint8_t> data;
    data.resize(34);

    const CChainParams params = Params(CBaseChainParams::MAIN);

    for (uint8_t v = 0; v < 16; v++)
    {
        std::fill(begin(data), end(data), 0);
        data[0] = v;
        auto content = DecodeCashAddrContent(cashaddr::Encode(params.CashAddrPrefix(), data), params);
        BOOST_CHECK_EQUAL(content.type, v);
        BOOST_CHECK_EQUAL(content.hash.size(), 20UL);

        // Check that using the reserved bit result in a failure.
        data[0] |= 0x10;
        content = DecodeCashAddrContent(cashaddr::Encode(params.CashAddrPrefix(), data), params);
        BOOST_CHECK_EQUAL(content.type, 0);
        BOOST_CHECK_EQUAL(content.hash.size(), 0UL);
    }
}

/**
 * We ensure size is extracted and checked properly.
 */
BOOST_AUTO_TEST_CASE(check_size)
{
    const CTxDestination nodst = CNoDestination{};
    const CChainParams params = Params(CBaseChainParams::MAIN);

    std::vector<uint8_t> data;

    for (auto ps : valid_sizes)
    {
        // Number of bytes required for a 5-bit packed version of a hash, with
        // version byte.  Add half a byte(4) so integer math provides the next
        // multiple-of-5 that would fit all the data.
        size_t expectedSize = (8 * (1 + ps.second) + 4) / 5;
        data.resize(expectedSize);
        std::fill(begin(data), end(data), 0);
        // After conversion from 8 bit packing to 5 bit packing, the size will
        // be in the second 5-bit group, shifted left twice.
        data[1] = ps.first << 2;

        auto content = DecodeCashAddrContent(cashaddr::Encode(params.CashAddrPrefix(), data), params);

        BOOST_CHECK_EQUAL(content.type, 0);
        BOOST_CHECK_EQUAL(content.hash.size(), ps.second);
        BOOST_CHECK(!content.IsNull());

        data.push_back(0);
        content = DecodeCashAddrContent(cashaddr::Encode(params.CashAddrPrefix(), data), params);

        BOOST_CHECK_EQUAL(content.type, 0);
        BOOST_CHECK_EQUAL(content.hash.size(), 0UL);
        BOOST_CHECK(content.IsNull());

        data.pop_back();
        data.pop_back();
        content = DecodeCashAddrContent(cashaddr::Encode(params.CashAddrPrefix(), data), params);

        BOOST_CHECK_EQUAL(content.type, 0);
        BOOST_CHECK_EQUAL(content.hash.size(), 0UL);
        BOOST_CHECK(content.IsNull());
    }
}

BOOST_AUTO_TEST_CASE(test_addresses)
{
    const CChainParams params = Params(CBaseChainParams::MAIN);

    std::vector<std::vector<uint8_t> > hash{
        {118, 160, 64, 83, 189, 160, 168, 139, 218, 81, 119, 184, 106, 21, 195, 178, 159, 85, 152, 115},
        {203, 72, 18, 50, 41, 156, 213, 116, 49, 81, 172, 75, 45, 99, 174, 25, 142, 123, 176, 169},
        {1, 31, 40, 228, 115, 201, 95, 64, 19, 215, 213, 62, 197, 251, 195, 180, 45, 248, 237, 16}};

    std::vector<std::string> pubkey = {"bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a",
        "bitcoincash:qr95sy3j9xwd2ap32xkykttr4cvcu7as4y0qverfuy",
        "bitcoincash:qqq3728yw0y47sqn6l2na30mcw6zm78dzqre909m2r"};
    std::vector<std::string> token_pubkey = {"bitcoincash:zpm2qsznhks23z7629mms6s4cwef74vcwvrqekrq9w",
        "bitcoincash:zr95sy3j9xwd2ap32xkykttr4cvcu7as4yg2l8d0rh",
        "bitcoincash:zqq3728yw0y47sqn6l2na30mcw6zm78dzqynk3ta4s"};
    std::vector<std::string> script = {"bitcoincash:ppm2qsznhks23z7629mms6s4cwef74vcwvn0h829pq",
        "bitcoincash:pr95sy3j9xwd2ap32xkykttr4cvcu7as4yc93ky28e",
        "bitcoincash:pqq3728yw0y47sqn6l2na30mcw6zm78dzq5ucqzc37"};
    std::vector<std::string> token_script = {"bitcoincash:rpm2qsznhks23z7629mms6s4cwef74vcwv59yeyr7n",
        "bitcoincash:rr95sy3j9xwd2ap32xkykttr4cvcu7as4yl0zg2vc2",
        "bitcoincash:rqq3728yw0y47sqn6l2na30mcw6zm78dzqnkt7v7wd"};

    for (size_t i = 0; i < hash.size(); ++i)
    {
        const CTxDestination dstKey = CKeyID(uint160(hash[i]));
        BOOST_CHECK_EQUAL(pubkey[i], EncodeCashAddr(dstKey, params));

        CashAddrContent keyContent{PUBKEY_TYPE, hash[i]};
        BOOST_CHECK_EQUAL(pubkey[i], EncodeCashAddr("bitcoincash", keyContent));
        BOOST_CHECK(!keyContent.IsTokenAwareType());

        CashAddrContent tokenKeyContent{TOKEN_PUBKEY_TYPE, hash[i]};
        BOOST_CHECK_EQUAL(token_pubkey[i], EncodeCashAddr("bitcoincash", tokenKeyContent));
        BOOST_CHECK(tokenKeyContent.IsTokenAwareType());

        const CTxDestination dstScript = ScriptID(uint160(hash[i]));
        BOOST_CHECK_EQUAL(script[i], EncodeCashAddr(dstScript, params));

        CashAddrContent scriptContent{SCRIPT_TYPE, hash[i]};
        BOOST_CHECK_EQUAL(script[i], EncodeCashAddr("bitcoincash", scriptContent));
        BOOST_CHECK(!scriptContent.IsTokenAwareType());

        CashAddrContent tokenScriptContent{TOKEN_SCRIPT_TYPE, hash[i]};
        BOOST_CHECK_EQUAL(token_script[i], EncodeCashAddr("bitcoincash", tokenScriptContent));
        BOOST_CHECK(tokenScriptContent.IsTokenAwareType());
    }
}

struct CashAddrTestVector
{
    std::string prefix;
    CashAddrType type;
    std::vector<uint8_t> hash;
    std::string addr;
};

BOOST_AUTO_TEST_CASE(test_vectors)
{
    std::vector<CashAddrTestVector> cases = {
        // 20 bytes
        {"bitcoincash", PUBKEY_TYPE, ParseHex("F5BF48B397DAE70BE82B3CCA4793F8EB2B6CDAC9"),
            "bitcoincash:qr6m7j9njldwwzlg9v7v53unlr4jkmx6eylep8ekg2"},
        {"bitcoincash", TOKEN_PUBKEY_TYPE, ParseHex("F5BF48B397DAE70BE82B3CCA4793F8EB2B6CDAC9"),
            "bitcoincash:zr6m7j9njldwwzlg9v7v53unlr4jkmx6eycnjehshe"},
        {"bchtest", SCRIPT_TYPE, ParseHex("F5BF48B397DAE70BE82B3CCA4793F8EB2B6CDAC9"),
            "bchtest:pr6m7j9njldwwzlg9v7v53unlr4jkmx6eyvwc0uz5t"},
        {"bchtest", TOKEN_SCRIPT_TYPE, ParseHex("F5BF48B397DAE70BE82B3CCA4793F8EB2B6CDAC9"),
            "bchtest:rr6m7j9njldwwzlg9v7v53unlr4jkmx6eytyt3jytc"},
        {"prefix", CashAddrType(15), ParseHex("F5BF48B397DAE70BE82B3CCA4793F8EB2B6CDAC9"),
            "prefix:0r6m7j9njldwwzlg9v7v53unlr4jkmx6ey3qnjwsrf"},
        {"bchreg", PUBKEY_TYPE, ParseHex("d85c2b71d0060b09c9886aeb815e50991dda124d"),
            "bchreg:qrv9c2m36qrqkzwf3p4whq272zv3mksjf5ln6v9le5"},
        {"bchreg", TOKEN_PUBKEY_TYPE, ParseHex("d85c2b71d0060b09c9886aeb815e50991dda124d"),
            "bchreg:zrv9c2m36qrqkzwf3p4whq272zv3mksjf5cefjtex8"},
        {"bchreg", PUBKEY_TYPE, ParseHex("00aea9a2e5f0f876a588df5546e8742d1d87008f"),
            "bchreg:qqq2a2dzuhc0sa493r0423hgwsk3mpcq3upac4z3wr"},
        {"bchreg", TOKEN_PUBKEY_TYPE, ParseHex("00aea9a2e5f0f876a588df5546e8742d1d87008f"),
            "bchreg:zqq2a2dzuhc0sa493r0423hgwsk3mpcq3uxhttvh3s"},
        // 24 bytes
        {"bitcoincash", PUBKEY_TYPE, ParseHex("7ADBF6C17084BC86C1706827B41A56F5CA32865925E946EA"),
            "bitcoincash:q9adhakpwzztepkpwp5z0dq62m6u5v5xtyj7j3h2ws4mr9g0"},
        {"bitcoincash", TOKEN_PUBKEY_TYPE, ParseHex("7ADBF6C17084BC86C1706827B41A56F5CA32865925E946EA"),
            "bitcoincash:z9adhakpwzztepkpwp5z0dq62m6u5v5xtyj7j3h2upmv9v72"},
        {"bchtest", SCRIPT_TYPE, ParseHex("7ADBF6C17084BC86C1706827B41A56F5CA32865925E946EA"),
            "bchtest:p9adhakpwzztepkpwp5z0dq62m6u5v5xtyj7j3h2u94tsynr"},
        {"bchtest", TOKEN_SCRIPT_TYPE, ParseHex("7ADBF6C17084BC86C1706827B41A56F5CA32865925E946EA"),
            "bchtest:r9adhakpwzztepkpwp5z0dq62m6u5v5xtyj7j3h2w5mukd9x"},
        {"prefix", CashAddrType(15), ParseHex("7ADBF6C17084BC86C1706827B41A56F5CA32865925E946EA"),
            "prefix:09adhakpwzztepkpwp5z0dq62m6u5v5xtyj7j3h2p29kc2lp"},
        // 28 bytes
        {"bitcoincash", PUBKEY_TYPE, ParseHex("3A84F9CF51AAE98A3BB3A78BF16A6183790B18719126325BFC0C075B"),
            "bitcoincash:qgagf7w02x4wnz3mkwnchut2vxphjzccwxgjvvjmlsxqwkcw59jxxuz"},
        {"bitcoincash", TOKEN_PUBKEY_TYPE, ParseHex("3A84F9CF51AAE98A3BB3A78BF16A6183790B18719126325BFC0C075B"),
            "bitcoincash:zgagf7w02x4wnz3mkwnchut2vxphjzccwxgjvvjmlsxqwkc8c9wvd0v"},
        {"bchtest", SCRIPT_TYPE, ParseHex("3A84F9CF51AAE98A3BB3A78BF16A6183790B18719126325BFC0C075B"),
            "bchtest:pgagf7w02x4wnz3mkwnchut2vxphjzccwxgjvvjmlsxqwkcvs7md7wt"},
        {"bchtest", TOKEN_SCRIPT_TYPE, ParseHex("3A84F9CF51AAE98A3BB3A78BF16A6183790B18719126325BFC0C075B"),
            "bchtest:rgagf7w02x4wnz3mkwnchut2vxphjzccwxgjvvjmlsxqwkc9u7884a9"},
        {"prefix", CashAddrType(15), ParseHex("3A84F9CF51AAE98A3BB3A78BF16A6183790B18719126325BFC0C075B"),
            "prefix:0gagf7w02x4wnz3mkwnchut2vxphjzccwxgjvvjmlsxqwkc5djw8s9g"},
        // 32 bytes
        {"bitcoincash", PUBKEY_TYPE,
            ParseHex("3173EF6623C6B48FFD1A3DCC0CC6489B0A07BB47A37F47CFEF4FE69DE825"
                     "C060"),
            "bitcoincash:"
            "qvch8mmxy0rtfrlarg7ucrxxfzds5pamg73h7370aa87d80gyhqxq5nlegake"},
        {"bitcoincash", TOKEN_PUBKEY_TYPE,
            ParseHex("3173EF6623C6B48FFD1A3DCC0CC6489B0A07BB47A37F47CFEF4FE69DE825"
                     "C060"),
            "bitcoincash:"
            "zvch8mmxy0rtfrlarg7ucrxxfzds5pamg73h7370aa87d80gyhqxqxqrc3u0j"},
        {"bchtest", SCRIPT_TYPE,
            ParseHex("3173EF6623C6B48FFD1A3DCC0CC6489B0A07BB47A37F47CFEF4FE69DE825"
                     "C060"),
            "bchtest:"
            "pvch8mmxy0rtfrlarg7ucrxxfzds5pamg73h7370aa87d80gyhqxq7fqng6m6"},
        {"bchtest", TOKEN_SCRIPT_TYPE,
            ParseHex("3173EF6623C6B48FFD1A3DCC0CC6489B0A07BB47A37F47CFEF4FE69DE825"
                     "C060"),
            "bchtest:"
            "rvch8mmxy0rtfrlarg7ucrxxfzds5pamg73h7370aa87d80gyhqxqv6uj3mz3"},
        {"prefix", CashAddrType(15),
            ParseHex("3173EF6623C6B48FFD1A3DCC0CC6489B0A07BB47A37F47CFEF4FE69DE825"
                     "C060"),
            "prefix:"
            "0vch8mmxy0rtfrlarg7ucrxxfzds5pamg73h7370aa87d80gyhqxqsh6jgp6w"},
        // 40 bytes
        {"bitcoincash", PUBKEY_TYPE,
            ParseHex("C07138323E00FA4FC122D3B85B9628EA810B3F381706385E289B0B256311"
                     "97D194B5C238BEB136FB"),
            "bitcoincash:"
            "qnq8zwpj8cq05n7pytfmskuk9r4gzzel8qtsvwz79zdskftrzxtar994cgutavfklv39g"
            "r3uvz"},
        {"bitcoincash", TOKEN_PUBKEY_TYPE,
            ParseHex("C07138323E00FA4FC122D3B85B9628EA810B3F381706385E289B0B256311"
                     "97D194B5C238BEB136FB"),
            "bitcoincash:"
            "znq8zwpj8cq05n7pytfmskuk9r4gzzel8qtsvwz79zdskftrzxtar994cgutavfklvyjy"
            "sntx8"},
        {"bchtest", SCRIPT_TYPE,
            ParseHex("C07138323E00FA4FC122D3B85B9628EA810B3F381706385E289B0B256311"
                     "97D194B5C238BEB136FB"),
            "bchtest:"
            "pnq8zwpj8cq05n7pytfmskuk9r4gzzel8qtsvwz79zdskftrzxtar994cgutavfklvmgm"
            "6ynej"},
        {"bchtest", TOKEN_SCRIPT_TYPE,
            ParseHex("C07138323E00FA4FC122D3B85B9628EA810B3F381706385E289B0B256311"
                     "97D194B5C238BEB136FB"),
            "bchtest:"
            "rnq8zwpj8cq05n7pytfmskuk9r4gzzel8qtsvwz79zdskftrzxtar994cgutavfklvwlh"
            "fxynh"},
        {"prefix", CashAddrType(15),
            ParseHex("C07138323E00FA4FC122D3B85B9628EA810B3F381706385E289B0B256311"
                     "97D194B5C238BEB136FB"),
            "prefix:"
            "0nq8zwpj8cq05n7pytfmskuk9r4gzzel8qtsvwz79zdskftrzxtar994cgutavfklvwsv"
            "ctzqy"},
        // 48 bytes
        {"bitcoincash", PUBKEY_TYPE,
            ParseHex("E361CA9A7F99107C17A622E047E3745D3E19CF804ED63C5C40C6BA763696"
                     "B98241223D8CE62AD48D863F4CB18C930E4C"),
            "bitcoincash:"
            "qh3krj5607v3qlqh5c3wq3lrw3wnuxw0sp8dv0zugrrt5a3kj6ucysfz8kxwv2k53krr7"
            "n933jfsunqex2w82sl"},
        {"bitcoincash", TOKEN_PUBKEY_TYPE,
            ParseHex("E361CA9A7F99107C17A622E047E3745D3E19CF804ED63C5C40C6BA763696"
                     "B98241223D8CE62AD48D863F4CB18C930E4C"),
            "bitcoincash:"
            "zh3krj5607v3qlqh5c3wq3lrw3wnuxw0sp8dv0zugrrt5a3kj6ucysfz8kxwv2k53krr7"
            "n933jfsunq4e575wfw"},
        {"bchtest", SCRIPT_TYPE,
            ParseHex("E361CA9A7F99107C17A622E047E3745D3E19CF804ED63C5C40C6BA763696"
                     "B98241223D8CE62AD48D863F4CB18C930E4C"),
            "bchtest:"
            "ph3krj5607v3qlqh5c3wq3lrw3wnuxw0sp8dv0zugrrt5a3kj6ucysfz8kxwv2k53krr7"
            "n933jfsunqnzf7mt6x"},
        {"bchtest", TOKEN_SCRIPT_TYPE,
            ParseHex("E361CA9A7F99107C17A622E047E3745D3E19CF804ED63C5C40C6BA763696"
                     "B98241223D8CE62AD48D863F4CB18C930E4C"),
            "bchtest:"
            "rh3krj5607v3qlqh5c3wq3lrw3wnuxw0sp8dv0zugrrt5a3kj6ucysfz8kxwv2k53krr7"
            "n933jfsunqlahwg0rh"},
        {"prefix", CashAddrType(15),
            ParseHex("E361CA9A7F99107C17A622E047E3745D3E19CF804ED63C5C40C6BA763696"
                     "B98241223D8CE62AD48D863F4CB18C930E4C"),
            "prefix:"
            "0h3krj5607v3qlqh5c3wq3lrw3wnuxw0sp8dv0zugrrt5a3kj6ucysfz8kxwv2k53krr7"
            "n933jfsunqakcssnmn"},
        // 56 bytes
        {"bitcoincash", PUBKEY_TYPE,
            ParseHex("D9FA7C4C6EF56DC4FF423BAAE6D495DBFF663D034A72D1DC7D52CBFE7D1E"
                     "6858F9D523AC0A7A5C34077638E4DD1A701BD017842789982041"),
            "bitcoincash:"
            "qmvl5lzvdm6km38lgga64ek5jhdl7e3aqd9895wu04fvhlnare5937w4ywkq57juxsrhv"
            "w8ym5d8qx7sz7zz0zvcypqscw8jd03f"},
        {"bitcoincash", TOKEN_PUBKEY_TYPE,
            ParseHex("D9FA7C4C6EF56DC4FF423BAAE6D495DBFF663D034A72D1DC7D52CBFE7D1E"
                     "6858F9D523AC0A7A5C34077638E4DD1A701BD017842789982041"),
            "bitcoincash:"
            "zmvl5lzvdm6km38lgga64ek5jhdl7e3aqd9895wu04fvhlnare5937w4ywkq57juxsrhv"
            "w8ym5d8qx7sz7zz0zvcypqswr8epnvt"},
        {"bchtest", SCRIPT_TYPE,
            ParseHex("D9FA7C4C6EF56DC4FF423BAAE6D495DBFF663D034A72D1DC7D52CBFE7D1E"
                     "6858F9D523AC0A7A5C34077638E4DD1A701BD017842789982041"),
            "bchtest:"
            "pmvl5lzvdm6km38lgga64ek5jhdl7e3aqd9895wu04fvhlnare5937w4ywkq57juxsrhv"
            "w8ym5d8qx7sz7zz0zvcypqs6kgdsg2g"},
        {"bchtest", TOKEN_SCRIPT_TYPE,
            ParseHex("D9FA7C4C6EF56DC4FF423BAAE6D495DBFF663D034A72D1DC7D52CBFE7D1E"
                     "6858F9D523AC0A7A5C34077638E4DD1A701BD017842789982041"),
            "bchtest:"
            "rmvl5lzvdm6km38lgga64ek5jhdl7e3aqd9895wu04fvhlnare5937w4ywkq57juxsrhv"
            "w8ym5d8qx7sz7zz0zvcypqsvmgxu5h2"},
        {"prefix", CashAddrType(15),
            ParseHex("D9FA7C4C6EF56DC4FF423BAAE6D495DBFF663D034A72D1DC7D52CBFE7D1E"
                     "6858F9D523AC0A7A5C34077638E4DD1A701BD017842789982041"),
            "prefix:"
            "0mvl5lzvdm6km38lgga64ek5jhdl7e3aqd9895wu04fvhlnare5937w4ywkq57juxsrhv"
            "w8ym5d8qx7sz7zz0zvcypqsgjrqpnw8"},
        // 64 bytes
        {"bitcoincash", PUBKEY_TYPE,
            ParseHex("D0F346310D5513D9E01E299978624BA883E6BDA8F4C60883C10F28C2967E"
                     "67EC77ECC7EEEAEAFC6DA89FAD72D11AC961E164678B868AEEEC5F2C1DA0"
                     "8884175B"),
            "bitcoincash:"
            "qlg0x333p4238k0qrc5ej7rzfw5g8e4a4r6vvzyrcy8j3s5k0en7calvclhw46hudk5fl"
            "ttj6ydvjc0pv3nchp52amk97tqa5zygg96mtky5sv5w"},
        {"bitcoincash", TOKEN_PUBKEY_TYPE,
            ParseHex("D0F346310D5513D9E01E299978624BA883E6BDA8F4C60883C10F28C2967E"
                     "67EC77ECC7EEEAEAFC6DA89FAD72D11AC961E164678B868AEEEC5F2C1DA0"
                     "8884175B"),
            "bitcoincash:"
            "zlg0x333p4238k0qrc5ej7rzfw5g8e4a4r6vvzyrcy8j3s5k0en7calvclhw46hudk5fl"
            "ttj6ydvjc0pv3nchp52amk97tqa5zygg96m4zqdd0qv"},
        {"bchtest", SCRIPT_TYPE,
            ParseHex("D0F346310D5513D9E01E299978624BA883E6BDA8F4C60883C10F28C2967E"
                     "67EC77ECC7EEEAEAFC6DA89FAD72D11AC961E164678B868AEEEC5F2C1DA0"
                     "8884175B"),
            "bchtest:"
            "plg0x333p4238k0qrc5ej7rzfw5g8e4a4r6vvzyrcy8j3s5k0en7calvclhw46hudk5fl"
            "ttj6ydvjc0pv3nchp52amk97tqa5zygg96mc773cwez"},
        {"bchtest", TOKEN_SCRIPT_TYPE,
            ParseHex("D0F346310D5513D9E01E299978624BA883E6BDA8F4C60883C10F28C2967E"
                     "67EC77ECC7EEEAEAFC6DA89FAD72D11AC961E164678B868AEEEC5F2C1DA0"
                     "8884175B"),
            "bchtest:"
            "rlg0x333p4238k0qrc5ej7rzfw5g8e4a4r6vvzyrcy8j3s5k0en7calvclhw46hudk5fl"
            "ttj6ydvjc0pv3nchp52amk97tqa5zygg96mx26g9ddq"},
        {"prefix", CashAddrType(15),
            ParseHex("D0F346310D5513D9E01E299978624BA883E6BDA8F4C60883C10F28C2967E"
                     "67EC77ECC7EEEAEAFC6DA89FAD72D11AC961E164678B868AEEEC5F2C1DA0"
                     "8884175B"),
            "prefix:"
            "0lg0x333p4238k0qrc5ej7rzfw5g8e4a4r6vvzyrcy8j3s5k0en7calvclhw46hudk5fl"
            "ttj6ydvjc0pv3nchp52amk97tqa5zygg96ms92w6845"},
    };

    for (const auto &t : cases)
    {
        CashAddrContent content{t.type, t.hash};
        BOOST_CHECK_EQUAL(t.addr, EncodeCashAddr(t.prefix, content));

        std::string err("hash mistmatch for address: ");
        err += t.addr;

        content = DecodeCashAddrContent(t.addr, t.prefix);
        BOOST_CHECK_EQUAL(t.type, content.type);
        BOOST_CHECK_MESSAGE(t.hash == content.hash, err);
    }
}

BOOST_AUTO_TEST_CASE(token_json_test_vectors)
{
    static_assert(sizeof(json_tests::cashaddr_token_types[0]) == 1);
    const UniValue vecs = read_json({reinterpret_cast<const char *>(&json_tests::cashaddr_token_types[0]),
        std::size(json_tests::cashaddr_token_types)});
    BOOST_CHECK(!vecs.empty());
    for (size_t i = 0; i < vecs.size(); ++i)
    {
        BOOST_TEST_MESSAGE(strprintf("Running token JSON test %i ...", i));
        const UniValue &o = vecs[i].get_obj();
        BOOST_CHECK(!o.empty());
        struct TestVector
        {
            size_t payloadSize;
            uint8_t type;
            std::string cashaddr;
            std::vector<uint8_t> payload;
        } const tv{size_t(o["payloadSize"].get_int()), uint8_t(o["type"].get_int()), o["cashaddr"].get_str(),
            ParseHex(o["payload"].get_str())};
        auto GetPrefix = [](const std::string &ca) -> std::string
        {
            const auto pos = ca.find_first_of(':');
            if (pos == ca.npos)
                throw std::runtime_error(strprintf("Cannot parse prefix from: %s", ca));
            return ca.substr(0, pos);
        };
        const CashAddrContent content = DecodeCashAddrContent(tv.cashaddr, GetPrefix(tv.cashaddr));
        BOOST_CHECK(!content.IsNull());
        BOOST_CHECK_EQUAL(uint8_t(content.type), tv.type);
        BOOST_CHECK_EQUAL(content.hash.size(), tv.payloadSize);
        BOOST_CHECK_EQUAL(HexStr(content.hash), HexStr(tv.payload));
    }
}

BOOST_AUTO_TEST_SUITE_END()
