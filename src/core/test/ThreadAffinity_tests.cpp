#include <catch2/catch.hpp>
#include <Debug.hpp>
#include <fmt/chrono.h>
#include <ThreadAffinity.hpp>

#define REQUIRE_MESSAGE(cond, msg) \
    do { \
        INFO(msg); \
        REQUIRE(cond); \
    } while ((void) 0, 0)

std::jthread testTimeoutGuard(const std::chrono::milliseconds &timeout, std::atomic<bool> &run) {
    return std::jthread([&]() {
        int count = 10;
        while (count-- > 0 && run) {
            std::this_thread::sleep_for(timeout / 10);
        }
        const bool stateAfter = run;
        run                   = false;
        REQUIRE_MESSAGE(!stateAfter, fmt::format("TEST_CASE(\"{}\", ..) expired after {} ", Catch::getResultCapture().getCurrentTestName(), timeout));
    });
}

TEST_CASE("basic thread affinity", "[ThreadAffinity]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("ThreadAffinity - basic thread affinity", 40);

    using namespace opencmw;
    std::atomic<bool>    run         = true;
    auto                 guard       = testTimeoutGuard(std::chrono::milliseconds(1000), run);
    const auto           dummyAction = [&run]() { while (run) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); } };
    std::jthread         testThread(dummyAction);

    constexpr std::array threadMap = { true, false, false, false };
    thread::setThreadAffinity(threadMap, testThread);
    auto affinity = thread::getThreadAffinity(testThread);
    bool equal    = true;
    for (size_t i = 0; i < std::min(threadMap.size(), affinity.size()); i++) {
        if (threadMap[i] != affinity[i]) {
            equal = false;
        }
    }
    REQUIRE_MESSAGE(equal, fmt::format("set {{{}}} affinity map does not match get {{{}}} map", fmt::join(threadMap, ", "), fmt::join(affinity, ", ")));

    // tests w/o thread argument
    constexpr std::array threadMapOn = { true, true };
    thread::setThreadAffinity(threadMapOn);
    affinity = thread::getThreadAffinity();
    for (size_t i = 0; i < std::min(threadMapOn.size(), affinity.size()); i++) {
        if (threadMapOn[i] != affinity[i]) {
            equal = false;
        }
    }
    REQUIRE_MESSAGE(equal, fmt::format("set {{{}}} affinity map does not match get {{{}}} map", fmt::join(threadMap, ", "), fmt::join(affinity, ", ")));

    std::jthread bogusThread;
    REQUIRE_THROWS_AS(thread::getThreadAffinity(bogusThread), std::system_error);
    REQUIRE_THROWS_AS(thread::setThreadAffinity(threadMapOn, bogusThread), std::system_error);

    run = false;
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
    REQUIRE_MESSAGE(equal, fmt::format("set {{{}}} affinity map does not match get {{{}}} map", fmt::join(threadMap, ", "), fmt::join(affinity, ", ")));
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
    auto              guard       = testTimeoutGuard(std::chrono::milliseconds(1000), run);
    const auto        dummyAction = [&run]() { while (run) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); } };
    std::jthread      testThread(dummyAction);
    REQUIRE("" != thread::getThreadName(testThread));
    REQUIRE_NOTHROW(thread::setThreadName("testThreadName", testThread));
    thread::setThreadName("testThreadName", testThread);
    REQUIRE("testThreadName" == thread::getThreadName(testThread));

    std::jthread uninitialisedTestThread;
    REQUIRE_THROWS_AS(thread::getThreadName(uninitialisedTestThread), std::system_error);
    REQUIRE_THROWS_AS(thread::setThreadName("name", uninitialisedTestThread), std::system_error);
    run = false;
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

    REQUIRE_NOTHROW(setProcessSchedulingParameter(OTHER, 0));
    REQUIRE_THROWS_AS(setProcessSchedulingParameter(OTHER, 0, -1), std::system_error);
    REQUIRE_THROWS_AS(setProcessSchedulingParameter(OTHER, 4), std::system_error);
    REQUIRE_THROWS_AS(setProcessSchedulingParameter(ROUND_ROBIN, 5), std::system_error); // missing rights -- because most users do not have CAP_SYS_NICE rights by default -- hard to unit-test
    param = getProcessSchedulingParameter();
    REQUIRE(param.policy == OTHER);

    REQUIRE_THROWS_AS(getProcessSchedulingParameter(-1), std::system_error);
    REQUIRE_THROWS_AS(setProcessSchedulingParameter(ROUND_ROBIN, 5, -1), std::system_error);
}

TEST_CASE("ThreadSchedulingParameter", "[ThreadAffinity]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("ThreadAffinity - ThreadSchedParameter", 40);

    std::atomic<bool> run         = true;
    auto              guard       = testTimeoutGuard(std::chrono::milliseconds(1000), run);
    const auto        dummyAction = [&run]() { while (run) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); } };
    std::jthread      testThread(dummyAction);
    std::jthread      bogusThread;

    using namespace opencmw::thread;
    struct SchedulingParameter param = getThreadSchedulingParameter(testThread);
    REQUIRE(param.policy == OTHER);

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
}