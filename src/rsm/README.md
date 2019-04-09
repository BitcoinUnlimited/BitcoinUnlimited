# cxx_recursive_shared_mutex

A recursive shared mutex for C++

__Behaviorial Overview__

The recursive shared mutex has the following behavior implemented:
- This mutex has two levels of access, shared and exclusive. Multiple threads can own this mutex in shared mode but only one can own it in exclusive mode.
- A thread is considered to have ownership when it successfully calls either lock or try_lock (for either level of access).
- A thread may recursively call lock for ownership and must call a matching number of unlock calls to end ownership.
- A thread may call for shared ownership if it already has exclusive ownership without giving up exclusive ownership.
- There is internal tracking of how many times a thread locked for shared ownership. A thread can not unlock more times than it locked. Trying to do so will cause an assertion as this is a critical error somewhere in the locking logic.
- A thread may obtain exclusive ownership if no threads excluding itself have shared ownership by calling try_promotion(). Doing so while other threads have shared ownership will block until all other threads have released their shared ownership. Promoting ownership in this way will "jump the line" of other threads that waiting for exclusive ownership and will cause the thread with shared ownership to become the next thread to obtain exclusive ownership. To avoid deadlocks only one thread may attempt this ownership promotion at a time. If a thread has already done this and is currently waiting for promotion and a different thread tries to request promotion the try_promotion() call will return false.
- If a thread has exclusive ownership and checks if it has shared ownership we should should return true.


__NOTES__

- We use try_promotion() for promotions instead of lock() for two reasons:
    - We want to be able to signal that we did not get the promotion. try_promotion() returns a boolean while lock() doesn't return anything.
    - It is generally discouraged to have a lot of threads potentially calling and waiting for promotions because this creates a sort of race condition where the edited data set will be the same for the first thread to get promoted but all following threads aren't guaranteed to be editing the same data that was observed during shared ownership
- if the thread that has exclusive ownership got that ownership via promotion, another thread can not request a promotion to follow it. this prevents threads that use promotion to continuously "cut the line" for exclusive ownership.




__Development and Testing__

The `master` branch `rsm` folder should be stable at all times. To use rsm in your project just add the `rsm` folder to your project.
The `experimental` folder has changes that are in testing. To build the test suite, run Make. This should produce a binary named text_cxx_rsm.


__Requirements__

- C++14 or higher is required.
- Boost 1.55.0 or higher is recommended.

__Upstream__

The upstream repository can be found at: https://github.com/Greg-Griffith/cxx_recursive_shared_mutex

Initial development and feedback can be found at: https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/1591
