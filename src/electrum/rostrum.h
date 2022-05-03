// Copyright (c) 2019-2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ELECTRUM_ROSTRUM_H
#define ELECTRUM_ROSTRUM_H

#include <string>
#include <vector>
#include <map>

class CExtversionMessage;

/// Electrs specific code goes in here. Separating generic electrum code allows
/// us to support multiple, or swap electrum implementation in the future.

namespace electrum {

std::string rostrum_path();
std::vector<std::string> rostrum_args(int rpcport, const std::string& network);

std::map<std::string, int64_t> fetch_rostrum_info();

void set_extversion_flags(CExtversionMessage&, const std::string& network);

} // ns electrum

#endif
