// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilprocess.h"
#include "test/test_bitcoin.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/predef/os.h>
#include <boost/test/unit_test.hpp>
#include <iostream>

BOOST_FIXTURE_TEST_SUITE(utilprocess_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(this_process_path_test)
{
#if BOOST_OS_LINUX
    std::string path = this_process_path();
    // TODO: replace boost with std::string::ends_with in C++20
    BOOST_CHECK(boost::algorithm::ends_with(path, "/test_bitcoin"));
#else
    BOOST_CHECK_THROW(this_process_path(), unsupported_platform_error);
#endif
}

static bool bin_exists(const std::string &path) { return boost::filesystem::exists(path); }
BOOST_AUTO_TEST_CASE(subprocess_return_code)
{
#if (BOOST_OS_LINUX && (BOOST_VERSION >= 106500))
    auto dummy_callb = [](const std::string &) {};

    if (!bin_exists("/bin/true") || !bin_exists("/bin/false"))
    {
        std::cerr << "Skipping test " << __func__ << std::endl;
    }

    SubProcess p_true("/bin/true", {}, dummy_callb, dummy_callb);
    BOOST_CHECK_NO_THROW(p_true.Run());
    BOOST_CHECK(!p_true.IsRunning());

    SubProcess p_false("/bin/false", {}, dummy_callb, dummy_callb);
    try
    {
        // run throws when exit code is != 0
        p_false.Run();
        BOOST_CHECK(false); // should have thrown
    }
    catch (const subprocess_error &e)
    {
        BOOST_CHECK(e.exit_status != -1);
    }
    BOOST_CHECK(!p_false.IsRunning());
#endif
}

BOOST_AUTO_TEST_CASE(subprocess_stdout)
{
#if (BOOST_OS_LINUX && (BOOST_VERSION >= 106500))
    std::vector<std::string> callback_lines;
    auto callb = [&callback_lines](const std::string &line) { callback_lines.push_back(line); };

    if (!bin_exists("/bin/echo"))
    {
        std::cerr << "Skipping test " << __func__ << std::endl;
    }

    SubProcess p("/bin/echo", {"first line\nsecond line"}, callb, callb);
    p.Run();
    BOOST_CHECK_EQUAL(size_t(2), callback_lines.size());
    BOOST_CHECK_EQUAL("first line", callback_lines[0]);
    BOOST_CHECK_EQUAL("second line", callback_lines[1]);
#endif
}

BOOST_AUTO_TEST_CASE(subprocess_terminate)
{
#if (BOOST_OS_LINUX && (BOOST_VERSION >= 106500))
    auto dummy_callb = [](const std::string &) {};

    if (!bin_exists("/bin/sleep"))
    {
        std::cerr << "Skipping test " << __func__ << std::endl;
    }

    int termination_signal = -1;

    SubProcess p("/bin/sleep", {"30"}, dummy_callb, dummy_callb);
    std::thread t([&]() {
        try
        {
            p.Run();
        }
        catch (const subprocess_error &e)
        {
            termination_signal = e.termination_signal;
        }
    });
    while (!p.IsRunning())
    {
        std::this_thread::yield();
    }
    BOOST_CHECK(p.GetPID() != -1);
    p.Terminate();
    t.join();
    BOOST_CHECK(termination_signal != -1);
#endif
}

BOOST_AUTO_TEST_CASE(subprocess_non_existing_path)
{
#if (BOOST_OS_LINUX && (BOOST_VERSION >= 106500))
    auto dummy_callb = [](const std::string &) {};
    const std::string path = "/nonexistingpath";
    if (bin_exists(path))
    {
        std::cerr << "Skipping test " << __func__ << std::endl;
    }
    SubProcess p(path, {}, dummy_callb, dummy_callb);
    BOOST_CHECK_THROW(p.Run(), subprocess_error);
#endif
}

BOOST_AUTO_TEST_SUITE_END()
