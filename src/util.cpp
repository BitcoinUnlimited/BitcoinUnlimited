// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "compat.h"

#include "util.h"

#include "chainparamsbase.h"
#include "fs.h"
#include "random.h"
#include "serialize.h"
#include "sync.h"
#include "utilstrencodings.h"
#include "utiltime.h"

#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdarg.h>

#if (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
#include <pthread.h>
#include <pthread_np.h>
#endif

#ifndef WIN32
// for posix_fallocate
#ifdef __linux__

#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif

#define _POSIX_C_SOURCE 200112L

#endif // __linux__

#include <algorithm>
#include <fcntl.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/stat.h>

#else

#ifdef _MSC_VER
#pragma warning(disable : 4786)
#pragma warning(disable : 4804)
#pragma warning(disable : 4805)
#pragma warning(disable : 4717)
#endif

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501

#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <io.h> /* for _commit */
#include <shlobj.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp> // for startswith() and endswith()
#include <boost/program_options/detail/config_file.hpp>
#include <boost/program_options/parsers.hpp>
#include <list>
#include <map>
#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <set>
#include <thread>
#include <thread>
#include <vector>
// std::scopted_lock not available until c++17, use boost for now
#include <boost/thread/mutex.hpp>

std::vector<std::string> splitByCommasAndRemoveSpaces(const std::vector<std::string> &args,
    bool removeDuplicates /* false */)
{
    std::vector<std::string> result;
    for (std::string arg : args)
    {
        size_t pos;
        while ((pos = arg.find(',')) != std::string::npos)
        {
            result.push_back(arg.substr(0, pos));
            arg = arg.substr(pos + 1);
        }
        std::string arg_nospace;
        for (char c : arg)
            if (c != ' ')
                arg_nospace += c;
        result.push_back(arg_nospace);
    }

    // remove duplicates from the list of debug categories
    if (removeDuplicates)
    {
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
    }
    std::reverse(result.begin(), result.end());
    return result;
}

// Work around clang compilation problem in Boost 1.46:
// /usr/include/boost/program_options/detail/config_file.hpp:163:17: error: call to function 'to_internal' that is
// neither visible in the template definition nor found by argument-dependent lookup
// See also: http://stackoverflow.com/questions/10020179/compilation-fail-in-boost-librairies-program-options
//           http://clang.debian.net/status.php?version=3.0&key=CANNOT_FIND_FUNCTION
namespace boost
{
namespace program_options
{
std::string to_internal(const std::string &);
}

} // namespace boost

namespace Logging
{
// Globals defined here because link fails if in globals.cpp.
// Keep at top of file so init first:
uint64_t categoriesEnabled = 0; // 64 bit log id mask.
static std::map<uint64_t, std::string> logLabelMap = LOGLABELMAP; // Lookup log label from log id.


uint64_t LogFindCategory(const std::string label)
{
    for (auto &x : logLabelMap)
    {
        if ((std::string)x.second == label)
            return (uint64_t)x.first;
    }
    return NONE;
}

std::string LogGetLabel(uint64_t category)
{
    std::string label = "none";
    if (logLabelMap.count(category) != 0)
        label = logLabelMap[category];

    return label;
}
// Return a string rapresentation of all debug categories and their current status,
// one category per line. If enabled is true it returns only the list of enabled
// debug categories concatenated in a single line.
std::string LogGetAllString(bool fEnabled)
{
    std::string allCategories = "";
    std::string enabledCategories = "";
    for (auto &x : logLabelMap)
    {
        if (x.first == ALL || x.first == NONE)
            continue;

        if (LogAcceptCategory(x.first))
        {
            allCategories += "on ";
            if (fEnabled)
                enabledCategories += (std::string)x.second + " ";
        }
        else
            allCategories += "   ";

        allCategories += (std::string)x.second + "\n";
    }
    // strip last char from enabledCategories if it is eqaul to a blank space
    if (enabledCategories.length() > 0)
        enabledCategories.pop_back();

    return fEnabled ? enabledCategories : allCategories;
}

void LogInit()
{
    std::string category = "";
    uint64_t catg = NONE;
    const std::vector<std::string> categories = splitByCommasAndRemoveSpaces(mapMultiArgs["-debug"], true);

    // enable all when given -debug=1 or -debug
    if (categories.size() == 1 && (categories[0] == "" || categories[0] == "1"))
    {
        LogToggleCategory(ALL, true);
    }
    else
    {
        for (std::string const &cat : categories)
        {
            category = boost::algorithm::to_lower_copy(cat);

            // remove the category from the list of enables one
            // if label is suffixed with a dash
            bool toggle_flag = true;

            if (category.length() > 0 && category.at(0) == '-')
            {
                toggle_flag = false;
                category.erase(0, 1);
            }

            if (category == "" || category == "1")
            {
                category = "all";
            }

            catg = LogFindCategory(category);

            if (catg == NONE) // Not a valid category
                continue;

            LogToggleCategory(catg, toggle_flag);
        }
    }
    LOGA("List of enabled categories: %s\n", LogGetAllString(true));
}
}

const char *const BITCOIN_CONF_FILENAME = "bitcoin.conf";
const char *const BITCOIN_PID_FILENAME = "bitcoind.pid";
const char *const FORKS_CSV_FILENAME = "forks.csv"; // bip135 added
// Application startup time (used for uptime calculation)
const int64_t nStartupTime = GetTime();

std::map<std::string, std::string> mapArgs;
std::map<std::string, std::vector<std::string> > mapMultiArgs;
bool fDebug = false;
bool fPrintToConsole = false;
bool fPrintToDebugLog = true;
bool fDaemon = false;
bool fServer = false;
std::string strMiscWarning;
bool fLogTimestamps = DEFAULT_LOGTIMESTAMPS;
bool fLogTimeMicros = DEFAULT_LOGTIMEMICROS;
bool fLogIPs = DEFAULT_LOGIPS;
volatile bool fReopenDebugLog = false;
CTranslationInterface translationInterface;

// None of this is needed with OpenSSL 1.1.0
#if OPENSSL_VERSION_NUMBER < 0x10100000L
/** Init OpenSSL library multithreading support */
static std::mutex **ppmutexOpenSSL;
void locking_callback(int mode, int i, const char *file, int line) NO_THREAD_SAFETY_ANALYSIS
{
    if (mode & CRYPTO_LOCK)
    {
        (*ppmutexOpenSSL[i]).lock();
    }
    else
    {
        (*ppmutexOpenSSL[i]).unlock();
    }
}
#endif

// Init
class CInit
{
public:
    CInit()
    {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        // Init OpenSSL library multithreading support
        ppmutexOpenSSL = (std::mutex **)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(std::mutex *));
        for (int i = 0; i < CRYPTO_num_locks(); i++)
            ppmutexOpenSSL[i] = new std::mutex();
        CRYPTO_set_locking_callback(locking_callback);

        // OpenSSL can optionally load a config file which lists optional loadable modules and engines.
        // We don't use them so we don't require the config. However some of our libs may call functions
        // which attempt to load the config file, possibly resulting in an exit() or crash if it is missing
        // or corrupt. Explicitly tell OpenSSL not to try to load the file. The result for our libs will be
        // that the config appears to have been loaded and there are no modules/engines available.
        OPENSSL_no_config();
#else
        OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, nullptr);
#endif

#ifdef WIN32
        // Seed OpenSSL PRNG with current contents of the screen
        RAND_screen();
#endif

        // Seed OpenSSL PRNG with performance counter
        RandAddSeed();
    }
    ~CInit()
    {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        // Securely erase the memory used by the PRNG
        RAND_cleanup();
        // Shutdown OpenSSL library multithreading support
        CRYPTO_set_locking_callback(nullptr);
        for (int i = 0; i < CRYPTO_num_locks(); i++)
            delete ppmutexOpenSSL[i];
        OPENSSL_free(ppmutexOpenSSL);
#else
        // Adding this on the side of caution, perhaps unnecessary according to OpenSSL 1.1 docs:
        // "Deinitialises OpenSSL (both libcrypto and libssl). All resources allocated by OpenSSL are freed.
        // Typically there should be no need to call this function directly as it is initiated automatically on
        // application exit. This is done via the standard C library atexit() function."
        // https://www.openssl.org/docs/man1.1.1/man3/OPENSSL_cleanup.html
        OPENSSL_cleanup();
#endif
    }
} instance_of_cinit;

/**
 * LOGA() has been broken a couple of times now
 * by well-meaning people adding mutexes in the most straightforward way.
 * It breaks because it may be called by global destructors during shutdown.
 * Since the order of destruction of static/global objects is undefined,
 * defining a mutex as a global object doesn't work (the mutex gets
 * destroyed, and then some later destructor calls OutputDebugStringF,
 * maybe indirectly, and you get a core dump at shutdown trying to lock
 * the mutex).
 */

std::once_flag debugPrintInitFlag;

/**
 * We use boost::call_once() to make sure mutexDebugLog and
 * vMsgsBeforeOpenLog are initialized in a thread-safe manner.
 *
 * NOTE: fileout, mutexDebugLog and sometimes vMsgsBeforeOpenLog
 * are leaked on exit. This is ugly, but will be cleaned up by
 * the OS/libc. When the shutdown sequence is fully audited and
 * tested, explicit destruction of these objects can be implemented.
 */
static FILE *fileout = nullptr;
static boost::mutex *mutexDebugLog = nullptr;
static std::list<std::string> *vMsgsBeforeOpenLog;

static int FileWriteStr(const std::string &str, FILE *fp) { return fwrite(str.data(), 1, str.size(), fp); }
static void DebugPrintInit()
{
    assert(mutexDebugLog == nullptr);
    mutexDebugLog = new boost::mutex();
    vMsgsBeforeOpenLog = new std::list<std::string>;
}

void OpenDebugLog()
{
    std::call_once(debugPrintInitFlag, &DebugPrintInit);
    boost::mutex::scoped_lock scoped_lock(*mutexDebugLog);

    assert(fileout == nullptr);
    assert(vMsgsBeforeOpenLog);
    fs::path pathDebug = GetDataDir() / "debug.log";
    fileout = fsbridge::fopen(pathDebug, "a");
    if (fileout)
    {
        setbuf(fileout, nullptr); // unbuffered
        // dump buffered messages from before we opened the log
        while (!vMsgsBeforeOpenLog->empty())
        {
            FileWriteStr(vMsgsBeforeOpenLog->front(), fileout);
            vMsgsBeforeOpenLog->pop_front();
        }
    }

    delete vMsgsBeforeOpenLog;
    vMsgsBeforeOpenLog = nullptr;
}

/** All logs are automatically CR terminated.  If you want to construct a single-line log out of multiple calls, don't.
    Make your own temporary.  You can make a multi-line log by adding \n in your temporary.
 */
static std::string LogTimestampStr(const std::string &str, std::string &logbuf)
{
    if (!logbuf.size())
    {
        int64_t nTimeMicros = GetLogTimeMicros();
        if (fLogTimestamps)
        {
            logbuf = DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nTimeMicros / 1000000);
            if (fLogTimeMicros)
                logbuf += strprintf(".%06d", nTimeMicros % 1000000);
        }
        logbuf += ' ' + str;
    }
    else
    {
        logbuf += str;
    }

    if (logbuf.size() && logbuf[logbuf.size() - 1] != '\n')
    {
        logbuf += '\n';
    }

    std::string result = logbuf;
    logbuf.clear();
    return result;
}

static void MonitorLogfile()
{
    // Check if debug.log has been deleted or moved.
    // If so re-open
    static int existcounter = 1;
    static fs::path fileName = GetDataDir() / "debug.log";
    existcounter++;
    if (existcounter % 63 == 0) // Check every 64 log msgs
    {
        bool exists = boost::filesystem::exists(fileName);
        if (!exists)
            fReopenDebugLog = true;
    }
}

void LogFlush()
{
    if (fPrintToDebugLog)
    {
        fflush(fileout);
    }
}

int LogPrintStr(const std::string &str)
{
    int ret = 0; // Returns total number of characters written
    std::string logbuf;
    std::string strTimestamped = LogTimestampStr(str, logbuf);

    if (!strTimestamped.size())
        return 0;

    if (fPrintToConsole)
    {
        // print to console
        ret = fwrite(strTimestamped.data(), 1, strTimestamped.size(), stdout);
        fflush(stdout);
    }
    if (fPrintToDebugLog)
    {
        std::call_once(debugPrintInitFlag, &DebugPrintInit);
        boost::mutex::scoped_lock scoped_lock(*mutexDebugLog);

        // buffer if we haven't opened the log yet
        if (fileout == nullptr)
        {
            assert(vMsgsBeforeOpenLog);
            ret = strTimestamped.length();
            vMsgsBeforeOpenLog->push_back(strTimestamped);
        }
        else
        {
            // reopen the log file, if requested
            if (fReopenDebugLog)
            {
                fReopenDebugLog = false;
                fs::path pathDebug = GetDataDir() / "debug.log";
                if (fsbridge::freopen(pathDebug, "a", fileout) != nullptr)
                    setbuf(fileout, nullptr); // unbuffered
            }

            ret = FileWriteStr(strTimestamped, fileout);
            MonitorLogfile();
        }
    }
    return ret;
}

std::string formatInfoUnit(double value)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};

    size_t i = 0;
    while ((value > 1000.0 || value < -1000.0) && i < (sizeof(units) / sizeof(units[0])) - 1)
    {
        value /= 1000.0;
        i++;
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << value << units[i];
    return ss.str();
}

static const std::set<std::string> affirmativeStrings{"", "1", "t", "y", "true", "yes"};

/** Interpret string as boolean, for argument parsing */
bool InterpretBool(const std::string &strValue) { return (affirmativeStrings.count(strValue) != 0); }
/** Turn -noX into -X=0 */
static void InterpretNegativeSetting(std::string &strKey, std::string &strValue)
{
    if (strKey.length() > 3 && strKey[0] == '-' && strKey[1] == 'n' && strKey[2] == 'o')
    {
        strKey = "-" + strKey.substr(3);
        strValue = InterpretBool(strValue) ? "0" : "1";
    }
}

void ParseParameters(int argc, const char *const argv[], const AllowedArgs::AllowedArgs &allowedArgs)
{
    mapArgs.clear();
    mapMultiArgs.clear();

    for (int i = 1; i < argc; i++)
    {
        std::string str(argv[i]);
        std::string strValue;
        size_t is_index = str.find('=');
        if (is_index != std::string::npos)
        {
            strValue = str.substr(is_index + 1);
            str = str.substr(0, is_index);
        }
#ifdef WIN32
        if (boost::algorithm::starts_with(str, "/"))
            str = "-" + str.substr(1);
#endif

        if (str[0] != '-')
            break;

        // Interpret --foo as -foo.
        // If both --foo and -foo are set, the last takes effect.
        if (str.length() > 1 && str[1] == '-')
            str = str.substr(1);
        InterpretNegativeSetting(str, strValue);
        allowedArgs.checkArg(str.substr(1), strValue);

        mapArgs[str] = strValue;
        mapMultiArgs[str].push_back(strValue);
    }
}

std::string GetArg(const std::string &strArg, const std::string &strDefault)
{
    if (mapArgs.count(strArg))
        return mapArgs[strArg];
    return strDefault;
}

int64_t GetArg(const std::string &strArg, int64_t nDefault)
{
    if (mapArgs.count(strArg))
        return atoi64(mapArgs[strArg]);
    return nDefault;
}

double GetDoubleArg(const std::string &strArg, double dDefault)
{
    if (mapArgs.count(strArg))
        return atof(mapArgs[strArg].c_str()); // returns 0.0 on conversion failure
    return dDefault;
}

bool GetBoolArg(const std::string &strArg, bool fDefault)
{
    if (mapArgs.count(strArg))
        return InterpretBool(mapArgs[strArg]);
    return fDefault;
}

// You can set the args directly, using SetArg which always will update the value or you can use
// SoftSetArg which will only set the value if it hasn't already been set and return success/fail.
void SetArg(const std::string &strArg, const std::string &strValue) { mapArgs[strArg] = strValue; }
void UnsetArg(const std::string &strArg) { mapArgs.erase(strArg); }
void SetBoolArg(const std::string &strArg, bool fValue)
{
    if (fValue)
        SetArg(strArg, std::string("1"));
    else
        SetArg(strArg, std::string("0"));
}
bool SoftSetArg(const std::string &strArg, const std::string &strValue)
{
    if (mapArgs.count(strArg))
        return false;
    mapArgs[strArg] = strValue;
    return true;
}
bool SoftSetBoolArg(const std::string &strArg, bool fValue)
{
    if (fValue)
        return SoftSetArg(strArg, std::string("1"));
    else
        return SoftSetArg(strArg, std::string("0"));
}

static std::string FormatException(const std::exception *pex, const char *pszThread)
{
#ifdef WIN32
    char pszModule[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, pszModule, sizeof(pszModule));
#else
    const char *pszModule = "bitcoin";
#endif
    if (pex)
        return strprintf("EXCEPTION: %s       \n%s       \n%s in %s       \n", typeid(*pex).name(), pex->what(),
            pszModule, pszThread);
    else
        return strprintf("UNKNOWN EXCEPTION       \n%s in %s       \n", pszModule, pszThread);
}

void PrintExceptionContinue(const std::exception *pex, const char *pszThread)
{
    std::string message = FormatException(pex, pszThread);
    LOGA("\n\n************************\n%s\n", message);
    fprintf(stderr, "\n\n************************\n%s\n", message.c_str());
}

fs::path GetDefaultDataDir()
{
// Windows < Vista: C:\Documents and Settings\Username\Application Data\Bitcoin
// Windows >= Vista: C:\Users\Username\AppData\Roaming\Bitcoin
// Mac: ~/Library/Application Support/Bitcoin
// Unix: ~/.bitcoin
#ifdef WIN32
    // Windows
    return GetSpecialFolderPath(CSIDL_APPDATA) / "Bitcoin";
#else
    fs::path pathRet;
    char *pszHome = getenv("HOME");
    if (pszHome == nullptr || strlen(pszHome) == 0)
        pathRet = fs::path("/");
    else
        pathRet = fs::path(pszHome);
#ifdef MAC_OSX
    // Mac
    return pathRet / "Library/Application Support/Bitcoin";
#else
    // Unix
    return pathRet / ".bitcoin";
#endif
#endif
}

static fs::path pathCached;
static fs::path pathCachedNetSpecific;
static CCriticalSection csPathCached;

const fs::path &GetDataDir(bool fNetSpecific)
{
    LOCK(csPathCached);

    fs::path &path = fNetSpecific ? pathCachedNetSpecific : pathCached;

    // This can be called during exceptions by LOGA(), so we cache the
    // value so we don't have to do memory allocations after that.
    if (!path.empty())
        return path;

    if (mapArgs.count("-datadir"))
    {
        path = fs::system_complete(mapArgs["-datadir"]);
        if (!fs::is_directory(path))
        {
            std::stringstream err;
            err << "datadir path " << path << " is not a directory";
            throw std::invalid_argument(err.str());
        }
    }
    else
    {
        path = GetDefaultDataDir();
    }
    if (fNetSpecific)
        path /= BaseParams().DataDir();

    try
    {
        fs::create_directories(path);
    }
    catch (const fs::filesystem_error &e)
    {
        LOGA("failed to create directories to (%s): %s\n", path, e.what());
    }

    return path;
}

void ClearDatadirCache()
{
    LOCK(csPathCached);

    pathCached = fs::path();
    pathCachedNetSpecific = fs::path();
}

fs::path GetConfigFile(const std::string &confPath)
{
    fs::path pathConfigFile(confPath);
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir(false) / pathConfigFile;

    return pathConfigFile;
}

// bip135 added
/**
 * Function to return expected path of FORKS_CSV_FILENAME
 */
fs::path GetForksCsvFile()
{
    fs::path pathCsvFile(GetArg("-forks", FORKS_CSV_FILENAME));
    if (!pathCsvFile.is_complete())
        pathCsvFile = GetDataDir(false) / pathCsvFile;

    return pathCsvFile;
}

void ReadConfigFile(std::map<std::string, std::string> &mapSettingsRet,
    std::map<std::string, std::vector<std::string> > &mapMultiSettingsRet,
    const AllowedArgs::AllowedArgs &allowedArgs)
{
    fs::ifstream streamConfig(GetConfigFile(GetArg("-conf", BITCOIN_CONF_FILENAME)));
    if (!streamConfig.good())
        return; // No bitcoin.conf file is OK

    std::set<std::string> setOptions;
    setOptions.insert("*");

    for (boost::program_options::detail::config_file_iterator it(streamConfig, setOptions), end; it != end; ++it)
    {
        // Don't overwrite existing settings so command line settings override bitcoin.conf
        std::string strKey = std::string("-") + it->string_key;
        std::string strValue = it->value[0];
        InterpretNegativeSetting(strKey, strValue);
        allowedArgs.checkArg(strKey.substr(1), strValue);
        if (mapSettingsRet.count(strKey) == 0)
            mapSettingsRet[strKey] = strValue;
        mapMultiSettingsRet[strKey].push_back(strValue);
    }
    // If datadir is changed in .conf file:
    ClearDatadirCache();
}

#ifndef WIN32
fs::path GetPidFile()
{
    fs::path pathPidFile(GetArg("-pid", BITCOIN_PID_FILENAME));
    if (!pathPidFile.is_complete())
        pathPidFile = GetDataDir() / pathPidFile;
    return pathPidFile;
}

void CreatePidFile(const fs::path &path, pid_t pid)
{
    FILE *file = fsbridge::fopen(path, "w");
    if (file)
    {
        fprintf(file, "%d\n", pid);
        fclose(file);
    }
}
#endif

bool RenameOver(fs::path src, fs::path dest)
{
#ifdef WIN32
    return MoveFileExA(src.string().c_str(), dest.string().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    int rc = std::rename(src.string().c_str(), dest.string().c_str());
    return (rc == 0);
#endif /* WIN32 */
}

/**
 * Ignores exceptions thrown by Boost's create_directories if the requested directory exists.
 * Specifically handles case where path p exists, but it wasn't possible for the user to
 * write to the parent directory.
 */
bool TryCreateDirectories(const fs::path &p)
{
    try
    {
        return fs::create_directories(p);
    }
    catch (const fs::filesystem_error &)
    {
        if (!fs::exists(p) || !fs::is_directory(p))
            throw;
    }

    // create_directories didn't create the directory, it had to have existed already
    return false;
}

void FileCommit(FILE *pFileout)
{
    fflush(pFileout); // harmless if redundantly called
#ifdef WIN32
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(pFileout));
    FlushFileBuffers(hFile);
#else
#if defined(__linux__) || defined(__NetBSD__)
    fdatasync(fileno(pFileout));
#elif defined(__APPLE__) && defined(F_FULLFSYNC)
    fcntl(fileno(pFileout), F_FULLFSYNC, 0);
#else
    fsync(fileno(pFileout));
#endif
#endif
}

bool TruncateFile(FILE *file, unsigned int length)
{
#if defined(WIN32)
    return _chsize(_fileno(file), length) == 0;
#else
    return ftruncate(fileno(file), length) == 0;
#endif
}

/**
 * this function tries to raise the file descriptor limit to the requested number.
 * It returns the actual file descriptor limit (which may be more or less than nMinFD)
 */
int RaiseFileDescriptorLimit(int nMinFD)
{
#if defined(WIN32)
    return FD_SETSIZE;
#else
    struct rlimit limitFD;
    if (getrlimit(RLIMIT_NOFILE, &limitFD) != -1)
    {
        if (limitFD.rlim_cur < (rlim_t)nMinFD)
        {
            limitFD.rlim_cur = nMinFD;
            if (limitFD.rlim_cur > limitFD.rlim_max)
                limitFD.rlim_cur = limitFD.rlim_max;
            setrlimit(RLIMIT_NOFILE, &limitFD);
            getrlimit(RLIMIT_NOFILE, &limitFD);
        }
        return limitFD.rlim_cur;
    }
    return nMinFD; // getrlimit failed, assume it's fine
#endif
}

/**
 * this function tries to make a particular range of a file allocated (corresponding to disk space)
 * it is advisory, and the range specified in the arguments will never contain live data
 */
void AllocateFileRange(FILE *file, unsigned int offset, unsigned int length)
{
#if defined(WIN32)
    // Windows-specific version
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(file));
    LARGE_INTEGER nFileSize;
    int64_t nEndPos = (int64_t)offset + length;
    nFileSize.u.LowPart = nEndPos & 0xFFFFFFFF;
    nFileSize.u.HighPart = nEndPos >> 32;
    SetFilePointerEx(hFile, nFileSize, 0, FILE_BEGIN);
    SetEndOfFile(hFile);
#elif defined(MAC_OSX)
    // OSX specific version
    fstore_t fst;
    fst.fst_flags = F_ALLOCATECONTIG;
    fst.fst_posmode = F_PEOFPOSMODE;
    fst.fst_offset = 0;
    fst.fst_length = (off_t)offset + length;
    fst.fst_bytesalloc = 0;
    if (fcntl(fileno(file), F_PREALLOCATE, &fst) == -1)
    {
        fst.fst_flags = F_ALLOCATEALL;
        fcntl(fileno(file), F_PREALLOCATE, &fst);
    }
    ftruncate(fileno(file), fst.fst_length);
#elif defined(__linux__)
    // Version using posix_fallocate
    off_t nEndPos = (off_t)offset + length;
    posix_fallocate(fileno(file), 0, nEndPos);
#else
    // Fallback version
    // TODO: just write one byte per block
    static const char buf[65536] = {};
    fseek(file, offset, SEEK_SET);
    while (length > 0)
    {
        unsigned int now = 65536;
        if (length < now)
            now = length;
        fwrite(buf, 1, now, file); // allowed to fail; this function is advisory anyway
        length -= now;
    }
#endif
}

void ShrinkDebugFile()
{
    // Scroll debug.log if it's getting too big
    fs::path pathLog = GetDataDir() / "debug.log";
    FILE *file = fsbridge::fopen(pathLog, "r");
    // If debug.log file is more than 10% bigger the RECENT_DEBUG_HISTORY_SIZE
    // trim it down by saving only the last RECENT_DEBUG_HISTORY_SIZE bytes
    if (file && fs::file_size(pathLog) > 10 * 1000000)
    {
        // Restart the file with some of the end
        std::vector<char> vch(200000, 0);
        fseek(file, -((long)vch.size()), SEEK_END);
        int nBytes = fread(begin_ptr(vch), 1, vch.size(), file);
        fclose(file);

        file = fsbridge::fopen(pathLog, "w");
        if (file)
        {
            fwrite(begin_ptr(vch), 1, nBytes, file);
            fclose(file);
        }
    }
    else if (file != nullptr)
        fclose(file);
}

#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate)
{
    char pszPath[MAX_PATH] = "";

    if (SHGetSpecialFolderPathA(nullptr, pszPath, nFolder, fCreate))
    {
        return fs::path(pszPath);
    }

    LOGA("SHGetSpecialFolderPathA() failed, could not obtain requested path.\n");
    return fs::path("");
}
#endif

void runCommand(const std::string &strCommand)
{
    int nErr = ::system(strCommand.c_str());
    if (nErr)
        LOGA("runCommand error: system(%s) returned %d\n", strCommand, nErr);
}

void RenameThread(const char *name)
{
#if defined(PR_SET_NAME)
    // Only the first 15 characters are used (16 - NUL terminator)
    ::prctl(PR_SET_NAME, name, 0, 0, 0);
#elif (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
    pthread_set_name_np(pthread_self(), name);

#elif defined(MAC_OSX)
    pthread_setname_np(name);
#else
    // Prevent warnings for unused parameters...
    (void)name;
#endif
}

void SetupEnvironment()
{
// On most POSIX systems (e.g. Linux, but not BSD) the environment's locale
// may be invalid, in which case the "C" locale is used as fallback.
#if !defined(WIN32) && !defined(MAC_OSX) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
    try
    {
        std::locale(""); // Raises a runtime error if current locale is invalid
    }
    catch (const std::runtime_error &)
    {
        setenv("LC_ALL", "C", 1);
    }
#endif
    // The path locale is lazy initialized and to avoid deinitialization errors
    // in multithreading environments, it is set explicitly by the main thread.
    // A dummy locale is used to extract the internal default locale, used by
    // fs::path, which is then used to explicitly imbue the path.
    std::locale loc = fs::path::imbue(std::locale::classic());
    fs::path::imbue(loc);
}

bool SetupNetworking()
{
#ifdef WIN32
    // Initialize Windows Sockets
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2, 2), &wsadata);
    if (ret != NO_ERROR || LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2)
        return false;
#endif
    return true;
}

void SetThreadPriority(int nPriority)
{
#ifdef WIN32
    SetThreadPriority(GetCurrentThread(), nPriority);
#else // WIN32
#ifdef PRIO_THREAD
    setpriority(PRIO_THREAD, 0, nPriority);
#else // PRIO_THREAD
    setpriority(PRIO_PROCESS, 0, nPriority);
#endif // PRIO_THREAD
#endif // WIN32
}

int GetNumCores() { return std::thread::hardware_concurrency(); }
std::string CopyrightHolders(const std::string &strPrefix)
{
    std::string strCopyrightHolders = strPrefix + _(COPYRIGHT_HOLDERS);
    if (strCopyrightHolders.find("%s") != strCopyrightHolders.npos)
    {
        strCopyrightHolders = strprintf(strCopyrightHolders, _(COPYRIGHT_HOLDERS_SUBSTITUTION));
    }
    return strCopyrightHolders;
}

// Obtain the application startup time (used for uptime calculation)
int64_t GetStartupTime() { return nStartupTime; }
bool IsStringTrue(const std::string &str)
{
    static const std::set<std::string> strOn = {"enable", "1", "true", "True", "on"};
    static const std::set<std::string> strOff = {"disable", "0", "false", "False", "off"};

    if (strOn.count(str))
        return true;

    if (strOff.count(str))
        return false;

    std::ostringstream err;
    err << "invalid argument '" << str << "', expected any of: ";
    std::copy(begin(strOn), end(strOn), std::ostream_iterator<std::string>(err, ", "));
    std::copy(begin(strOff), end(strOff), std::ostream_iterator<std::string>(err, ", "));
    // substr to chop off last ', '
    throw std::invalid_argument(err.str().substr(0, err.str().size() - 2));
}

static const int wildmatch_max_length = 1024;

bool wildmatch(std::string pattern, std::string test)
{
    // stack overflow prevention
    if (test.size() > wildmatch_max_length || pattern.size() > wildmatch_max_length)
    {
        return false;
    }

    while (true)
    {
        // handle empty strings
        if (!test.size() && !pattern.size())
            return true;

        // handle trailing chars in test str
        if (test.size() && !pattern.size())
            return false;

        // handle trailing chars in  pattern str. Needs to be a single asterisk to match.
        if (!test.size() && pattern.size())
        {
            return pattern == "*";
        }

        // test.size() && pattern.size() holds when reaching here

        if (pattern[0] == '?')
        {
            pattern = pattern.substr(1);
            test = test.substr(1);
            continue;
        }

        if (pattern[0] == '*')
        {
            if (pattern.size() > 1)
            {
                // Will not try multiple ways to match to avoid the potential
                // for path explosion, like matching "*-*-*-*" to "------------" and the like
                // Just eat up the test string until the first char mismatches
                if (pattern[1] == '?' || pattern[1] == '*')
                {
                    // ** or *? patterns are disallowed in the midst of a matching expression
                    return false;
                }
                size_t i = 0;
                while (i < test.size())
                {
                    if (test[i] != pattern[1])
                        i++;
                    else
                        break;
                }
                if (i == test.size())
                    return true;

                pattern = pattern.substr(1);
                test = test.substr(i);
                continue;
            }
            else
                return true;
        }
        if (test[0] != pattern[0])
            return false;

        pattern = pattern.substr(1);
        test = test.substr(1);
    }
}

int ScheduleBatchPriority(void)
{
#ifdef SCHED_BATCH
    const static sched_param param{0};
    if (int ret = pthread_setschedparam(pthread_self(), SCHED_BATCH, &param))
    {
        LOGA("Failed to pthread_setschedparam: %s\n", strerror(errno));
        return ret;
    }
    return 0;
#else
    return 1;
#endif
}

std::string toString(uint64_t value, const std::map<uint64_t, std::string> bitmap)
{
    if (bitmap.count(value))
        return bitmap.at(value);

    int mask = 1;
    std::string result;

    while (value)
    {
        if ((value & 1) && bitmap.count(mask))
        {
            if (result.size())
                result += " | " + bitmap.at(mask);
            else
                result = bitmap.at(mask);
        }
        value >>= 1;
        mask <<= 1;
    }
    return result;
}

#ifdef DEBUG_PAUSE

// To integrate well with gdb, we want to show what thread has paused.  This requires some linux-specific code
// and headers.  To restrict accidental use of linux-specific code these headers are included here instead of at the
// file's top.
#ifdef __linux__
#include <sys/syscall.h>
#include <sys/types.h>
#endif

std::mutex dbgPauseMutex;
std::condition_variable dbgPauseCond;
void DbgPause()
{
#ifdef __linux__ // The thread ID returned by gettid is very useful since its shown in gdb
    printf("\n!!! Process %d, Thread %ld (%lx) paused !!!\n", getpid(), syscall(SYS_gettid), pthread_self());
#else
    printf("\n!!! Process %d paused !!!\n", getpid());
#endif
    std::unique_lock<std::mutex> lk(dbgPauseMutex);
    dbgPauseCond.wait(lk);
}

extern "C" void DbgResume() { dbgPauseCond.notify_all(); }
#endif
