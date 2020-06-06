// Copyright 2014 BitPay Inc.
// Copyright 2015 Bitcoin Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef __UNIVALUE_H__
#define __UNIVALUE_H__

#include <stdint.h>
#include <string.h>

#include <cassert>
#include <map>
#include <string>
#include <utility>
#include <vector>

class UniValue {
public:
    enum VType { VNULL, VOBJ, VARR, VSTR, VNUM, VBOOL, };

    UniValue(UniValue::VType initialType = VNULL) noexcept : typ(initialType) {}
    UniValue(UniValue::VType initialType, const std::string& initialStr)
        : typ(initialType), val(initialStr) {}
    UniValue(UniValue::VType initialType, std::string&& initialStr) noexcept
        : typ(initialType), val(std::move(initialStr)) {}
    UniValue(uint64_t val_) { setInt(val_); }
    UniValue(int64_t val_) { setInt(val_); }
    UniValue(bool val_) { setBool(val_); }
    UniValue(int val_) { setInt(val_); }
    UniValue(unsigned int val_) { setInt(val_); }
    UniValue(double val_) { setFloat(val_); }
    UniValue(const std::string& val_) : typ(VSTR), val(val_) {}
    UniValue(std::string&& val_) noexcept : typ(VSTR), val(std::move(val_)) {}
    UniValue(const char *val_) : typ(VSTR), val(val_) {}

    void clear() noexcept;

    bool setNull() noexcept;
    bool setBool(bool val);
    bool setNumStr(const std::string& val);
    bool setNumStr(std::string&& val) noexcept;
    bool setInt(uint64_t val);
    bool setInt(int64_t val);
    bool setInt(unsigned int val);
    inline bool setInt(int val_) { return setInt(int64_t(val_)); }
    bool setFloat(double val);
    bool setStr(const std::string& val);
    bool setStr(std::string&& val) noexcept;
    bool setArray() noexcept;
    bool setObject() noexcept;

    constexpr enum VType getType() const noexcept { return typ; }
    constexpr const std::string& getValStr() const noexcept { return val; }

    inline bool empty() const noexcept { return values.empty(); }
    inline size_t size() const noexcept { return values.size(); }
    bool reserve(size_t n);

    constexpr bool getBool() const noexcept { return isTrue(); }
    bool checkObject(const std::map<std::string,UniValue::VType>& memberTypes) const noexcept;
    const UniValue& operator[](const std::string& key) const noexcept;
    const UniValue& operator[](size_t index) const noexcept;
    bool exists(const std::string& key) const noexcept { size_t i; return findKey(key, i); }

    constexpr bool isNull() const noexcept { return typ == VNULL; }
    constexpr bool isTrue() const noexcept { return typ == VBOOL && val == boolTrueVal; }
    constexpr bool isFalse() const noexcept { return typ == VBOOL && val != boolTrueVal; }
    constexpr bool isBool() const noexcept { return typ == VBOOL; }
    constexpr bool isStr() const noexcept { return typ == VSTR; }
    constexpr bool isNum() const noexcept { return typ == VNUM; }
    constexpr bool isArray() const noexcept { return typ == VARR; }
    constexpr bool isObject() const noexcept { return typ == VOBJ; }

    bool push_back(UniValue&& val);
    bool push_back(const UniValue& val);
    bool push_backV(const std::vector<UniValue>& vec);
    bool push_backV(std::vector<UniValue>&& vec);

    // checkForDupes=true is slower, but does a linear search through the keys to overwrite existing keys.
    // checkForDupes=false is faster, and will always append the new entry at the end (even if `key` exists).
    bool pushKV(const std::string& key, const UniValue& val, bool checkForDupes = true);
    bool pushKV(const std::string& key, UniValue&& val, bool checkForDupes = true);
    bool pushKV(std::string&& key, const UniValue& val, bool checkForDupes = true);
    bool pushKV(std::string&& key, UniValue&& val, bool checkForDupes = true);
    // Inserts all key/value pairs from `obj` into `this`.
    // Caveat: For performance, `this` is not checked for duplicate keys coming in from `obj`.
    // As a result, `this` may end up with duplicate keys if `obj` contains keys already
    // present in `this`.
    bool pushKVs(const UniValue& obj);
    bool pushKVs(UniValue&& obj);

    std::string write(unsigned int prettyIndent = 0,
                      unsigned int indentLevel = 0) const;

    bool read(const char *raw, size_t len);
    inline bool read(const char *raw) { return read(raw, strlen(raw)); }
    inline bool read(const std::string& rawStr) { return read(rawStr.data(), rawStr.size()); }

private:
    UniValue::VType typ;
    std::string val;                       // numbers are stored as C++ strings
    std::vector<std::string> keys;
    std::vector<UniValue> values;
    static const std::string boolTrueVal; // = "1"

    bool findKey(const std::string& key, size_t& retIdx) const noexcept;
    // __pushKV does not check for duplicate keys and simply appends at the end
    void __pushKV(const std::string& key, const UniValue& val);
    void __pushKV(const std::string& key, UniValue&& val);
    void __pushKV(std::string&& key, UniValue&& val);
    void __pushKV(std::string&& key, const UniValue& val);

    struct Stream;

    void writeStream(Stream & stream, unsigned int prettyIndent = 0, unsigned int indentLevel = 0) const;
    void writeArray(Stream & stream, unsigned int prettyIndent, unsigned int indentLevel) const;
    void writeObject(Stream & stream, unsigned int prettyIndent, unsigned int indentLevel) const;
    static void jsonEscape(Stream & stream, const std::string & inString);
    static void indentStr(Stream & stream, unsigned int prettyIndent, unsigned int indentLevel);

public:
    // Strict type-specific getters, these throw std::runtime_error if the
    // value is of unexpected type
    const std::vector<std::string>& getKeys() const;
    const std::vector<UniValue>& getValues() const;
    bool get_bool() const;
    const std::string& get_str() const;
    int get_int() const;
    int64_t get_int64() const;
    uint64_t get_uint64() const;
    uint32_t get_uint32() const;
    uint16_t get_uint16() const;
    uint8_t get_uint8() const;
    double get_real() const;
    const UniValue& get_obj() const;
    const UniValue& get_array() const;

    constexpr enum VType type() const noexcept { return getType(); }
    friend const UniValue& find_value( const UniValue& obj, const std::string& name) noexcept;
};

enum jtokentype {
    JTOK_ERR        = -1,
    JTOK_NONE       = 0,                           // eof
    JTOK_OBJ_OPEN,
    JTOK_OBJ_CLOSE,
    JTOK_ARR_OPEN,
    JTOK_ARR_CLOSE,
    JTOK_COLON,
    JTOK_COMMA,
    JTOK_KW_NULL,
    JTOK_KW_TRUE,
    JTOK_KW_FALSE,
    JTOK_NUMBER,
    JTOK_STRING,
};

extern enum jtokentype getJsonToken(std::string& tokenVal,
                                    unsigned int& consumed, const char *raw, const char *end);
extern const char *uvTypeName(UniValue::VType t) noexcept;

static constexpr bool jsonTokenIsValue(enum jtokentype jtt) noexcept
{
    switch (jtt) {
    case JTOK_KW_NULL:
    case JTOK_KW_TRUE:
    case JTOK_KW_FALSE:
    case JTOK_NUMBER:
    case JTOK_STRING:
        return true;

    default:
        return false;
    }

    // not reached
}

static constexpr bool json_isspace(int ch) noexcept
{
    switch (ch) {
    case 0x20:
    case 0x09:
    case 0x0a:
    case 0x0d:
        return true;

    default:
        return false;
    }

    // not reached
}

extern const UniValue NullUniValue;

const UniValue& find_value( const UniValue& obj, const std::string& name) noexcept;

#endif // __UNIVALUE_H__
