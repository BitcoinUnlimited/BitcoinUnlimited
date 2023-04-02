// Copyright (c) 2021 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <utility>

/// Leverage RAII to run a functor at scope end
template <typename Func>
struct Defer {
    Func func;
    Defer(Func && f) : func(std::move(f)) {}
    ~Defer() { func(); }
};
