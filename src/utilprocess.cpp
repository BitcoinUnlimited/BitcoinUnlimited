// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilprocess.h"
#include "util.h"
#include <boost/filesystem.hpp>
#include <boost/predef/os.h>
#include <sstream>

#if BOOST_OS_LINUX
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
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

#if BOOST_OS_LINUX
class posix_error : public subprocess_error
{
public:
    //! fetches error number from (thread-local) global errno
    posix_error(const std::string &operation) : subprocess_error(operation + ": " + std::string(strerror(errno))) {}
    posix_error(const std::string &operation, int _errno)
        : subprocess_error(operation + ": " + std::string(strerror(_errno)))
    {
    }
};

#define THROW_IF_RETERROR(cond)               \
    {                                         \
        int errno_ = cond;                    \
        if (errno_ != 0)                      \
        {                                     \
            throw posix_error(#cond, errno_); \
        }                                     \
    }
#define THROW_CHECK_ERRNO(cond)       \
    {                                 \
        if ((cond) != 0)              \
        {                             \
            throw posix_error(#cond); \
        }                             \
    }
#endif

SubProcess::SubProcess(const std::string &path_,
    const std::vector<std::string> &args_,
    getline_callb stdout_callb_,
    getline_callb stderr_callb_)
    : path(path_), args(args_), stdout_callb(stdout_callb_), stderr_callb(stderr_callb_), pid(-1), is_running(false),
      run_started(false)
{
#if !BOOST_OS_LINUX
    throw unsupported_platform_error(__func__);
#endif
}

SubProcess::~SubProcess()
{
#ifdef DEBUG_ASSERTION
    assert(!run_started);
#endif
    if (!run_started)
    {
        return;
    }

    LOGA("CRITICAL ERROR: ~SubProcess called while process is running.");
    try
    {
        // Try to recover
        Terminate();
        while (run_started)
        {
            std::this_thread::yield();
        }
    }
    catch (...)
    {
        LOGA("~SubProcess failed to terminate process");
    }
}

void extract_line(std::string &buffer, getline_callb &callb)
{
    size_t pos = 0;
    while ((pos = buffer.find('\n')) != std::string::npos)
    {
        callb(buffer.substr(0, pos));
        buffer.erase(0, pos + 1);
    }
}

#if BOOST_OS_LINUX
struct RunCleanupRAII
{
    std::atomic<bool> &is_running;
    std::atomic<bool> &run_started;
    posix_spawn_file_actions_t *action;

    ~RunCleanupRAII()
    {
        run_started.store(false);
        is_running.store(false);
        if (action != nullptr)
        {
            posix_spawn_file_actions_destroy(action);
        }
    }
};

struct SpawnAttrRAII
{
    posix_spawnattr_t posixAttr;

    SpawnAttrRAII() { THROW_IF_RETERROR(posix_spawnattr_init(&posixAttr)); }
    ~SpawnAttrRAII() { posix_spawnattr_destroy(&posixAttr); }
};
#endif

void SubProcess::Run()
{
    DbgAssert(!run_started, return );
#if BOOST_OS_LINUX
    run_started.store(true);
    RunCleanupRAII cleanup{is_running, run_started, nullptr};

    const size_t PARENT = 0;
    const size_t CHILD = 1;

    int cout_pipe[2];
    int cerr_pipe[2];


    THROW_CHECK_ERRNO(pipe(cout_pipe));
    THROW_CHECK_ERRNO(pipe(cerr_pipe));

    // Plumbing to access the child process' stdout and stderr
    posix_spawn_file_actions_t action;
    THROW_IF_RETERROR(posix_spawn_file_actions_init(&action));
    cleanup.action = &action;

    THROW_IF_RETERROR(posix_spawn_file_actions_addclose(&action, cout_pipe[PARENT]));
    THROW_IF_RETERROR(posix_spawn_file_actions_addclose(&action, cerr_pipe[PARENT]));
    THROW_IF_RETERROR(posix_spawn_file_actions_adddup2(&action, cout_pipe[CHILD], 1));
    THROW_IF_RETERROR(posix_spawn_file_actions_adddup2(&action, cerr_pipe[CHILD], 2));
    THROW_IF_RETERROR(posix_spawn_file_actions_addclose(&action, cout_pipe[CHILD]));
    THROW_IF_RETERROR(posix_spawn_file_actions_addclose(&action, cerr_pipe[CHILD]));

    // Spawn the child process
    std::string filename = boost::filesystem::path(path).filename().string();
    std::vector<char *> cmdargs(1 + args.size() + 1);
    cmdargs[0] = const_cast<char *>(filename.c_str());
    for (size_t i = 0; i < args.size(); ++i)
    {
        cmdargs[i + 1] = const_cast<char *>(args[i].c_str());
    }
    cmdargs[cmdargs.size() - 1] = nullptr;
    pid_t child_pid;
    {
        SpawnAttrRAII attr;
        THROW_IF_RETERROR(posix_spawnattr_setflags(&attr.posixAttr, POSIX_SPAWN_SETPGROUP));
        THROW_IF_RETERROR(posix_spawnattr_setpgroup(&attr.posixAttr, 0));

        THROW_IF_RETERROR(posix_spawnp(&child_pid, path.c_str(), &action, &attr.posixAttr, &cmdargs[0], nullptr));
    }

    pid.store(child_pid);
    is_running.store(true);

    // Close child side of pipes
    THROW_CHECK_ERRNO(close(cout_pipe[CHILD]));
    THROW_CHECK_ERRNO(close(cerr_pipe[CHILD]));

    // Read stdin, stdout
    std::string buffer(1024, '\0');
    std::vector<pollfd> plist = {{cout_pipe[PARENT], POLLIN, 0}, {cerr_pipe[PARENT], POLLIN, 0}};

    std::string stdout_buffer;
    std::string stderr_buffer;

    while (true)
    {
        const int timeout = -1;
        int rc = poll(&plist[0], plist.size(), timeout);
        if (rc == -1)
        {
            LOGA("%s: poll error %s", __func__, strerror(errno));
            break;
        }
        if (rc == 0)
        {
            // timeout, this shouldn't happen
            LOGA("%s: unexpected poll timeout", __func__);
            break;
        }

        const size_t PIPE_STDOUT = 0;
        const size_t PIPE_STDERR = 1;

        if (plist[PIPE_STDOUT].revents & POLLIN)
        {
            int bytes_read = read(cout_pipe[PARENT], &buffer[0], buffer.length());
            stdout_buffer += buffer.substr(0, static_cast<size_t>(bytes_read));
            extract_line(stdout_buffer, stdout_callb);
            continue;
        }
        if (plist[PIPE_STDERR].revents & POLLIN)
        {
            int bytes_read = read(cerr_pipe[PARENT], &buffer[0], buffer.length());
            stderr_buffer += buffer.substr(0, static_cast<size_t>(bytes_read));
            extract_line(stderr_buffer, stderr_callb);
            continue;
        }
        // nothing left to read
        break;
    }

    int status;
    if (waitpid(pid, &status, 0) == -1)
    {
        throw subprocess_error("waitpid failed");
    }

    if (WIFEXITED(status))
    {
        int exit_status = WEXITSTATUS(status);
        if (exit_status != 0)
        {
            subprocess_error err("exited with error");
            err.exit_status = exit_status;
            throw err;
        }
    }
    else if (WIFSIGNALED(status))
    {
        subprocess_error err("terminated by signal");
        err.termination_signal = WTERMSIG(status);
        throw err;
    }
    else
    {
        throw subprocess_error("unknown termination reason");
    }
#endif
}

void SubProcess::SendSignal(int signal)
{
#if BOOST_OS_LINUX
    int curr_pid = pid; // copy to avoid a race
    if (curr_pid == -1)
    {
        std::stringstream err;
        err << "Cannot signal " << signal << " to " << path << ", process is not running.";
        throw subprocess_error(err.str());
    }
    if (kill(static_cast<pid_t>(curr_pid), signal) != 0)
    {
        std::stringstream err;
        err << "Failed to send signal " << signal << " to pid " << curr_pid;
        throw subprocess_error(err.str());
    }
#endif
}

void SubProcess::Interrupt()
{
#if BOOST_OS_LINUX
    SendSignal(SIGINT);
#endif
}

void SubProcess::Terminate()
{
#if BOOST_OS_LINUX
    SendSignal(SIGKILL);
#endif
}
