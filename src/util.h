// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * logging, thread wrappers
 */
#ifndef BITCOIN_UTIL_H
#define BITCOIN_UTIL_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "allowed_args.h"
#include "compat.h"
#include "fs.h"
#include "tinyformat.h"
#include "utiltime.h"

#include <exception>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

#include <boost/signals2/signal.hpp>
#include <boost/thread/exceptions.hpp>

#ifdef DEBUG
#define DEBUG_ASSERTION
#define DEBUG_PAUSE
#endif

#ifdef DEBUG_ASSERTION
/// If DEBUG_ASSERTION is enabled this asserts when the predicate is false.
//  If DEBUG_ASSERTION is disabled and the predicate is false, it executes the execInRelease statements.
//  Typically, the programmer will error out -- return false, raise an exception, etc in the execInRelease code.
//  DO NOT USE break or continue inside the DbgAssert!
#define DbgAssert(pred, execInRelease) assert(pred)
#else
#define DbgStringify(x) #x
#define DbgStringifyIntLiteral(x) DbgStringify(x)
#define DbgAssert(pred, execInRelease)                                                                        \
    do                                                                                                        \
    {                                                                                                         \
        if (!(pred))                                                                                          \
        {                                                                                                     \
            LogPrintStr(std::string(                                                                          \
                __FILE__ "(" DbgStringifyIntLiteral(__LINE__) "): Debug Assertion failed: \"" #pred "\"\n")); \
            execInRelease;                                                                                    \
        }                                                                                                     \
    } while (0)
#endif

#ifdef DEBUG_PAUSE
// Stops this thread by taking a semaphore
// This should not be called as part of a release so during the non --enable-debug build
// you will get an undefined symbol compilation error.
void DbgPause();
// Continue the thread.  Intended to be called manually from gdb
extern "C" void DbgResume();
#endif

#define UNIQUE2(pfx, LINE) pfx##LINE
#define UNIQUE1(pfx, LINE) UNIQUE2(pfx, LINE)
/// UNIQUIFY is a macro that appends the current file's line number to the passed prefix, creating a symbol
// that is unique in this file.
#define UNIQUIFY(pfx) UNIQUE1(pfx, __LINE__)

static const bool DEFAULT_LOGTIMEMICROS = false;
static const bool DEFAULT_LOGIPS = true;
static const bool DEFAULT_LOGTIMESTAMPS = true;

// For bitcoin-cli
extern const char DEFAULT_RPCCONNECT[];
static const int DEFAULT_HTTP_CLIENT_TIMEOUT = 900;

/** Signals for translation. */
class CTranslationInterface
{
public:
    /** Translate a message to the native language of the user. */
    boost::signals2::signal<std::string(const char *psz)> Translate;
};

extern std::map<std::string, std::string> mapArgs;
extern std::map<std::string, std::vector<std::string> > mapMultiArgs;
extern bool fDebug;
extern bool fPrintToConsole;
extern bool fPrintToDebugLog;
extern bool fServer;
extern std::string strMiscWarning;
extern bool fLogTimestamps;
extern bool fLogTimeMicros;
extern bool fLogIPs;
extern volatile bool fReopenDebugLog;
extern CTranslationInterface translationInterface;

extern const char *const BITCOIN_CONF_FILENAME;
extern const char *const BITCOIN_PID_FILENAME;
extern const char *const FORKS_CSV_FILENAME; // bip135 added

/** Send a string to the log output */
int LogPrintStr(const std::string &str);

// Takes a std::vector of strings and splits individual arguments further up if
// they contain commas. Also removes space from the output strings.
// For example, ["a", "b,c", "d"] becomes ["a", "b", "c", "d"]
extern std::vector<std::string> splitByCommasAndRemoveSpaces(const std::vector<std::string> &args,
    bool removeDuplicates = false);

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
    NONE = 0x0, // No logging
    ALL = 0xFFFFFFFFFFFFFFFFUL, // Log everything

    // LOG Categories:
    THIN = 0x1,
    MEMPOOL = 0x2,
    COINDB = 0x4,
    TOR = 0x8,

    NET = 0x10,
    ADDRMAN = 0x20,
    LIBEVENT = 0x40,
    HTTP = 0x80,

    RPC = 0x100,
    PARTITIONCHECK = 0x200,
    BENCH = 0x400,
    PRUNE = 0x800,

    REINDEX = 0x1000,
    MEMPOOLREJ = 0x2000,
    BLK = 0x4000,
    EVICT = 0x8000,

    PARALLEL = 0x10000,
    RAND = 0x20000,
    REQ = 0x40000,
    BLOOM = 0x80000,

    ESTIMATEFEE = 0x100000,
    LCK = 0x200000,
    PROXY = 0x400000,
    DBASE = 0x800000,

    SELECTCOINS = 0x1000000,
    ZMQ = 0x2000000,
    QT = 0x4000000,
    IBD = 0x8000000,

    GRAPHENE = 0x10000000,
    RESPEND = 0x20000000,
    WB = 0x40000000, // weak blocks
    CMPCT = 0x80000000 // compact blocks
};

namespace Logging
{
extern uint64_t categoriesEnabled;

/*
To add a new log category:
1) Create a unique 1 bit category mask. (Easiest is to 2* the last enum entry.)
   Put it at the end of enum below.
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

// Log tests:
UniValue setlog(const UniValue &params, bool fHelp);
// END logging.


/**
 * Translate a boolean string to a bool.
 * Throws an exception if not one of the strings.
 * Is case insensitive.
 * @param[in] one of "enable|disable|1|0|true|false|on|off"
 * returns true if enabled, false if not.
 */
bool IsStringTrue(const std::string &str);

/**
 * Translation function: Call Translate signal on UI interface, which returns a boost::optional result.
 * If no translation slot is registered, nothing is returned, and simply return the input.
 */
inline std::string _(const char *psz)
{
    boost::optional<std::string> rv = translationInterface.Translate(psz);
    return rv ? (*rv) : psz;
}

void SetupEnvironment();
bool SetupNetworking();


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


/**
 Format an amount of bytes with a unit symbol attached, such as MB, KB, GB.
 Uses Kilobytes x1000, not Kibibytes x1024.

 Output value has two digits after the dot. No space between unit symbol and
 amount.

 Also works for negative amounts. The maximum unit supported is 1 Exabyte (EB).
 This formatting is used by the thinblock statistics functions, and this
 is a factored-out utility function.

 @param [value] The value to format
 @return String with unit
 */
extern std::string formatInfoUnit(double value);

void PrintExceptionContinue(const std::exception *pex, const char *pszThread);
void ParseParameters(int argc, const char *const argv[], const AllowedArgs::AllowedArgs &allowedArgs);
void FileCommit(FILE *fileout);
bool TruncateFile(FILE *file, unsigned int length);
int RaiseFileDescriptorLimit(int nMinFD);
void AllocateFileRange(FILE *file, unsigned int offset, unsigned int length);
bool RenameOver(fs::path src, fs::path dest);
bool TryCreateDirectories(const fs::path &p);
fs::path GetDefaultDataDir();
const fs::path &GetDataDir(bool fNetSpecific = true);
void ClearDatadirCache();
fs::path GetConfigFile(const std::string &confPath);
fs::path GetForksCsvFile(); // bip135 added
#ifndef WIN32
fs::path GetPidFile();
void CreatePidFile(const fs::path &path, pid_t pid);
#endif
void ReadConfigFile(std::map<std::string, std::string> &mapSettingsRet,
    std::map<std::string, std::vector<std::string> > &mapMultiSettingsRet,
    const AllowedArgs::AllowedArgs &allowedArgs);
#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif
void OpenDebugLog();
void ShrinkDebugFile();
void runCommand(const std::string &strCommand);

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

/**
 * Return string argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. "1")
 * @return command-line argument or default value
 */
std::string GetArg(const std::string &strArg, const std::string &strDefault);

/**
 * Return integer argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. 1)
 * @return command-line argument (0 if invalid number) or default value
 */
int64_t GetArg(const std::string &strArg, int64_t nDefault);

/**
 * Return boolean argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (true or false)
 * @return command-line argument or default value
 */
bool GetBoolArg(const std::string &strArg, bool fDefault);

/**
 * Set an argument
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param strValue Value (e.g. "1")
 * @return none
 */
void SetArg(const std::string &strArg, const std::string &strValue);

/**
 * Set a boolean argument
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param fValue Value (e.g. false)
 * @return none
 */
void SetBoolArg(const std::string &strArg, bool fValue);

/**
 * Set an argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param strValue Value (e.g. "1")
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetArg(const std::string &strArg, const std::string &strValue);

/**
 * Set a boolean argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param fValue Value (e.g. false)
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetBoolArg(const std::string &strArg, bool fValue);

/**
 * Return the number of cores available on the current system.
 * @note This does count virtual cores, such as those provided by HyperThreading.
 */
int GetNumCores();

void SetThreadPriority(int nPriority);
void RenameThread(const char *name);

/**
 * .. and a wrapper that just calls func once
 */
template <typename Callable>
void TraceThreads(const std::string &name, Callable func)
{
    RenameThread(name.c_str());
    try
    {
        LOGA("%s thread start\n", name);
        func();
        LOGA("%s thread exit\n", name);
    }
    catch (const boost::thread_interrupted &)
    {
        LOGA("%s thread interrupt\n", name);
        throw;
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, name.c_str());
        LogFlush();
        throw;
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, name.c_str());
        LogFlush();
        throw;
    }
}

template <typename Callable>
void TraceThread(const char *name, Callable func)
{
    TraceThreads(std::string(name), func);
}


std::string CopyrightHolders(const std::string &strPrefix);

/** Wildcard matching of strings
The first argument (the pattern) might contain '?' and '*' wildcards and
the second argument will be matched to this pattern. Returns true iff the string
matches pattern. */
bool wildmatch(std::string pattern, std::string test);

/**
 * On platforms that support it, tell the kernel the calling thread is
 * CPU-intensive and non-interactive. See SCHED_BATCH in sched(7) for details.
 *
 * @return The return value of sched_setschedule(), or 1 on systems without
 * sched_setchedule().
 */
int ScheduleBatchPriority(void);


//! short hand for declaring pure function
#define PURE_FUNCTION __attribute__((pure))

/** Function for converting enums and integers represented as OR-ed bitmasks into
    human-readable string representations.

    For an (up to 64-bit) unsigned integer and a map of bit values to
    strings, this will produce a string that is a C++ representation of
    OR-ing the strings to produce the given integer. Examples:

    toString(5, {{1, "ONE"}, {2, "TWO"}, {4, "FOUR"}} -> "ONE | FOUR"
    toString(7, {{1, "ONE"}, {2, "TWO"}, {4, "FOUR"}, {7, "ALL"}) -> "ALL"

    The current implementation is nothing fancy yet and will expect the
    map to contains values with a single bit set or comprehensive 'any'
    values that are returned preferably.
    It will put print lower bit values first into the resulting string.
*/
std::string toString(uint64_t value, const std::map<uint64_t, std::string> bitmap) PURE_FUNCTION;
#endif // BITCOIN_UTIL_H
