// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_THREAD_GROUP_H
#define BITCOIN_THREAD_GROUP_H

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>


extern std::atomic<bool> shutdown_threads;

class thread_group
{
private:
    std::mutex cs_threads;
    std::vector<std::thread> threads;

public:
    void interrupt_all() { shutdown_threads.store(true); }
    template <class Fn, class... Args>
    void create_thread(Fn &&f, Args &&... args)
    {
        std::lock_guard<std::mutex> lock(cs_threads);
        threads.push_back(std::thread(f, args...));
    }

    uint32_t size()
    {
        std::lock_guard<std::mutex> lock(cs_threads);
        return threads.size();
    }

    bool empty()
    {
        std::lock_guard<std::mutex> lock(cs_threads);
        return threads.size() == 0;
    }

    void join_all()
    {
        std::lock_guard<std::mutex> lock(cs_threads);
        for (size_t i = 0; i < threads.size(); i++)
        {
            if (threads[i].joinable())
            {
                threads[i].join();
            }
        }
        threads.clear();
    }

    ~thread_group()
    {
        interrupt_all();
        join_all();
    }
};
extern thread_group threadGroup;
#endif
