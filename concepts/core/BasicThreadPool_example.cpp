#include <atomic>
#include <format>

#include <opencmw.hpp>
#include <ThreadPool.hpp>

int main() {
    using namespace std::chrono;
    using opencmw::thread::getThreadName;

    opencmw::BasicThreadPool<opencmw::CPU_BOUND> poolWork("CustomCpuPool", 1, 1); // pool for CPU-bound tasks with exactly 1 thread
    opencmw::BasicThreadPool<opencmw::IO_BOUND>  poolIO("CustomIOPool", 1, 1000); // pool for IO-bound (potentially blocking) tasks with at least 1 and a max of 1000 threads
    poolIO.keepAliveDuration = seconds(10);                                       // keeps idling threads alive for 10 seconds
    poolIO.waitUntilInitialised();                                                // wait until the pool is initialised (optional)
    assert(poolIO.isInitialised());                                               // check if the pool is initialised

    // enqueue and add task to list -- w/o return type
    poolWork.execute([] { std::print("Hello World from thread '{}'!\n", getThreadName()); });
    // poolWork.execute([]<typename... T>(T&&...args) { std::print(std::format_string<T...>("Hello World from thread '{}'!\n"), std::forward<T>(args)...); }, getThreadName());
    // poolWork.execute([](const auto &...args) { std::print(std::runtime_format("Hello World from thread '{}'!\n"), args...); }, getThreadName());

    // constexpr auto           func1  = []<typename... T>(T&&...args) { return std::format(std::format_string<std::string, T...>("thread '{1}' scheduled task '{0}'!\n"), getThreadName(), args...); };
    constexpr auto           func1  = [](const auto &...args) { return std::format("thread '{1}' scheduled task '{0}'!\n", args..., getThreadName()); };
    std::future<std::string> result = poolIO.execute<"customTaskName">(func1, getThreadName());
    // do something else ... get result, wait if necessary
    std::cout << result.get() << std::endl;
    poolIO.setAffinityMask({ true, true, true, false });
    poolIO.execute<"task name", 20U, 2>([]() { std::print("Hello World from custom thread '{}'!\n", getThreadName()); }); // execute a task with a name, a priority and single-core affinity
    try {
        poolIO.execute<"customName", 20U, 3>([]() { /* this potentially long-running task is trackable via it's 'customName' thread name */ });
    } catch (const std::invalid_argument &e) {
        std::print("caught exception: {}\n", e.what());
    }

    constexpr int nTestRun = 5;
    constexpr int nTasks   = 100;
    for (int testRun = 0; testRun < nTestRun; testRun++) {
        // enqueue nTasks tasks on 1 threads -> the other tasks will need to wait until threads becomes available
        const auto       start = steady_clock::now();
        std::atomic<int> counter(0);
        for (int i = 0; i < nTasks; i++) {
            poolWork.execute([&counter] { std::this_thread::sleep_for(milliseconds(10)); ++counter; counter.notify_one(); });
        }
        auto const diff1 = steady_clock::now() - start;
        while (std::atomic_load(&counter) < nTasks)
            ; // wait until all tasks are finished
        auto const diff2 = steady_clock::now() - start;
        std::print("run {}: {:12} -- dispatching took {:>7} -- execution took {:>7} - #threads: {}\n", testRun, "CPU-bound",
                duration_cast<microseconds>(diff1), duration_cast<milliseconds>(diff2), poolWork.numThreads());
    }

    for (int testRun = 0; testRun < nTestRun; testRun++) {
        // execute nTasks tasks on up to 100 threads
        const auto       start = steady_clock::now();
        std::atomic<int> counter(0);
        for (int i = 0; i < nTasks; i++) {
            poolIO.execute([&counter] { std::this_thread::sleep_for(milliseconds(10)); ++counter; counter.notify_one(); });
        }
        auto const diff1 = steady_clock::now() - start;
        while (std::atomic_load(&counter) < nTasks)
            ; // wait until all tasks are finished
        auto const diff2 = steady_clock::now() - start;
        std::this_thread::sleep_for(milliseconds(10));
        std::print("run {}: {:12} -- dispatching took {:>7} -- execution took {:>7} - #threads: {}\n", testRun, "IO-bound",
                duration_cast<microseconds>(diff1), duration_cast<milliseconds>(diff2), poolIO.numThreads());
    }

    for (int testRun = 0; testRun < nTestRun; testRun++) {
        // execute nTasks tasks, each on a new jthreads (N.B. worst case timing <-> base-line benchmark)
        const auto             start = steady_clock::now();
        std::atomic<int>       counter(0);
        std::list<std::thread> threads;
        for (int i = 0; i < nTasks; i++) {
            threads.emplace_back([&counter] { std::this_thread::sleep_for(milliseconds(10)); ++counter; counter.notify_one(); });
        }
        auto const diff1 = steady_clock::now() - start;
        while (std::atomic_load(&counter) < nTasks)
            ; // wait until all tasks are finished
        auto const diff2 = steady_clock::now() - start;
        std::this_thread::sleep_for(milliseconds(10));
        std::print("run {}: {:12} -- dispatching took {:>7} -- execution took {:>7} - #threads: {}\n", testRun, "bare-thread",
                duration_cast<microseconds>(diff1), duration_cast<milliseconds>(diff2), nTasks);
        std::for_each(threads.begin(), threads.end(), [](auto &thread) { thread.join(); });
    }

    poolWork.requestShutdown();
    poolIO.requestShutdown(); // request the pool to shutdown
}
