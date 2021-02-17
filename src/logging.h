// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * logging, thread wrappers, startup time
 */
#ifndef BITCOIN_LOGGING_H
#define BITCOIN_LOGGING_H

#include "fs.h"
#include "tinyformat.h"

#include <stdint.h>
#include <string>

static const bool DEFAULT_LOGTIMEMICROS = false;
static const bool DEFAULT_LOGIPS = true;
static const bool DEFAULT_LOGTIMESTAMPS = true;

/** Send a string to the log output */
int LogPrintStr(const std::string &str);

// Logging API:
// Use the two macros
// LOG(ctgr,...)
// LOGA(...)
// located further down.
// (Do not use the Logging functions directly)
// Log Categories:
// 64 Bits: (Define unique bits, not 'normal' numbers)
enum
{
    // Turn off clang formatting so we can keep the assignment alinged for readability
    // clang-format off
    NONE           = 0x0, // No logging
    ALL            = 0xFFFFFFFFFFFFFFFFUL, // Log everything

    // LOG Categories:
    THIN           = 0x1,
    MEMPOOL        = 0x2,
    COINDB         = 0x4,
    TOR            = 0x8,

    NET            = 0x10,
    ADDRMAN        = 0x20,
    LIBEVENT       = 0x40,
    HTTP           = 0x80,

    RPC            = 0x100,
    PARTITIONCHECK = 0x200,
    BENCH          = 0x400,
    PRUNE          = 0x800,

    REINDEX        = 0x1000,
    MEMPOOLREJ     = 0x2000,
    BLK            = 0x4000,
    EVICT          = 0x8000,

    PARALLEL       = 0x10000,
    RAND           = 0x20000,
    REQ            = 0x40000,
    BLOOM          = 0x80000,

    ESTIMATEFEE    = 0x100000,
    LCK            = 0x200000,
    PROXY          = 0x400000,
    DBASE          = 0x800000,

    SELECTCOINS    = 0x1000000,
    ZMQ            = 0x2000000,
    QT             = 0x4000000,
    IBD            = 0x8000000,

    GRAPHENE       = 0x10000000,
    RESPEND        = 0x20000000,
    WB             = 0x40000000, // weak blocks
    CMPCT          = 0x80000000, // compact blocks

    ELECTRUM       = 0x100000000,
    MPOOLSYNC      = 0x200000000,
    PRIORITYQ      = 0x400000000,
    DSPROOF        = 0x800000000,

    TWEAKS         = 0x1000000000
    // clang-format on
};

namespace Logging
{
extern uint64_t categoriesEnabled;

/*
To add a new log category:
1) Create a unique 1 bit category mask. (Easiest is to 2* the last enum entry.)
   Put it at the end of enum above.
2) Add an category/string pair to LOGLABELMAP macro below.
*/

// Add corresponding lower case string for the category:
#define LOGLABELMAP                                                                                             \
    {                                                                                                           \
        {NONE, "none"}, {ALL, "all"}, {THIN, "thin"}, {MEMPOOL, "mempool"}, {COINDB, "coindb"}, {TOR, "tor"},   \
            {NET, "net"}, {ADDRMAN, "addrman"}, {LIBEVENT, "libevent"}, {HTTP, "http"}, {RPC, "rpc"},           \
            {PARTITIONCHECK, "partitioncheck"}, {BENCH, "bench"}, {PRUNE, "prune"}, {REINDEX, "reindex"},       \
            {MEMPOOLREJ, "mempoolrej"}, {BLK, "blk"}, {EVICT, "evict"}, {PARALLEL, "parallel"}, {RAND, "rand"}, \
            {REQ, "req"}, {BLOOM, "bloom"}, {LCK, "lck"}, {PROXY, "proxy"}, {DBASE, "dbase"},                   \
            {SELECTCOINS, "selectcoins"}, {ESTIMATEFEE, "estimatefee"}, {QT, "qt"}, {IBD, "ibd"},               \
            {GRAPHENE, "graphene"}, {RESPEND, "respend"}, {WB, "weakblocks"}, {CMPCT, "cmpctblock"},            \
            {ELECTRUM, "electrum"}, {MPOOLSYNC, "mempoolsync"}, {PRIORITYQ, "priorityq"}, {DSPROOF, "dsproof"}, \
            {TWEAKS, "tweaks"},                                                                                 \
        {                                                                                                       \
            ZMQ, "zmq"                                                                                          \
        }                                                                                                       \
    }

/**
 * Check if a category should be logged
 * @param[in] category
 * returns true if should be logged
 */
inline bool LogAcceptCategory(uint64_t category) { return (categoriesEnabled & category); }
/**
 * Turn on/off logging for a category
 * @param[in] category
 * @param[in] on  True turn on, False turn off.
 */
inline void LogToggleCategory(uint64_t category, bool on)
{
    if (on)
        categoriesEnabled |= category;
    else
        categoriesEnabled &= ~category; // off
}

/**
* Get a category associated with a string.
* @param[in] label string
* returns category
*/
uint64_t LogFindCategory(const std::string label);

/**
 * Get the label / associated string for a category.
 * @param[in] category
 * returns label
 */
std::string LogGetLabel(uint64_t category);

/**
 * Get all categories and their state.
 * Formatted for display.
 * returns all categories and states
 */
std::string LogGetAllString(bool fEnabled = false);

/**
 * Initialize
 */
void LogInit();

/**
 * Write log string to console:
 *
 * @param[in] All parameters are "printf like".
 */
template <typename T1, typename... Args>
inline void LogStdout(const char *fmt, const T1 &v1, const Args &... args)
{
    try
    {
        std::string str = tfm::format(fmt, v1, args...);
        ::fwrite(str.data(), 1, str.size(), stdout);
    }
    catch (...)
    {
        // Number of format specifiers (%) do not match argument count, etc
    };
}

/**
 * Write log string to console:
 * @param[in] str String to log.
 */
inline void LogStdout(const std::string &str)
{
    ::fwrite(str.data(), 1, str.size(), stdout); // No formatting for a simple string
}

/**
 * Log a string
 * @param[in] All parameters are "printf like args".
 */
template <typename T1, typename... Args>
inline void LogWrite(const char *fmt, const T1 &v1, const Args &... args)
{
    try
    {
        LogPrintStr(tfm::format(fmt, v1, args...));
    }
    catch (...)
    {
        // Number of format specifiers (%) do not match argument count, etc
    };
}

/**
 * Log a string
 * @param[in] str String to log.
 */
inline void LogWrite(const std::string &str)
{
    LogPrintStr(str); // No formatting for a simple string
}
}

// Logging API:
//
/**
 * LOG macro: Log a string if a category is enabled.
 * Note that categories can be ORed, such as: (NET|TOR)
 *
 * @param[in] category -Which category to log
 * @param[in] ... "printf like args".
 */
#define LOG(ctgr, ...)                        \
    do                                        \
    {                                         \
        using namespace Logging;              \
        if (Logging::LogAcceptCategory(ctgr)) \
            Logging::LogWrite(__VA_ARGS__);   \
    } while (0)

/**
 * LOGA macro: Always log a string.
 *
 * @param[in] ... "printf like args".
 */
#define LOGA(...) Logging::LogWrite(__VA_ARGS__)
//

// Flush log file (if you know you are about to abort)
void LogFlush();

/** Get format string from VA_ARGS for error reporting */
template <typename... Args>
std::string FormatStringFromLogArgs(const char *fmt, const Args &... args)
{
    return fmt;
}


template <typename... Args>
bool error(const char *fmt, const Args &... args)
{
    LogPrintStr("ERROR: " + tfm::format(fmt, args...) + "\n");
    return false;
}


template <typename... Args>
inline bool error(uint64_t ctgr, const char *fmt, const Args &... args)
{
    if (Logging::LogAcceptCategory(ctgr))
        LogPrintStr("ERROR: " + tfm::format(fmt, args...) + "\n");
    return false;
}

#endif // BITCOIN_LOGGING_H
