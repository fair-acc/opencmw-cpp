#include <catch2/catch.hpp>

#include <ThreadPool.hpp>

TEST_CASE("Basic ThreadPool tests", "[ThreadPool]") {
    using namespace std::chrono_literals;
    SECTION("Basic construction/destruction tests") {
        REQUIRE_NOTHROW(opencmw::BasicThreadPool<opencmw::IO_BOUND>());
        REQUIRE_NOTHROW(opencmw::BasicThreadPool<opencmw::CPU_BOUND>());

        std::atomic<int>                            enqueueCount{ 0 };
        std::atomic<int>                            executeCount{ 0 };
        opencmw::BasicThreadPool<opencmw::IO_BOUND> pool("TestPool", 1, 2);
        REQUIRE_NOTHROW(pool.sleepDuration = 1ms);
        REQUIRE_NOTHROW(pool.keepAliveDuration = 10s);
        pool.waitUntilInitialised();
        REQUIRE(pool.isInitialised());
        std::this_thread::sleep_for(5ms);
        REQUIRE(pool.poolName() == "TestPool");
        REQUIRE(pool.minThreads() == 1);
        REQUIRE(pool.maxThreads() == 2);
        REQUIRE(pool.numThreads() == 1);
        REQUIRE(pool.numTasksRunning() == 0);
        REQUIRE(pool.numTasksQueued() == 0);
        REQUIRE(pool.numTasksRecycled() == 0);
        pool.execute([&enqueueCount] { ++enqueueCount; enqueueCount.notify_all(); });
        enqueueCount.wait(0);
        REQUIRE(pool.numThreads() == 1);
        pool.execute([&executeCount] { ++executeCount; executeCount.notify_all(); });
        executeCount.wait(0);
        REQUIRE(pool.numThreads() >= 1);
        REQUIRE(enqueueCount == 1);
        REQUIRE(executeCount == 1);

        auto ret = pool.execute([] { return 42; });
        REQUIRE(ret.get() == 42);

        auto taskName = pool.execute<"taskName", 0, -1>([] { return opencmw::thread::getThreadName(); });
        REQUIRE(taskName.get() == "taskName");

        REQUIRE_NOTHROW(pool.setAffinityMask(pool.getAffinityMask()));
        REQUIRE_NOTHROW(pool.setThreadSchedulingPolicy(pool.getSchedulingPolicy(), pool.getSchedulingPriority()));
    }

    SECTION("contention tests") {
        std::atomic<int>                            counter{ 0 };
        opencmw::BasicThreadPool<opencmw::IO_BOUND> pool("contention", 1, 4);
        pool.waitUntilInitialised();
        REQUIRE(pool.isInitialised());
        REQUIRE(pool.numThreads() == 1);
        pool.execute([&counter] { std::this_thread::sleep_for(10ms); std::atomic_fetch_add(&counter, 1); counter.notify_all(); });
        REQUIRE(pool.numThreads() == 1);
        pool.execute([&counter] { std::this_thread::sleep_for(10ms); std::atomic_fetch_add(&counter, 1); counter.notify_all(); });
        REQUIRE(pool.numThreads() >= 1);
        counter.wait(0);
        counter.wait(1);
        REQUIRE(counter == 2);
    }
}

TEST_CASE("ThreadPool: Thread count tests", "[ThreadPool][MinMaxThreads]") {
    using namespace std::chrono_literals;
    struct bounds_def {
        std::uint32_t min, max;
    };
    std::array<bounds_def, 5> bounds{
        bounds_def{ 1, 1 },
        bounds_def{ 1, 4 },
        bounds_def{ 2, 2 },
        bounds_def{ 2, 8 },
        bounds_def{ 4, 8 }
    };

    for (const auto [minThreads, maxThreads] : bounds) {
        for (const auto taskCount : { 2, 8, 32 }) {
            std::print("## Test with min={} and max={} and taskCount={}\n", minThreads, maxThreads, taskCount);
            std::atomic<int> counter{ 0 };

            // Pool with min and max thread count
            opencmw::BasicThreadPool<opencmw::IO_BOUND> pool("count_test", minThreads, maxThreads);
            pool.keepAliveDuration = 10ms; // default is 10 seconds, reducing for testing
            pool.waitUntilInitialised();

            for (int i = 0; i < taskCount; ++i) {
                pool.execute([&counter] {
                    std::this_thread::sleep_for(10ms);
                    std::atomic_fetch_add(&counter, 1);
                    counter.notify_all();
                });
            }
            REQUIRE(pool.numThreads() >= minThreads);
            // the maximum number of threads is not a hard limit, if there is a burst of execute calls, it will spwawn more than maxThreads trheads.
            // REQUIRE(pool.numThreads() == std::min(std::uint32_t(taskCount), maxThreads));

            for (int i = 0; i < taskCount; ++i) {
                counter.wait(i);
                REQUIRE(pool.numThreads() >= minThreads);
                // REQUIRE(pool.numThreads() <= maxThreads);
            }

            // We should have gotten back to minimum
            std::this_thread::sleep_for(100ms);
            REQUIRE(pool.numThreads() == minThreads);
            REQUIRE(counter == taskCount);
        }
    }
}