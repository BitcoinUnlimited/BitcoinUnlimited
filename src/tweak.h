// Copyright (c) 2016-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TWEAK_H
#define TWEAK_H

#include <string>

#include <mutex>
#include <thread>

#include "univalue/include/univalue.h"

class CTweakBase;

typedef std::string CTweakKey;
typedef std::map<CTweakKey, CTweakBase *> CTweakMap;
extern CTweakMap tweaks;

class CTweakBase
{
public:
    mutable std::recursive_mutex cs_tweak;

    CTweakBase(){};
    virtual std::string GetName() const = 0; // Returns the name of this statistic
    virtual std::string GetHelp() const = 0; // Returns the help for this statistic
    virtual UniValue Get() const = 0; // Returns the current value of this statistic
    virtual UniValue Set(const UniValue &val) = 0; // Returns NullUnivalue or an error string
    virtual std::string Validate(const UniValue &val)
    {
        return std::string();
    }; // Returns NullUnivalue or an error string
};


inline void fill(const UniValue &v, double &output)
{
    if (v.isStr())
        output = std::stod(v.get_str());
    else
        output = v.get_real();
}
inline void fill(const UniValue &v, float &output)
{
    if (v.isStr())
        output = std::stof(v.get_str());
    else
        output = v.get_real();
}
inline void fill(const UniValue &v, int &output)
{
    if (v.isStr())
        output = std::stoi(v.get_str());
    else
        output = v.get_int();
}

inline void fill(const UniValue &v, uint64_t &output)
{
    if (v.isStr())
        output = std::stoull(v.get_str());
    else
        output = v.get_int64();
}

inline void fill(const UniValue &v, int64_t &output)
{
    if (v.isStr())
        output = std::stoll(v.get_str());
    else
        output = v.get_int64();
}

inline void fill(const UniValue &v, uint32_t &output)
{
    if (v.isStr())
        output = (uint32_t)std::stoul(v.get_str());
    else
        output = (uint32_t)v.get_uint32();
}

inline void fill(const UniValue &v, uint16_t &output)
{
    if (v.isStr())
        output = (uint16_t)std::stoul(v.get_str());
    else
        output = (uint16_t)v.get_uint16();
}

inline void fill(const UniValue &v, uint8_t &output)
{
    if (v.isStr())
        output = (uint8_t)std::stoul(v.get_str());
    else
        output = (uint8_t)v.get_uint8();
}

inline void fill(const UniValue &v, std::string &output) { output = v.get_str(); }
inline void fill(const UniValue &v, bool &output)
{
    if (v.isStr())
    {
        std::string s = v.get_str();
        output = ((s[0] == 't') || (s[0] == 'T') || (s[0] == 'y') || (s[0] == 'Y') || (s[0] == '1'));
    }
    else
        output = v.get_bool();
}

/** A configuration parameter that is automatically hooked up to
 * bitcoin.conf, bitcoin-cli, and is available as a command line argument
 */
template <class DataType>
class CTweakRef : public CTweakBase
{
public:
    // Validation and assignment notification function.
    // If "validate" is true, then return nonempty error string if this field
    // can't be set to this value (value parameter contains the candidate
    // value).
    // If "validate" is false, this is a notification that this item has been set
    // (value parameter contains the old value).  You can return a string if you
    // want to give some kind of ACK message to the user.
    typedef std::string (*EventFn)(const DataType &value, DataType *item, bool validate);

protected:
    const std::string name;
    const std::string help;
    DataType *value;
    EventFn eventCb;

public:
    ~CTweakRef()
    {
        if (name.size())
            tweaks.erase(CTweakKey(name));
    }

    CTweakRef(const char *namep, const char *helpp, DataType *val, EventFn callback = nullptr)
        : name(namep), help(helpp), value(val), eventCb(callback)
    {
        tweaks[CTweakKey(name)] = this;
    }

    CTweakRef(const std::string &namep, const std::string &helpp, DataType *val, EventFn callback = nullptr)
        : name(namep), help(helpp), value(val), eventCb(callback)
    {
        tweaks[CTweakKey(name)] = this;
    }

    virtual std::string GetName() const { return name; }
    virtual std::string GetHelp() const { return help; }
    virtual UniValue Get() const
    {
        std::lock_guard<std::recursive_mutex> lck(cs_tweak);
        return UniValue(*value);
    }
    virtual std::string Validate(const UniValue &val)
    {
        std::lock_guard<std::recursive_mutex> lck(cs_tweak);
        if (eventCb)
        {
            DataType candidate;
            fill(val, candidate);
            std::string result = eventCb(candidate, value, true);
            if (!result.empty())
                return result;
        }
        return std::string();
    };

    virtual UniValue Set(const UniValue &v) // Returns NullUnivalue or an error string
    {
        std::lock_guard<std::recursive_mutex> lck(cs_tweak);
        DataType prior = *value;
        fill(v, *value);
        if (eventCb)
        {
            std::string result = eventCb(prior, value, false);
            if (!result.empty())
                return UniValue(result);
        }
        return NullUniValue;
    }

    virtual DataType Value() const
    {
        std::lock_guard<std::recursive_mutex> lck(cs_tweak);
        return *value;
    }

    CTweakRef &operator=(const DataType &d)
    {
        std::lock_guard<std::recursive_mutex> lck(cs_tweak);
        if (value)
            *value = d;
        return *this;
    }
};

/** A configuration parameter that is automatically hooked up to
 * bitcoin.conf, bitcoin-cli, and is available as a command line argument
 */
template <class DataType>
class CTweak : public CTweakBase
{
public:
    // Validation and assignment notification function.
    // If "validate" is true, then return nonempty error string if this field
    // can't be set to this value (value parameter contains the candidate
    // value).
    // If "validate" is false, this is a notification that this item has been set
    // (value parameter contains the old value).  You can return a string if you
    // want to give some kind of ACK message to the user.
    typedef std::string (*EventFn)(const DataType &value, CTweak<DataType> *item, bool validate);

protected:
    const std::string name;
    const std::string help;
    DataType value;
    EventFn eventCb;

public:
    ~CTweak()
    {
        if (name.size())
            tweaks.erase(CTweakKey(name));
    }

    CTweak(const char *namep, const char *helpp, DataType v = DataType(), EventFn callback = nullptr)
        : name(namep), help(helpp), value(v), eventCb(callback)
    {
        tweaks[CTweakKey(name)] = this;
    }

    CTweak(const std::string &namep, const std::string &helpp, DataType v = DataType(), EventFn callback = nullptr)
        : name(namep), help(helpp), value(v), eventCb(callback)
    {
        tweaks[CTweakKey(name)] = this;
    }

    virtual std::string GetName() const { return name; }
    virtual std::string GetHelp() const { return help; }
    virtual UniValue Get() const
    {
        std::lock_guard<std::recursive_mutex> lck(cs_tweak);
        return UniValue(value);
    }

    virtual UniValue Set(const UniValue &v) // Returns NullUnivalue or an error string
    {
        std::lock_guard<std::recursive_mutex> lck(cs_tweak);
        DataType prior = value;
        fill(v, value);
        if (eventCb)
        {
            std::string result = eventCb(prior, this, false);
            if (!result.empty())
                return UniValue(result);
        }
        return NullUniValue;
    }

    virtual std::string Validate(const UniValue &val)
    {
        std::lock_guard<std::recursive_mutex> lck(cs_tweak);
        if (eventCb)
        {
            DataType candidate;
            fill(val, candidate);
            std::string result = eventCb(candidate, this, true);
            if (!result.empty())
                return result;
        }
        return std::string();
    };

    virtual DataType Value() const
    {
        std::lock_guard<std::recursive_mutex> lck(cs_tweak);
        return value;
    }

    CTweak &operator=(const DataType &d)
    {
        std::lock_guard<std::recursive_mutex> lck(cs_tweak);
        value = d;
        return *this;
    }
};

void LoadTweaks();

#endif
