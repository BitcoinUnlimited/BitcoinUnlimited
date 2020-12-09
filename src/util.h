// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * logging, thread wrappers, startup time
 */
#ifndef BITCOIN_UTIL_H
#define BITCOIN_UTIL_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "allowed_args.h"
#include "compat.h"
#include "fs.h"
#include "logging.h"
#include "tinyformat.h"
#include "utiltime.h"

#include <exception>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

#ifndef ANDROID
#include <boost/signals2/signal.hpp>
#include <boost/thread.hpp>

/** Signals for translation. */
class CTranslationInterface
{
public:
    /** Translate a message to the native language of the user. */
    boost::signals2::signal<std::string(const char *psz)> Translate;
};
extern CTranslationInterface translationInterface;

/**
 * Translation function: Call Translate signal on UI interface, which returns a boost::optional result.
 * If no translation slot is registered, nothing is returned, and simply return the input.
 */
inline std::string _(const char *psz)
{
    boost::optional<std::string> rv = translationInterface.Translate(psz);
    return rv ? (*rv) : psz;
}

#else

class CTranslationInterface
{
public:
    std::string Translate(const char *psz);
};
extern CTranslationInterface translationInterface;

inline std::string _(const char *psz)
{
    std::string rv = translationInterface.Translate(psz);
    if (rv.empty())
        return std::string(psz);
    return rv;
}
#endif

// Preface any Shared Library API definition with this macro.  This will ensure that the function is available for
// external linkage.
// For example:
// SLAPI int myExportedFunc(unsigned char *buf, int num);
#define SLAPI extern "C" __attribute__((visibility("default")))

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

// Application startup time (used for uptime calculation)
int64_t GetStartupTime();

// For bitcoin-cli
extern const char DEFAULT_RPCCONNECT[];
static const int DEFAULT_HTTP_CLIENT_TIMEOUT = 900;

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

extern const char *const BITCOIN_CONF_FILENAME;
extern const char *const BITCOIN_PID_FILENAME;
extern const char *const FORKS_CSV_FILENAME; // bip135 added

// Takes a std::vector of strings and splits individual arguments further up if
// they contain commas. Also removes space from the output strings.
// For example, ["a", "b,c", "d"] becomes ["a", "b", "c", "d"]
extern std::vector<std::string> splitByCommasAndRemoveSpaces(const std::vector<std::string> &args,
    bool removeDuplicates = false);

/**
 * Translate a boolean string to a bool.
 * Throws an exception if not one of the strings.
 * Is case insensitive.
 * @param[in] one of "enable|disable|1|0|true|false|on|off"
 * returns true if enabled, false if not.
 */
bool IsStringTrue(const std::string &str);


void SetupEnvironment();
bool SetupNetworking();


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
fs::path GetForksCsvFile(); // bip135 added
void ReadConfigFile(std::map<std::string, std::string> &mapSettingsRet,
    std::map<std::string, std::vector<std::string> > &mapMultiSettingsRet,
    const AllowedArgs::AllowedArgs &allowedArgs);

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
 * Return double argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. 3.14)
 * @return command-line argument (0.0 if invalid number) or default value
 */
double GetDoubleArg(const std::string &strArg, double dDefault);

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
 * Unset an argument, reverting to default value.
 */
void UnsetArg(const std::string &strArg);

/**
 * Set a boolean argument
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param fValue Value (e.g. false)
 * @return none
 */
void SetBoolArg(const std::string &strArg, bool fValue);

/*
 * Convert string into true/false
 *
 * @param strValue String to parse as a boolean
 * @return true or false
 */
bool InterpretBool(const std::string &strValue);

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
#ifndef ANDROID
    catch (const boost::thread_interrupted &)
    {
        LOGA("%s thread interrupt\n", name);
        throw;
    }
#endif
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
