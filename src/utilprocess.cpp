// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilprocess.h"
#include <boost/filesystem.hpp>
#include <boost/predef/os.h>
#include <sstream>

#if BOOST_OS_LINUX
#include <signal.h>
#endif

unsupported_platform_error::unsupported_platform_error(const std::string &func_name)
    : std::runtime_error("Function '" + func_name + "' is not implemented on this platform")
{
}

std::string this_process_path()
{
// this could be implemented with boost::dll::program_location, however
// with current boost version on linux this adds a linker dependency to libdl.
#if BOOST_OS_LINUX
    // TODO: Replaced with std::read_symlink with C++17
    return boost::filesystem::read_symlink("/proc/self/exe").string();
#else
    throw unsupported_platform_error(__func__);
#endif
}
