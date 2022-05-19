#include <catch2/catch.hpp>

#include <ThreadPool.hpp>

TEST_CASE("Basic ThreadPool tests", "[ThreadPool]") {
    SECTION("Basic construction/destruction tests") {
        REQUIRE_NOTHROW(opencmw::BasicThreadPool<opencmw::IO_BOUND>());
        REQUIRE_NOTHROW(opencmw::BasicThreadPool<opencmw::CPU_BOUND>());

        std::atomic<int>         enqueueCount{ 0 };
        std::atomic<int>         executeCount{ 0 };
        opencmw::BasicThreadPool<opencmw::IO_BOUND> pool("TestPool", 1, 2);
        REQUIRE_NOTHROW(pool.sleepDuration() = std::chrono::milliseconds(1));
        REQUIRE_NOTHROW(pool.keepAliveDuration() = std::chrono::seconds(10));
        pool.waitUntilInitialised();
        REQUIRE(pool.isInitialised());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
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

        REQUIRE_NOTHROW(pool.setAffinityMask(pool.getAffinityMask()));
        REQUIRE_NOTHROW(pool.setThreadSchedulingPolicy(pool.getSchedulingPolicy(), pool.getSchedulingPriority()));
    }

    SECTION("contention tests") {
        std::atomic<int>         counter{ 0 };
        opencmw::BasicThreadPool<opencmw::IO_BOUND> pool("contention", 1, 4);
        pool.waitUntilInitialised();
        REQUIRE(pool.isInitialised());
        REQUIRE(pool.numThreads() == 1);
        pool.execute([&counter] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); std::atomic_fetch_add(&counter, 1); counter.notify_all(); });
        REQUIRE(pool.numThreads() == 1);
        pool.execute([&counter] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); std::atomic_fetch_add(&counter, 1); counter.notify_all(); });
        REQUIRE(pool.numThreads() >= 1);
        counter.wait(0);
        counter.wait(1);
        REQUIRE(counter == 2);
    }
}