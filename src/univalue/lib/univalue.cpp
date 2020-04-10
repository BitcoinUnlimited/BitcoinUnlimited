// Copyright 2014 BitPay Inc.
// Copyright 2015 Bitcoin Core Developers
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
    keys.clear();
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
    keys.push_back(key);
    values.emplace_back(std::move(val_));
}

void UniValue::__pushKV(std::string&& key, UniValue&& val_)
{
    keys.emplace_back(std::move(key));
    values.emplace_back(std::move(val_));
}

void UniValue::__pushKV(std::string&& key, const UniValue& val_)
{
    keys.emplace_back(std::move(key));
    values.push_back(val_);
}

void UniValue::__pushKV(const std::string& key, const UniValue& val_)
{
    keys.push_back(key);
    values.push_back(val_);
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
    size_t idx;
    if (check && findKey(key, idx))
        values[idx] = val_;
    else
        __pushKV(key, val_);
    return true;
}
bool UniValue::pushKV(const std::string& key, UniValue&& val_, bool check)
{
    if (typ != VOBJ)
        return false;

    size_t idx;
    if (check && findKey(key, idx))
        values[idx] = std::move(val_);
    else
        __pushKV(key, std::move(val_));
    return true;
}
bool UniValue::pushKV(std::string&& key, const UniValue& val_, bool check)
{
    if (typ != VOBJ)
        return false;

    size_t idx;
    if (check && findKey(key, idx))
        values[idx] = val_;
    else
        __pushKV(std::move(key), val_);
    return true;
}
bool UniValue::pushKV(std::string&& key, UniValue&& val_, bool check)
{
    if (typ != VOBJ)
        return false;

    size_t idx;
    if (check && findKey(key, idx))
        values[idx] = std::move(val_);
    else
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

    for (size_t i = 0, nKeys = obj.keys.size(); i < nKeys; i++)
        __pushKV(obj.keys[i], obj.values[i]);

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

    for (size_t i = 0, nKeys = obj.keys.size(); i < nKeys; i++)
        __pushKV(std::move(obj.keys[i]), std::move(obj.values[i]));
    obj.setObject(); // reset moved obj now to be tidy with memory.

    return true;
}

void UniValue::getObjMap(std::map<std::string,UniValue>& kv) const
{
    if (typ != VOBJ)
        return;

    kv.clear();
    for (size_t i = 0, nKeys = keys.size(); i < nKeys; ++i)
        kv[keys[i]] = values[i];
}

bool UniValue::findKey(const std::string& key, size_t& retIdx) const noexcept
{
    for (size_t i = 0, nKeys = keys.size(); i < nKeys; ++i) {
        if (keys[i] == key) {
            retIdx = i;
            return true;
        }
    }

    return false;
}

bool UniValue::checkObject(const std::map<std::string,UniValue::VType>& t) const noexcept
{
    if (typ != VOBJ)
        return false;

    size_t idx; // initialized if findKey below returns true
    for (const auto & kv : t) {
        if (!findKey(kv.first, idx))
            return false;

        if (values[idx].getType() != kv.second)
            return false;
    }

    return true;
}

const UniValue& UniValue::operator[](const std::string& key) const noexcept
{
    if (typ != VOBJ)
        return NullUniValue;

    size_t index; // initialized below if findKey returns true
    if (!findKey(key, index))
        return NullUniValue;

    return values[index];
}

const UniValue& UniValue::operator[](size_t index) const noexcept
{
    if (typ != VOBJ && typ != VARR)
        return NullUniValue;
    if (index >= values.size())
        return NullUniValue;

    return values[index];
}

bool UniValue::reserve(size_t n) {
    bool ret = false;
    if (isArray() || isObject()) {
        values.reserve(n);
        ret = true;
    }
    if (isObject()) {
        keys.reserve(n);
        ret = true;
    }
    return ret;
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
    return NULL;
}

const UniValue& find_value(const UniValue& obj, const std::string& name) noexcept
{
    // NB: keys is always empty if typ != VOBJ, and keys.size() == values.size() if type == VOBJ
    for (size_t i = 0, nKeys = obj.keys.size(); i < nKeys; ++i)
        if (obj.keys[i] == name)
            return obj.values[i];

    return NullUniValue;
}
