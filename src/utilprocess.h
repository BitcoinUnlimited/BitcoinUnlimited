// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef UTILPROCESS_H
#define UTILPROCESS_H

#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

//! thrown when function is not (yet) implemented for platfrom
class unsupported_platform_error : public std::runtime_error
{
public:
    unsupported_platform_error(const std::string &func_name);
};

//! full path for running process
std::string this_process_path();

class subprocess_error : public std::runtime_error
{
public:
    using runtime_error::runtime_error;
    int exit_status = -1;
    int termination_signal = -1;
};

using getline_callb = std::function<void(const std::string &line)>;
class SubProcess
{
public:
    SubProcess(const std::string &path,
        const std::vector<std::string> &args,
        getline_callb stdout_callb,
        getline_callb stderr_callb);
    ~SubProcess();

    //! Spawn the process. Blocks until process exits.
    void Run();

    //! PID is -1 if process has not started yet.
    int GetPID() const { return pid; }
    bool IsRunning() const { return is_running; }
    void Interrupt();
    void Terminate();
    void SendSignal(int signal);


private:
    std::string path;
    std::vector<std::string> args;
    getline_callb stdout_callb;
    getline_callb stderr_callb;
    std::atomic<int> pid;
    std::atomic<bool> is_running;
    std::atomic<bool> run_started;
};


#endif
