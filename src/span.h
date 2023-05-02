// Copyright (c) 2018 The Bitcoin Core developers
// Copyright (c) 2020-2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#ifdef DEBUG
#define CONSTEXPR_IF_NOT_DEBUG
#define ASSERT_IF_DEBUG(x) assert((x))
#else
#define CONSTEXPR_IF_NOT_DEBUG constexpr
#define ASSERT_IF_DEBUG(x)
#endif

/** A Span is an object that can refer to a contiguous sequence of objects.
 *
 * It implements a subset of C++20's std::span.
 *
 * Things to be aware of when writing code that deals with Spans:
 *
 * - Similar to references themselves, Spans are subject to reference lifetime
 *   issues. The user is responsible for making sure the objects pointed to by
 *   a Span live as long as the Span is used. For example:
 *
 *       std::vector<int> vec{1,2,3,4};
 *       Span<int> sp(vec);
 *       vec.push_back(5);
 *       printf("%i\n", sp.front()); // UB!
 *
 *   may exhibit undefined behavior, as increasing the size of a vector may
 *   invalidate references.
 *
 * - One particular pitfall is that Spans can be constructed from temporaries,
 *   but this is unsafe when the Span is stored in a variable, outliving the
 *   temporary. For example, this will compile, but exhibits undefined behavior:
 *
 *       Span<const int> sp(std::vector<int>{1, 2, 3});
 *       printf("%i\n", sp.front()); // UB!
 *
 *   The lifetime of the vector ends when the statement it is created in ends.
 *   Thus the Span is left with a dangling reference, and using it is undefined.
 *
 * - Due to Span's automatic creation from range-like objects (arrays, and data
 *   types that expose a data() and size() member function), functions that
 *   accept a Span as input parameter can be called with any compatible
 *   range-like object. For example, this works:
 *
 *       void Foo(Span<const int> arg);
 *
 *       Foo(std::vector<int>{1, 2, 3}); // Works
 *
 *   This is very useful in cases where a function truly does not care about the
 *   container, and only about having exactly a range of elements. However it
 *   may also be surprising to see automatic conversions in this case.
 *
 *   When a function accepts a Span with a mutable element type, it will not
 *   accept temporaries; only variables or other references. For example:
 *
 *       void FooMut(Span<int> arg);
 *
 *       FooMut(std::vector<int>{1, 2, 3}); // Does not compile
 *       std::vector<int> baz{1, 2, 3};
 *       FooMut(baz); // Works
 *
 *   This is similar to how functions that take (non-const) lvalue references
 *   as input cannot accept temporaries. This does not work either:
 *
 *       void FooVec(std::vector<int>& arg);
 *       FooVec(std::vector<int>{1, 2, 3}); // Does not compile
 *
 *   The idea is that if a function accepts a mutable reference, a meaningful
 *   result will be present in that variable after the call. Passing a temporary
 *   is useless in that context.
 */
template <typename C> class Span {
    C *m_data{};
    std::size_t m_size{};

public:
    constexpr Span() noexcept = default;

    /** Construct a span from a begin pointer and a size.
     *
     * This implements a subset of the iterator-based std::span constructor in C++20,
     * which is hard to implement without std::address_of.
     */
    template <typename T, typename std::enable_if_t<std::is_convertible_v<T (*)[], C (*)[]>, int> = 0>
    constexpr Span(T *begin, std::size_t size) noexcept : m_data(begin), m_size(size) {}

    /** Construct a span from a begin and end pointer.
     *
     * This implements a subset of the iterator-based std::span constructor in C++20,
     * which is hard to implement without std::address_of.
     */
    template <typename T, typename std::enable_if_t<std::is_convertible_v<T (*)[], C (*)[]>, int> = 0>
    CONSTEXPR_IF_NOT_DEBUG Span(T *begin, T *end) noexcept : m_data(begin), m_size(end - begin) {
        ASSERT_IF_DEBUG(end >= begin);
    }

    /** Implicit conversion of spans between compatible types.
     *
     *  Specifically, if a pointer to an array of type O can be implicitly converted to a pointer to an array of type
     *  C, then permit implicit conversion of Span<O> to Span<C>. This matches the behavior of the corresponding
     *  C++20 std::span constructor.
     *
     *  For example this means that a Span<T> can be converted into a Span<const T>.
     */
    template <typename O, typename std::enable_if_t<std::is_convertible_v<O (*)[], C (*)[]>, int> = 0>
    constexpr Span(const Span<O>& other) noexcept : m_data(other.m_data), m_size(other.m_size) {}

    /** Default copy constructor. */
    constexpr Span(const Span&) noexcept = default;

    /** Construct a Span from an array. This matches the corresponding C++20 std::span constructor. */
    template <std::size_t N>
    constexpr Span(C (&a)[N]) noexcept : m_data(a), m_size(N) {}

    /** Construct a Span for objects with .data() and .size() (std::string, std::array, std::vector, ...).
     *
     * This implements a subset of the functionality provided by the C++20 std::span range-based constructor.
     *
     * To prevent surprises, only Spans for constant value types are supported when passing in temporaries.
     * Note that this restriction does not exist when converting arrays or other Spans (see above).
     */
    template <typename V,
              typename std::enable_if_t<
                  (std::is_const_v<C> || std::is_lvalue_reference_v<V>) && std::is_convertible_v<
                      typename std::remove_pointer_t<decltype(std::declval<V &>().data())> (*)[], C (*)[]> &&
                      std::is_convertible_v<decltype(std::declval<V &>().size()), std::size_t>,
                  int> = 0>
    constexpr Span(V &&v) noexcept : m_data(v.data()), m_size(v.size()) {}

    /** Default assignment operator. */
    Span& operator=(const Span& other) noexcept = default;

    constexpr C *data() const noexcept { return m_data; }
    constexpr C *begin() const noexcept { return m_data; }
    constexpr C *end() const noexcept { return m_data + m_size; }
    CONSTEXPR_IF_NOT_DEBUG C &front() const noexcept {
        ASSERT_IF_DEBUG(size() > 0);
        return *begin();
    }
    CONSTEXPR_IF_NOT_DEBUG C &back() const noexcept {
        ASSERT_IF_DEBUG(size() > 0);
        return *(end() - 1);
    }
    constexpr std::size_t size() const noexcept { return m_size; }
    constexpr bool empty() const noexcept { return size() == 0; }
    CONSTEXPR_IF_NOT_DEBUG C &operator[](std::size_t pos) const noexcept {
        ASSERT_IF_DEBUG(size() > pos);
        return m_data[pos];
    }
    CONSTEXPR_IF_NOT_DEBUG Span<C> subspan(std::size_t offset) const noexcept {
        ASSERT_IF_DEBUG(size() >= offset);
        return offset <= m_size? Span<C>(m_data + offset, m_size - offset) : Span<C>(end(), std::size_t{0});
    }
    CONSTEXPR_IF_NOT_DEBUG Span<C> subspan(std::size_t offset, std::size_t count) const noexcept {
        ASSERT_IF_DEBUG(size() >= offset + count);
        return offset + count <= m_size ? Span<C>(m_data + offset, count) : Span<C>(end(), std::size_t{0});
    }
    CONSTEXPR_IF_NOT_DEBUG Span<C> first(std::size_t count) const noexcept {
        ASSERT_IF_DEBUG(size() >= count);
        return count <= m_size ? Span<C>(m_data, count) : Span<C>(begin(), std::size_t{0});
    }
    CONSTEXPR_IF_NOT_DEBUG Span<C> last(std::size_t count) const noexcept {
        ASSERT_IF_DEBUG(size() >= count);
        return count <= m_size ? Span<C>(m_data + m_size - count, count) : Span<C>(end(), std::size_t{0});
    }

    /** Pop the last element off, and return a reference to that element.
        Span must not be empty(); span will decrease in size by 1, having its end() moved back by 1. */
    constexpr C & pop_back() noexcept {
        assert(!empty());
        return m_data[--m_size];
    }

    /** Pop the first element off, and return a reference to that element.
        Span must not be empty(); span will decrease in size by 1, having its begin() moved up by 1. */
    constexpr C & pop_front() noexcept {
        assert(!empty());
        --m_size;
        return *m_data++;
    }

    friend constexpr bool operator==(const Span &a, const Span &b) noexcept {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    }
    friend constexpr bool operator!=(const Span &a, const Span &b) noexcept {
        return !(a == b);
    }
    friend constexpr bool operator<(const Span &a, const Span &b) noexcept {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
    }
    friend constexpr bool operator<=(const Span &a, const Span &b) noexcept {
        return !(b < a);
    }
    friend constexpr bool operator>(const Span &a, const Span &b) noexcept {
        return b < a;
    }
    friend constexpr bool operator>=(const Span &a, const Span &b) noexcept {
        return !(a < b);
    }

    /** Ensures the convertible-to constructor works */
    template <typename O> friend class Span;

    /** value_type is used for generic code compatibility */
    using value_type = C;
};

// Deduction guides for Span
// For the pointer/size based and iterator based constructor:
template <typename T, typename EndOrSize> Span(T*, EndOrSize) -> Span<T>;
// For the array constructor:
template <typename T, std::size_t N> Span(T (&)[N]) -> Span<T>;
// For the temporaries/rvalue references constructor, only supporting const output.
template <typename T> Span(T&&) -> Span<std::enable_if_t<!std::is_lvalue_reference_v<T>, const std::remove_pointer_t<decltype(std::declval<T&&>().data())>>>;
// For (lvalue) references, supporting mutable output.
template <typename T> Span(T&) -> Span<std::remove_pointer_t<decltype(std::declval<T&>().data())>>;

// Helper functions to safely cast to uint8_t pointers.
inline uint8_t *UInt8Cast(char *c) {
    return reinterpret_cast<uint8_t *>(c);
}
inline uint8_t *UInt8Cast(uint8_t *c) {
    return c;
}
inline const uint8_t *UInt8Cast(const char *c) {
    return reinterpret_cast<const uint8_t *>(c);
}
inline const uint8_t *UInt8Cast(const uint8_t *c) {
    return c;
}

// Helper function to safely convert a Span to a Span<[const] uint8_t>.
template <typename T>
constexpr auto UInt8SpanCast(Span<T> s) -> Span<typename std::remove_pointer_t<decltype(UInt8Cast(s.data()))>> {
    return {UInt8Cast(s.data()), s.size()};
}

/** Like the Span constructor, but for (const) uint8_t member types only. Only works for (un)signed char containers. */
template <typename V>
constexpr auto MakeUInt8Span(V&& v) -> decltype(UInt8SpanCast(Span{std::forward<V>(v)})) {
    return UInt8SpanCast(Span{std::forward<V>(v)});
}
