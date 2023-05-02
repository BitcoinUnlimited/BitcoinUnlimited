// Copyright (c) 2019 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <span.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

[[nodiscard]] inline std::string TrimString(const std::string &str, const std::string &pattern = " \f\n\r\t\v") {
    std::string::size_type front = str.find_first_not_of(pattern);
    if (front == std::string::npos) {
        return std::string();
    }
    std::string::size_type end = str.find_last_not_of(pattern);
    return str.substr(front, end - front + 1);
}

/**
 * Join a list of items
 *
 * @param list       The list to join
 * @param separator  The separator
 * @param unary_op   Apply this operator to each item in the list
 */
template <typename T, typename UnaryOp>
std::string Join(const std::vector<T> &list, const std::string &separator, UnaryOp unary_op) {
    std::string ret;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) ret += separator;
        ret += unary_op(list[i]);
    }
    return ret;
}

inline std::string Join(const std::vector<std::string> &list, const std::string &separator) {
    return Join(list, separator, [](const std::string &i) { return i; });
}

/**
 * boost::split work-alike, appends tokens from `input` to `result`. Separators are identified by `separators`.
 *
 * @param result - Tokens are appended to this container. It should be a container of std::string or std::string_view.
 *                 This container will be overwritten by this function.
 * @param input - The string to split.
 * @param separators - The set of characters to consider as separators.
 * @param tokenCompress - If true, adjacent separators are merged together. Otherwise, every two separators delimit a
 *                        token.
 * @return A reference to `result`. `result` will be overwritten with the tokens.
 */
template<typename OutputSequence>
OutputSequence& Split(OutputSequence& result, std::string_view input, std::string_view separators = " \f\n\r\t\v",
                      bool tokenCompress = false) {
    // GCC 8.3.0 workaround so that e.g. OutputSequence above can be std::set<std::string>.
    struct Token {
        std::string_view s;
        Token(std::string_view::iterator begin, std::string_view::iterator end) : s(&*begin, end - begin) {}
        operator std::string() const { return std::string{s.begin(), s.end()}; }
        operator std::string_view() const noexcept { return s; }
    };
    std::vector<Token> tokens;
    auto it = input.begin();
    auto tok_begin = it;
    const auto end = input.end();
    size_t repeated_sep_ct = 0;
    while (it != end) {
        if (separators.find_first_of(*it) != separators.npos) {
            if (!tokenCompress || repeated_sep_ct++ == 0) {
                tokens.emplace_back(tok_begin, it);
            }
            tok_begin = ++it;
        } else {
            ++it;
            repeated_sep_ct = 0;
        }
    }
    // append the last token (or the entire string, even if empty, if no tokens found)
    tokens.emplace_back(tok_begin, end);

    // We use a tmp container in this way to leverage InputIterator construction, so we can operate on any container,
    // including std::set<std::string>, etc.
    OutputSequence tmp(tokens.begin(), tokens.end());
    result.swap(tmp);
    return result;
}

/**
 * boost::replace_all equivalent, finds all instances of `old` and replaces with `new` in-place.
 *
 * @param input - A reference to the string to operate on.
 * @param search - The substring to search for and replace.
 * @param format - The string to insert in place of `old`.
 */
inline void ReplaceAll(std::string &input, std::string const &search, std::string const &format) {
    if (search.empty()) return; // an empty string will always match in string::find - we don't want that
    for (size_t pos = input.find(search); pos != std::string::npos; pos = input.find(search, pos + format.length())) {
        input.replace(pos, search.length(), format);
    }
}

/**
 * Check if a string does not contain any embedded NUL (\0) characters
 */
[[nodiscard]] inline bool ValidAsCString(const std::string &str) noexcept {
    return str.find_first_of('\0') == std::string::npos;
}

/**
 * Check whether a container begins with the given prefix.
 */
template <typename T1>
[[nodiscard]] inline bool HasPrefix(const T1 &obj, const Span<const uint8_t> prefix) {
    return obj.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), obj.begin());
}
