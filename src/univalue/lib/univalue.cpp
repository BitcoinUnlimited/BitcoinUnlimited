// Copyright 2014 BitPay Inc.
// Copyright 2015 Bitcoin Core Developers
// Copyright (c) 2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <stdint.h>
#include <iomanip>
#include <sstream>
#include <stdlib.h>

#include "univalue.h"

const UniValue NullUniValue;

const std::string UniValue::boolTrueVal{"1"};

void UniValue::clear() noexcept
{
    typ = VNULL;
    val.clear();
    entries.clear();
    values.clear();
}

bool UniValue::setNull() noexcept
{
    clear();
    return true;
}

bool UniValue::setBool(bool val_)
{
    clear();
    typ = VBOOL;
    if (val_)
        val = boolTrueVal;
    return true;
}

static bool validNumStr(const std::string& s)
{
    std::string tokenVal;
    unsigned int consumed;
    enum jtokentype tt = getJsonToken(tokenVal, consumed, s.data(), s.data() + s.size());
    return (tt == JTOK_NUMBER);
}

bool UniValue::setNumStr(const std::string& val_)
{
    if (!validNumStr(val_))
        return false;

    clear();
    typ = VNUM;
    val = val_;
    return true;
}
bool UniValue::setNumStr(std::string&& val_) noexcept
{
    if (!validNumStr(val_))
        return false;

    clear();
    typ = VNUM;
    val = std::move(val_);
    return true;
}

bool UniValue::setInt(uint64_t val_)
{
    std::ostringstream oss;

    oss << val_;

    return setNumStr(oss.str());
}

bool UniValue::setInt(int64_t val_)
{
    std::ostringstream oss;

    oss << val_;

    return setNumStr(oss.str());
}

bool UniValue::setInt(unsigned int val_)
{
    std::ostringstream oss;

    oss << val_;

    return setNumStr(oss.str());
}

bool UniValue::setFloat(double val_)
{
    std::ostringstream oss;

    oss << std::setprecision(16) << val_;

    bool ret = setNumStr(oss.str());
    typ = VNUM;
    return ret;
}

bool UniValue::setStr(const std::string& val_)
{
    clear();
    typ = VSTR;
    val = val_;
    return true;
}
bool UniValue::setStr(std::string&& val_) noexcept
{
    clear();
    typ = VSTR;
    val = std::move(val_);
    return true;
}

bool UniValue::setArray() noexcept
{
    clear();
    typ = VARR;
    return true;
}

bool UniValue::setObject() noexcept
{
    clear();
    typ = VOBJ;
    return true;
}

bool UniValue::push_back(const UniValue& val_)
{
#ifdef DEBUG
    assert(typ == VARR);
#else
    if (typ != VARR)
    {
        return false;
    }
#endif
    values.push_back(val_);
    return true;
}

bool UniValue::push_back(UniValue&& val_)
{
    if (typ != VARR)
        return false;

    values.emplace_back(std::move(val_));
    return true;
}

bool UniValue::push_backV(const std::vector<UniValue>& vec)
{
#ifdef DEBUG
    assert(typ == VARR);
#else
    if (typ != VARR)
    {
        return false;
    }
#endif
    values.insert(values.end(), vec.begin(), vec.end());

    return true;
}
bool UniValue::push_backV(std::vector<UniValue>&& vec)
{
    if (typ != VARR)
        return false;

    values.reserve(std::max(values.size() + vec.size(), values.capacity()));
    for (auto & item : vec)
        values.emplace_back(std::move(item));
    vec.clear(); // clear vector now to be tidy with memory

    return true;
}

void UniValue::__pushKV(const std::string& key, UniValue&& val_)
{
    entries.emplace_back(key, std::move(val_));
}

void UniValue::__pushKV(std::string&& key, UniValue&& val_)
{
    entries.emplace_back(std::move(key), std::move(val_));
}

void UniValue::__pushKV(std::string&& key, const UniValue& val_)
{
    entries.emplace_back(std::move(key), val_);
}

void UniValue::__pushKV(const std::string& key, const UniValue& val_)
{
    entries.emplace_back(key, val_);
}

bool UniValue::pushKV(const std::string& key, const UniValue& val_, bool check)
{
#ifdef DEBUG
    assert(typ == VOBJ);
#else
    if (typ != VOBJ)
    {
        return false;
    }
#endif
    if (check) {
        if (auto found = find(key)) {
            *found = val_;
            return true;
        }
    }
    __pushKV(key, val_);
    return true;
}
bool UniValue::pushKV(const std::string& key, UniValue&& val_, bool check)
{
    if (typ != VOBJ)
        return false;
    if (check) {
        if (auto found = find(key)) {
            *found = std::move(val_);
            return true;
        }
    }
    __pushKV(key, std::move(val_));
    return true;
}
bool UniValue::pushKV(std::string&& key, const UniValue& val_, bool check)
{
    if (typ != VOBJ)
        return false;
    if (check) {
        if (auto found = find(key)) {
            *found = val_;
            return true;
        }
    }
    __pushKV(std::move(key), val_);
    return true;
}
bool UniValue::pushKV(std::string&& key, UniValue&& val_, bool check)
{
    if (typ != VOBJ)
        return false;
    if (check) {
        if (auto found = find(key)) {
            *found = std::move(val_);
            return true;
        }
    }
    __pushKV(std::move(key), std::move(val_));
    return true;
}

bool UniValue::pushKVs(const UniValue& obj)
{
#ifdef DEBUG
    assert(typ == VOBJ && obj.typ == VOBJ);
#else
    if (typ != VOBJ || obj.typ != VOBJ)
    {
        return false;
    }
#endif

    for (auto& entry : obj.entries)
        entries.emplace_back(entry);

    return true;
}
bool UniValue::pushKVs(UniValue&& obj)
{
#ifdef DEBUG
    assert(typ == VOBJ && obj.typ == VOBJ);
#else
    if (typ != VOBJ || obj.typ != VOBJ)
    {
        return false;
    }
#endif

    for (auto& entry : obj.entries)
        entries.emplace_back(std::move(entry));
    obj.setObject(); // reset moved obj now to be tidy with memory.

    return true;
}

const UniValue& UniValue::operator[](const std::string& key) const noexcept
{
    if (auto found = find(key)) {
        return *found;
    }
    return NullUniValue;
}

const UniValue& UniValue::operator[](size_t index) const noexcept
{
    switch (typ) {
    case VOBJ:
        if (index < entries.size())
            return entries[index].second;
        return NullUniValue;
    case VARR:
        if (index < values.size())
            return values[index];
        return NullUniValue;
    default:
        return NullUniValue;
    }
}

const UniValue& UniValue::front() const noexcept
{
    switch (typ) {
    case VOBJ:
        if (!entries.empty())
            return entries.front().second;
        return NullUniValue;
    case VARR:
        if (!values.empty())
            return values.front();
        return NullUniValue;
    default:
        return NullUniValue;
    }
}

const UniValue& UniValue::back() const noexcept
{
    switch (typ) {
    case VOBJ:
        if (!entries.empty())
            return entries.back().second;
        return NullUniValue;
    case VARR:
        if (!values.empty())
            return values.back();
        return NullUniValue;
    default:
        return NullUniValue;
    }
}

const UniValue* UniValue::find(const std::string& key) const noexcept {
    for (auto& entry : entries) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}
UniValue* UniValue::find(const std::string& key) noexcept {
    for (auto& entry : entries) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool UniValue::operator==(const UniValue& other) const noexcept
{
    // Type must be equal.
    if (typ != other.typ)
        return false;
    // Some types have additional requirements for equality.
    switch (typ) {
    case VBOOL:
    case VNUM:
    case VSTR:
        return val == other.val;
    case VARR:
        return values == other.values;
    case VOBJ:
        return entries == other.entries;
    case VNULL:
        break;
    }
    // Returning true is the default behavior, but this is not included as a default statement inside the switch statement,
    // so that the compiler warns if some type is not explicitly listed there.
    return true;
}

void UniValue::reserve(size_t n)
{
    switch (typ) {
    case VOBJ:
        entries.reserve(n);
        break;
    case VARR:
        values.reserve(n);
        break;
    default:
        break;
    }
}

const char *uvTypeName(UniValue::VType t) noexcept
{
    switch (t) {
    case UniValue::VNULL: return "null";
    case UniValue::VBOOL: return "bool";
    case UniValue::VOBJ: return "object";
    case UniValue::VARR: return "array";
    case UniValue::VSTR: return "string";
    case UniValue::VNUM: return "number";
    }

    // not reached
    return nullptr;
}

const UniValue& find_value(const UniValue& obj, const std::string& name) noexcept
{
    // NB: keys is always empty if typ != VOBJ, and keys.size() == values.size() if type == VOBJ
    for (size_t i = 0, nKeys = obj.keys.size(); i < nKeys; ++i)
        if (obj.keys[i] == name)
            return obj.values[i];

    return NullUniValue;
}
