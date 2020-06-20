// Copyright 2014 BitPay Inc.
// Copyright 2015 Bitcoin Core Developers
// Copyright (c) 2020 The Bitcoin developers
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
    bool setInt(int val_) { return setInt(int64_t(val_)); }
    bool setFloat(double val);
    bool setStr(const std::string& val);
    bool setStr(std::string&& val) noexcept;
    bool setArray() noexcept;
    bool setObject() noexcept;

    constexpr enum VType getType() const noexcept { return typ; }
    constexpr const std::string& getValStr() const noexcept { return val; }

    /**
     * VOBJ/VARR: Returns whether the object/array is empty.
     * Other types: Returns true.
     *
     * Complexity: constant.
     *
     * Compatible with the upstream UniValue API.
     */
    bool empty() const noexcept {
        switch (typ) {
        case VOBJ:
            return entries.empty();
        case VARR:
            return values.empty();
        default:
            return true;
        }
    }

    /**
     * VOBJ/VARR: Returns the size of the object/array.
     * Other types: Returns zero.
     *
     * Complexity: constant.
     *
     * Compatible with the upstream UniValue API.
     */
    size_t size() const noexcept {
        switch (typ) {
        case VOBJ:
            return entries.size();
        case VARR:
            return values.size();
        default:
            return 0;
        }
    }

    /**
     * VOBJ/VARR: Increases the capacity of the underlying vector to at least n.
     * Other types: Does nothing.
     *
     * Complexity: at most linear in number of elements.
     *
     * Compatible with the upstream UniValue API for VOBJ/VARR but does not implement upstream behavior for other types.
     */
    void reserve(size_t n);

    constexpr bool getBool() const noexcept { return isTrue(); }

    /**
     * VOBJ: Returns a reference to the first value associated with the key, or NullUniValue if the key does not exist.
     * Other types: Returns NullUniValue.
     *
     * The returned reference follows the iterator invalidation rules of the underlying vector.
     *
     * Complexity: linear in number of elements.
     *
     * Compatible with the upstream UniValue API.
     *
     * If you want to distinguish between null values and missing keys, please use find() instead.
     */
    const UniValue& operator[](const std::string& key) const noexcept;

    /**
     * VOBJ: Returns a reference to the value at the numeric index (regardless of key), or NullUniValue if index >= object size.
     * VARR: Returns a reference to the element at the index, or NullUniValue if index >= array size.
     * Other types: Returns NullUniValue.
     *
     * The returned reference follows the iterator invalidation rules of the underlying vector.
     *
     * Complexity: constant.
     *
     * Compatible with the upstream UniValue API.
     *
     * To access the first or last value, consider using front() or back() instead.
     */
    const UniValue& operator[](size_t index) const noexcept;

    /**
     * Returns whether the UniValues are of the same type and contain equal data.
     * Two objects/arrays are not considered equal if elements are ordered differently.
     *
     * Complexity: linear in the amount of data to compare.
     *
     * This is a Bitcoin Cash Node extension of the UniValue API.
     */
    bool operator==(const UniValue& other) const noexcept;

    /**
     * Returns whether the UniValues are not of the same type or contain unequal data.
     * Two objects/arrays are not considered equal if elements are ordered differently.
     *
     * Complexity: linear in the amount of data to compare.
     *
     * This is a Bitcoin Cash Node extension of the UniValue API.
     */
    bool operator!=(const UniValue& other) const noexcept { return !(*this == other); }

    /**
     * VOBJ: Returns a reference to the first value (regardless of key), or NullUniValue if the object is empty.
     * VARR: Returns a reference to the first element, or NullUniValue if the array is empty.
     * Other types: Returns NullUniValue.
     *
     * The returned reference follows the iterator invalidation rules of the underlying vector.
     *
     * Complexity: constant.
     *
     * This is a Bitcoin Cash Node extension of the UniValue API.
     */
    const UniValue& front() const noexcept;

    /**
     * VOBJ: Returns a reference to the last value (regardless of key), or NullUniValue if the object is empty.
     * VARR: Returns a reference to the last element, or NullUniValue if the array is empty.
     * Other types: Returns NullUniValue.
     *
     * The returned reference follows the iterator invalidation rules of the underlying vector.
     *
     * Complexity: constant.
     *
     * This is a Bitcoin Cash Node extension of the UniValue API.
     */
    const UniValue& back() const noexcept;

    /**
     * VOBJ: Returns a pointer to the first value associated with the key, or nullptr if the key does not exist.
     * Other types: Returns nullptr.
     *
     * The returned pointer follows the iterator invalidation rules of the underlying vector.
     *
     * Complexity: linear in the number of elements.
     *
     * This is a Bitcoin Cash Node extension of the UniValue API.
     *
     * If you want to treat missing keys as null values, please use the [] operator with string argument instead.
     */
    const UniValue* find(const std::string& key) const noexcept;
    UniValue* find(const std::string& key) noexcept;

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
    bool read(const char *raw) { return read(raw, strlen(raw)); }
    bool read(const std::string& rawStr) { return read(rawStr.data(), rawStr.size()); }

private:
    UniValue::VType typ;
    std::string val;                       // numbers are stored as C++ strings
    std::vector<std::pair<std::string, UniValue>> entries;
    std::vector<UniValue> values;
    static const std::string boolTrueVal; // = "1"

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
    static inline void indentStr(Stream & stream, unsigned int prettyIndent, unsigned int indentLevel);

public:
    // Strict type-specific getters, these throw std::runtime_error if the
    // value is of unexpected type

    /**
     * VOBJ: Returns a reference to the underlying vector of key-value pairs.
     * Other types: Throws std::runtime_error.
     *
     * Destroying the object invalidates the returned reference.
     *
     * Complexity: constant.
     *
     * This is a Bitcoin Cash Node extension of the UniValue API.
     */
    const std::vector<std::pair<std::string, UniValue>>& getObjectEntries() const;

    /**
     * VARR: Returns a reference to the underlying vector of values.
     * Other types: Throws std::runtime_error.
     *
     * Destroying the array invalidates the returned reference.
     *
     * Complexity: constant.
     *
     * This is a Bitcoin Cash Node extension of the UniValue API.
     *
     * If you want to clear the array after using this method, consider using takeArrayValues() instead.
     */
    const std::vector<UniValue>& getArrayValues() const;

    /**
     * VARR: Changes the UniValue into an empty array and returns the old array contents as a vector.
     * Other types: Throws std::runtime_error.
     *
     * Complexity: constant.
     *
     * This is a Bitcoin Cash Node extension of the UniValue API.
     *
     * If you do not want to make the array empty, please use getArrayValues() instead.
     */
    std::vector<UniValue> takeArrayValues();

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
