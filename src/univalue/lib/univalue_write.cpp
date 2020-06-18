// Copyright 2014 BitPay Inc.
// Copyright (c) 2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstring>
#include <stdio.h>
#include "univalue.h"
#include "univalue_escapes.h"

// Opaque type used for writing. This can be further optimized later.
struct UniValue::Stream {
    std::string & str; // this is a reference for RVO to always work in UniValue::write() below
    void put(char c) { str.push_back(c); }
    void put(char c, size_t nFill) { str.append(nFill, c); }
    void write(const char *s, size_t len) { str.append(s, len); }
    Stream & operator<<(const char *s) { str.append(s); return *this; }
    Stream & operator<<(const std::string &s) { str.append(s); return *this; }
};

/* static */
void UniValue::jsonEscape(Stream & ss, const std::string & inS)
{
    for (const auto ch : inS) {
        const char * const escStr = escapes[uint8_t(ch)];

        if (escStr)
            ss << escStr;
        else
            ss.put(ch);
    }
}

std::string UniValue::write(unsigned int prettyIndent, unsigned int indentLevel) const
{
    std::string s; // we do it this way for RVO to work on all compilers
    Stream ss{s};
    s.reserve(1024);
    writeStream(ss, prettyIndent, indentLevel);
    return s;
}

void UniValue::writeStream(Stream & ss, unsigned int prettyIndent, unsigned int indentLevel) const
{
    unsigned int modIndent = indentLevel;
    if (modIndent == 0)
        modIndent = 1;

    switch (typ) {
    case VNULL:
        ss.write("null", 4); // .write() is slightly faster than operator<<
        break;
    case VOBJ:
        writeObject(ss, prettyIndent, modIndent);
        break;
    case VARR:
        writeArray(ss, prettyIndent, modIndent);
        break;
    case VSTR:
        ss.put('"'); jsonEscape(ss, val); ss.put('"');
        break;
    case VNUM:
        ss << val;
        break;
    case VBOOL:
        if (val == boolTrueVal)
            ss.write("true", 4);
        else
            ss.write("false", 5);
        break;
    }
}

/* static */
inline void UniValue::indentStr(Stream & ss, unsigned int prettyIndent, unsigned int indentLevel)
{
    ss.put(' ', prettyIndent * indentLevel);
}

void UniValue::writeArray(Stream & ss, unsigned int prettyIndent, unsigned int indentLevel) const
{
    ss.put('[');
    if (prettyIndent)
        ss.put('\n');

    for (size_t i = 0, nValues = values.size(); i < nValues; ++i) {
        if (prettyIndent)
            indentStr(ss, prettyIndent, indentLevel);
        values[i].writeStream(ss, prettyIndent, indentLevel + 1);
        if (i != (nValues - 1)) {
            ss.put(',');
        }
        if (prettyIndent)
            ss.put('\n');
    }

    if (prettyIndent)
        indentStr(ss, prettyIndent, indentLevel - 1);
    ss.put(']');
}

void UniValue::writeObject(Stream & ss, unsigned int prettyIndent, unsigned int indentLevel) const
{
    ss.put('{');
    if (prettyIndent)
        ss.put('\n');

    for (size_t i = 0, nEntries = entries.size(); i < nEntries; ++i) {
        if (prettyIndent)
            indentStr(ss, prettyIndent, indentLevel);
        auto& entry = entries[i];
        ss.put('"'); jsonEscape(ss, entry.first); ss.write("\":", 2);
        if (prettyIndent)
            ss.put(' ');
        entry.second.writeStream(ss, prettyIndent, indentLevel + 1);
        if (i != (nEntries - 1))
            ss.put(',');
        if (prettyIndent)
            ss.put('\n');
    }

    if (prettyIndent)
        indentStr(ss, prettyIndent, indentLevel - 1);
    ss.put('}');
}
