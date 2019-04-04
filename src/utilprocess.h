// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef UTILPROCESS_H
#define UTILPROCESS_H

#include <stdexcept>
#include <string>

//! thrown when function is not (yet) implemented for platfrom
class unsupported_platform_error : public std::runtime_error
{
public:
    unsupported_platform_error(const std::string &func_name);
};

//! full path for running process
std::string this_process_path();

#endif
