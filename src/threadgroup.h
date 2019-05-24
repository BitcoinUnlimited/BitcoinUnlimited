#ifndef BITCOIN_THREAD_GROUP_H
#define BITCOIN_THREAD_GROUP_H

#include <atomic>
#include <thread>
#include <vector>


extern std::atomic<bool> shutdown_threads;

class thread_group
{
private:
    std::vector<std::thread> threads;

public:
    void interrupt_all() { shutdown_threads.store(true); }
    template <class Fn, class... Args>
    void create_thread(Fn &&f, Args &&... args)
    {
        threads.push_back(std::thread(f, args...));
    }

    bool empty() { return threads.size() == 0; }
    void join_all()
    {
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

#endif
