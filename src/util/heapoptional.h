// Copyright (c) 2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <algorithm>
#include <memory>
#include <type_traits>
#include <utility>
#include <functional>

/// An optional that stores its value on the heap, rather than in-line, in
/// order to save memory. This is implemented using a std::unique_ptr. This
/// optional is intended to be a drop-in replacement for a std::optional
/// but with the in-line cost of a unique_ptr. Like an optional but unlike a
/// unique_ptr, it can be treated as a value type. It is copy-constructible
/// and copy-assignable (does a deep copy). It also can be
/// copy/move-constructed or copy/move-assigned from a T.
///
/// Intended to be used for "heavy" optional data members that are null in the
/// common case, but take non-trivial amounts of memory when they are not
/// null. In this way, the common-case ends up using less memory than a normal
/// in-lined optional would.
template <typename T>
class HeapOptional {
    std::unique_ptr<T> p{};
public:
    using element_type = T;

    constexpr HeapOptional() noexcept = default;
    explicit HeapOptional(const T & t) { *this = t; }
    explicit HeapOptional(T && t) noexcept { *this = t; }

    /// Construct the HeapOptional in-place using argument forwarding
    template <typename ...Args>
    explicit HeapOptional(Args && ...args) { emplace(std::forward<Args>(args)...); }

    HeapOptional(const HeapOptional & o) { *this = o; }
    HeapOptional(HeapOptional && o) = default;

    /// Create the new object in-place. Deletes previous object (if any) first.
    template <typename ...Args>
    void emplace(Args && ...args) { p = std::make_unique<T>(std::forward<Args>(args)...); }

    HeapOptional & operator=(const HeapOptional & o) {
        if (o.p) p = std::make_unique<T>(*o.p);
        else p.reset();
        return *this;
    }
    HeapOptional & operator=(HeapOptional && o) noexcept = default;

    HeapOptional & operator=(const T & t) { p = std::make_unique<T>(t); return *this; }
    HeapOptional & operator=(T && t) { p = std::make_unique<T>(std::move(t)); return *this; }

    operator bool() const { return static_cast<bool>(p); }
    T & operator*() { return *p; }
    const T & operator*() const { return *p; }
    T * get() { return p.get(); }
    const T * get() const { return p.get(); }
    T * operator->() { return p.operator->(); }
    const T * operator->() const { return p.operator->(); }

    void reset(T * t = nullptr) { p.reset(t); }
    T * release() { return p.release(); }


    //--- Comparison operators: ==, !=, <, does deep compare of pointed-to T values
    //    (only SFINAE-enabled if underlying type T supports these ops)

    auto operator==(const HeapOptional & o) const -> decltype(std::declval<std::equal_to<T>>()(std::declval<T>(),
                                                                                              std::declval<T>())) {
        if (p && o.p) return std::equal_to{}(*p, *o.p); // compare by pointed-to value if both are not null
        return std::equal_to{}(p, o.p); // compare the unique_ptr's if either are null
    }
    // compare to a value directly
    auto operator==(const T & t) const -> decltype(std::declval<std::equal_to<T>>()(std::declval<T>(),
                                                                                    std::declval<T>())) {
        if (!p) return false; // we never compare equal to a real value if we are null
        return std::equal_to{}(*p, t); // compare by pointed-to value if we not null
    }

    auto operator!=(const HeapOptional & o) const -> decltype(std::declval<std::not_equal_to<T>>()(std::declval<T>(),
                                                                                                  std::declval<T>())) {
        if (p && o.p) return std::not_equal_to{}(*p, *o.p); // compare by pointed-to value if both are not null
        return std::not_equal_to{}(p, o.p); // compare the unique_ptr's if either are null
    }
    // compare to a value directly
    auto operator!=(const T & t) const -> decltype(std::declval<std::not_equal_to<T>>()(std::declval<T>(),
                                                                                        std::declval<T>())) {
        if (!p) return true; // we are not equal to t if we are nullptr
        return std::not_equal_to{}(*p, t); // compare by pointed-to value
    }

    auto operator<(const HeapOptional & o) const -> decltype(std::declval<std::less<T>>()(std::declval<T>(),
                                                                                         std::declval<T>())) {
        if (p && o.p) return std::less{}(*p, *o.p); // compare by pointed-to value if both are not null
        return std::less{}(p, o.p); // compare the unique_ptr's if either are null
    }
    // compare to a value directly
    auto operator<(const T & t) const -> decltype(std::declval<std::less<T>>()(std::declval<T>(), std::declval<T>())) {
        if (!p) return true; // if we are null we are always less
        return std::less{}(*p, t); // compare to the pointed-to value
    }
};
