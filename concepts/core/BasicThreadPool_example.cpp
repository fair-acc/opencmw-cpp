#include <atomic>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral" // suppress warning caused by format not a string literal, format string not checked
#include <fmt/chrono.h>
#include <fmt/format.h>
#pragma GCC diagnostic pop

#include <ThreadPool.hpp>

int main() {
    using namespace std::chrono;

    opencmw::BasicThreadPool pool("CustomPool", 1, 1000); // create a thread pool with at least 1 and a max of 1000 threads
    pool.keepAliveDuration() = seconds(10);               // keeps idling threads alive for 10 seconds
    pool.waitUntilInitialised();                          // wait until the pool is initialised (optional)
    assert(pool.isInitialised());                         // check if the pool is initialised

    // enqueue and add task to list
    pool.enqueue([] { fmt::print("Hello World from thread '{}'!\n", opencmw::thread::getThreadName()); });

    // enqueue a (potentially blocking) task and spawns a new thread of no free thread is available
    pool.execute([] { fmt::print("Hello World from thread '{}'!\n", opencmw::thread::getThreadName()); });

    constexpr int nTestRun = 5;
    constexpr int nTasks   = 100;
    for (int testRun = 0; testRun < nTestRun; testRun++) {
        // enqueue nTasks tasks on 1 threads -> the other tasks will need to wait for a thread to become available
        const auto       start = steady_clock::now();
        std::atomic<int> counter(0);
        for (int i = 0; i < nTasks; i++) {
            pool.enqueue([&counter] { std::this_thread::sleep_for(milliseconds(10)); ++counter; counter.notify_one(); });
        }
        auto const diff1 = steady_clock::now() - start;
        while (std::atomic_load(&counter) < nTasks)
            ; // wait until all tasks are finished
        auto const diff2 = steady_clock::now() - start;
        fmt::print("run {}: {:12} -- dispatching took {:>7} -- execution took {:>7} - #threads: {}\n", testRun, "enqueue",
                duration_cast<microseconds>(diff1), duration_cast<milliseconds>(diff2), pool.numThreads());
    }

    for (int testRun = 0; testRun < nTestRun; testRun++) {
        // execute nTasks tasks on up to 100 threads
        const auto       start = steady_clock::now();
        std::atomic<int> counter(0);
        for (int i = 0; i < nTasks; i++) {
            pool.execute([&counter] { std::this_thread::sleep_for(milliseconds(10)); ++counter; counter.notify_one(); });
        }
        auto const diff1 = steady_clock::now() - start;
        while (std::atomic_load(&counter) < nTasks)
            ; // wait until all tasks are finished
        auto const diff2 = steady_clock::now() - start;
        std::this_thread::sleep_for(milliseconds(10));
        fmt::print("run {}: {:12} -- dispatching took {:>7} -- execution took {:>7} - #threads: {}\n", testRun, "execute",
                duration_cast<microseconds>(diff1), duration_cast<milliseconds>(diff2), pool.numThreads());
    }

    for (int testRun = 0; testRun < nTestRun; testRun++) {
        // execute nTasks tasks on up to 100 threads
        const auto       start = steady_clock::now();
        std::atomic<int> counter(0);
        for (int i = 0; i < nTasks; i++) {
            pool.enqueue([&counter] { std::this_thread::sleep_for(milliseconds(10)); ++counter; counter.notify_one(); });
        }
        auto const diff1 = steady_clock::now() - start;
        while (std::atomic_load(&counter) < nTasks)
            ; // wait until all tasks are finished
        auto const diff2 = steady_clock::now() - start;
        std::this_thread::sleep_for(milliseconds(10));
        fmt::print("run {}: {:12} -- dispatching took {:>7} -- execution took {:>7} - #threads: {}\n", testRun, "enqueue",
                duration_cast<microseconds>(diff1), duration_cast<milliseconds>(diff2), pool.numThreads());
    }

    for (int testRun = 0; testRun < nTestRun; testRun++) {
        // execute nTasks tasks, each on a new jthreads (N.B. worst case timing <-> base-line benchmark)
        const auto                start = steady_clock::now();
        std::atomic<int>          counter(0);
        std::vector<std::jthread> threads;
        for (int i = 0; i < nTasks; i++) {
            threads.emplace_back([&counter] { std::this_thread::sleep_for(milliseconds(10)); ++counter; counter.notify_one(); });
        }
        auto const diff1 = steady_clock::now() - start;
        while (std::atomic_load(&counter) < nTasks)
            ; // wait until all tasks are finished
        auto const diff2 = steady_clock::now() - start;
        std::this_thread::sleep_for(milliseconds(10));
        fmt::print("run {}: {:12} -- dispatching took {:>7} -- execution took {:>7} - #threads: {}\n", testRun, "bare-thread",
                duration_cast<microseconds>(diff1), duration_cast<milliseconds>(diff2), nTasks);
    }


    pool.requestShutdown(); // request the pool to shutdown
}