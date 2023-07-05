#include <catch2/catch.hpp>
#include <Debug.hpp>
#include <fmt/format.h>
#include <ThreadAffinity.hpp>

#define REQUIRE_MESSAGE(cond, msg) \
    do { \
        INFO(msg); \
        REQUIRE(cond); \
    } while ((void) 0, 0)

TEST_CASE("thread_exception", "[ThreadAffinity]") {
    REQUIRE_NOTHROW(opencmw::thread::thread_exception());
    REQUIRE(std::string("thread_exception") == opencmw::thread::thread_exception().name());
    REQUIRE(opencmw::thread::thread_exception().message(-1) == "unknown threading error code -1");
    REQUIRE(opencmw::thread::thread_exception().message(-2) == "unknown threading error code -2");
    REQUIRE(!opencmw::thread::thread_exception().message(opencmw::thread::THREAD_UNINITIALISED).starts_with("unknown threading error code"));
    REQUIRE(!opencmw::thread::thread_exception().message(opencmw::thread::THREAD_ERROR_UNKNOWN).starts_with("unknown threading error code"));
    REQUIRE(!opencmw::thread::thread_exception().message(opencmw::thread::THREAD_VALUE_RANGE).starts_with("unknown threading error code"));
    REQUIRE(!opencmw::thread::thread_exception().message(opencmw::thread::THREAD_ERANGE).starts_with("unknown threading error code"));
}

TEST_CASE("thread_helper", "[ThreadAffinity]") {
    REQUIRE(opencmw::thread::detail::getEnumPolicy(SCHED_FIFO) == opencmw::thread::Policy::FIFO);
    REQUIRE(opencmw::thread::detail::getEnumPolicy(SCHED_RR) == opencmw::thread::Policy::ROUND_ROBIN);
    REQUIRE(opencmw::thread::detail::getEnumPolicy(SCHED_OTHER) == opencmw::thread::Policy::OTHER);
    REQUIRE(opencmw::thread::detail::getEnumPolicy(-1) == opencmw::thread::Policy::UNKNOWN);
    REQUIRE(opencmw::thread::detail::getEnumPolicy(-2) == opencmw::thread::Policy::UNKNOWN);
}

TEST_CASE("basic thread affinity", "[ThreadAffinity]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("ThreadAffinity - basic thread affinity", 40);

    using namespace opencmw;
    std::atomic<bool>    run         = true;
    const auto           dummyAction = [&run]() { while (run) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); } };
    std::thread          testThread(dummyAction);

    constexpr std::array threadMap = { true, false, false, false };
    thread::setThreadAffinity(threadMap, testThread);
    auto affinity = thread::getThreadAffinity(testThread);
    bool equal    = true;
    for (size_t i = 0; i < std::min(threadMap.size(), affinity.size()); i++) {
        if (threadMap[i] != affinity[i]) {
            equal = false;
        }
    }
    REQUIRE_MESSAGE(equal, fmt::format("set {{{}}} affinity map does not match get {{{}}} map", fmt::join(threadMap.begin(), threadMap.end(), ", "), fmt::join(affinity.begin(), affinity.end(), ", ")));

    // tests w/o thread argument
    constexpr std::array threadMapOn = { true, true };
    thread::setThreadAffinity(threadMapOn);
    affinity = thread::getThreadAffinity();
    for (size_t i = 0; i < std::min(threadMapOn.size(), affinity.size()); i++) {
        if (threadMapOn[i] != affinity[i]) {
            equal = false;
        }
    }
    REQUIRE_MESSAGE(equal, fmt::format("set {{{}}} affinity map does not match get {{{}}} map", fmt::join(threadMap.begin(), threadMap.end(), ", "), fmt::join(affinity.begin(), affinity.end(), ", ")));

    std::thread bogusThread;
    REQUIRE_THROWS_AS(thread::getThreadAffinity(bogusThread), std::system_error);
    REQUIRE_THROWS_AS(thread::setThreadAffinity(threadMapOn, bogusThread), std::system_error);

    run = false;
    testThread.join();
}

TEST_CASE("basic process affinity", "[ThreadAffinity]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("ThreadAffinity - basic process affinity", 40);

    using namespace opencmw;
    constexpr std::array threadMap = { true, false, false, false };
    thread::setProcessAffinity(threadMap);
    auto affinity = thread::getProcessAffinity();
    bool equal    = true;
    for (size_t i = 0; i < std::min(threadMap.size(), affinity.size()); i++) {
        if (threadMap[i] != affinity[i]) {
            equal = false;
        }
    }
    REQUIRE_MESSAGE(equal, fmt::format("set {{{}}} affinity map does not match get {{{}}} map", fmt::join(threadMap.begin(), threadMap.end(), ", "), fmt::join(affinity.begin(), affinity.end(), ", ")));
    constexpr std::array threadMapOn = { true, true, true, true };
    thread::setProcessAffinity(threadMapOn);
    REQUIRE_THROWS_AS(thread::getProcessAffinity(-1), std::system_error);
    REQUIRE_THROWS_AS(thread::setProcessAffinity(threadMapOn, -1), std::system_error);
}

TEST_CASE("ThreadName", "[ThreadAffinity]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("ThreadAffinity - ThreadName", 40);

    using namespace opencmw;
    REQUIRE("" != thread::getThreadName());
    REQUIRE_NOTHROW(thread::setThreadName("testCoreName"));
    REQUIRE("testCoreName" == thread::getThreadName());

    std::atomic<bool> run         = true;
    const auto        dummyAction = [&run]() { while (run) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); } };
    std::thread       testThread(dummyAction);
    REQUIRE("" != thread::getThreadName(testThread));
    REQUIRE_NOTHROW(thread::setThreadName("testThreadName", testThread));
    thread::setThreadName("testThreadName", testThread);
    REQUIRE("testThreadName" == thread::getThreadName(testThread));

    std::thread uninitialisedTestThread;
    REQUIRE_THROWS_AS(thread::getThreadName(uninitialisedTestThread), std::system_error);
    REQUIRE_THROWS_AS(thread::setThreadName("name", uninitialisedTestThread), std::system_error);
    run = false;
    testThread.join();
}

TEST_CASE("ProcessName", "[ThreadAffinity]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("ThreadAffinity - ProcessName", 40);

    using namespace opencmw;
    REQUIRE("" != thread::getProcessName());
    REQUIRE(thread::getProcessName() == thread::getProcessName(thread::detail::getPid()));

    REQUIRE_NOTHROW(thread::setProcessName("TestProcessName"));
    REQUIRE("TestProcessName" == thread::getProcessName());
}

TEST_CASE("ProcessSchedulingParameter", "[ThreadAffinity]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("ThreadAffinity - ProcessSchedParameter", 40);

    using namespace opencmw::thread;
    struct SchedulingParameter param = getProcessSchedulingParameter();
    REQUIRE(param.policy == OTHER);
    REQUIRE(param.priority == 0);

    REQUIRE_NOTHROW(setProcessSchedulingParameter(OTHER, 0));
    REQUIRE_THROWS_AS(setProcessSchedulingParameter(OTHER, 0, -1), std::system_error);
    REQUIRE_THROWS_AS(setProcessSchedulingParameter(OTHER, 4), std::system_error);
    REQUIRE_THROWS_AS(setProcessSchedulingParameter(ROUND_ROBIN, 5), std::system_error); // missing rights -- because most users do not have CAP_SYS_NICE rights by default -- hard to unit-test
    param = getProcessSchedulingParameter();
    REQUIRE(param.policy == OTHER);
    REQUIRE(param.priority == 0);

    REQUIRE_THROWS_AS(getProcessSchedulingParameter(-1), std::system_error);
    REQUIRE_THROWS_AS(setProcessSchedulingParameter(ROUND_ROBIN, 5, -1), std::system_error);

    REQUIRE(opencmw::thread::detail::getEnumPolicy(SCHED_FIFO) == opencmw::thread::FIFO);
    REQUIRE(opencmw::thread::detail::getEnumPolicy(SCHED_RR) == opencmw::thread::ROUND_ROBIN);
    REQUIRE(opencmw::thread::detail::getEnumPolicy(SCHED_OTHER) == opencmw::thread::OTHER);
}

TEST_CASE("ThreadSchedulingParameter", "[ThreadAffinity]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("ThreadAffinity - ThreadSchedParameter", 40);

    std::atomic<bool>     run         = true;
    const auto            dummyAction = [&run]() { while (run) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); } };
    std::thread           testThread(dummyAction);
    std::thread           bogusThread;

    using namespace opencmw::thread;
    struct SchedulingParameter param = getThreadSchedulingParameter(testThread);
    REQUIRE(param.policy == OTHER);
    REQUIRE(param.priority == 0);

    REQUIRE_NOTHROW(setThreadSchedulingParameter(OTHER, 0, testThread));
    REQUIRE_NOTHROW(setThreadSchedulingParameter(OTHER, 0));
    REQUIRE_THROWS_AS(setThreadSchedulingParameter(OTHER, 0, bogusThread), std::system_error);
    REQUIRE_THROWS_AS(setThreadSchedulingParameter(OTHER, 4, testThread), std::system_error);
    REQUIRE_THROWS_AS(setThreadSchedulingParameter(OTHER, 4), std::system_error);
    REQUIRE_THROWS_AS(setThreadSchedulingParameter(ROUND_ROBIN, 5, testThread), std::system_error); // missing rights -- because most users do not have CAP_SYS_NICE rights by default -- hard to unit-test
    REQUIRE_THROWS_AS(setThreadSchedulingParameter(ROUND_ROBIN, 5), std::system_error);             // missing rights -- because most users do not have CAP_SYS_NICE rights by default -- hard to unit-test
    param = getThreadSchedulingParameter(testThread);
    REQUIRE(param.policy == OTHER);

    REQUIRE_THROWS_AS(getThreadSchedulingParameter(bogusThread), std::system_error);
    REQUIRE_THROWS_AS(setThreadSchedulingParameter(ROUND_ROBIN, 5, bogusThread), std::system_error);

    run = false;
    testThread.join();
}