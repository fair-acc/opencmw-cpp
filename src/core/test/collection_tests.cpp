#define CATCH_CONFIG_ENABLE_BENCHMARKING 1
#include <catch2/catch.hpp>

#include <atomic>
#include <iostream>

#include "collection.hpp"

TEST_CASE("basic tests", "[collection]") {
    using opencmw::collection;

    collection<size_t, float, char> testCollection1;
    REQUIRE_NOTHROW(testCollection1.push_back(1LU));
    REQUIRE_NOTHROW(testCollection1.push_back(1.0f));
    // REQUIRE_NOTHROW(testCollection1.push_back(1.0)); // should not compile (wrong type)
    REQUIRE_NOTHROW(testCollection1.push_back('c'));

    std::atomic<int> counter1{ 0 };
    std::atomic<int> counter2{ 0 };
    std::atomic<int> counter3{ 0 };
    std::atomic<int> counter4{ 0 };
    testCollection1.visit(
            [&counter1](const size_t &arg) noexcept { counter1++; REQUIRE(arg == 1LU); },
            [&counter2](const float &arg) noexcept { counter2++; REQUIRE(arg == 1.0f); },
            [&counter3](const char &arg) noexcept { counter3++; REQUIRE(arg == 'c'); },
            [&counter4]<typename T>(const T &arg) noexcept { counter4++; arg *= 1; });
    REQUIRE(counter1 == 1);
    REQUIRE(counter2 == 1);
    REQUIRE(counter3 == 1);
    REQUIRE(counter4 == 0); // N.B. should not be called
    testCollection1.visit([&counter4]<typename T>(T &arg) noexcept { counter4++; arg *= 1; });
    REQUIRE(counter4 == 3); // default fall-back

    counter4.store(0);
    collection testCollection2(1, 1.0f, "Hello", "World!", std::vector<int>(2));
    testCollection2.visit([&counter4]<typename T>(T &) noexcept { counter4++; });
    REQUIRE(counter4 == 5); // default fall-back

    collection testCollection3(testCollection2);
    REQUIRE(testCollection3 == testCollection2);
    testCollection2.push_back(1.0f);
    REQUIRE(testCollection3 != testCollection2);

    collection testCollection4 = testCollection2;
    REQUIRE(testCollection4 == testCollection2);
    testCollection2.push_back(1.0f);
    REQUIRE(testCollection4 != testCollection2);

    auto testCollection5 = collection{ 1LU, 1.0f, 'c' };
    REQUIRE(testCollection5 == testCollection1);
    testCollection1.push_back('b');
    REQUIRE(testCollection5 != testCollection1);
    REQUIRE(testCollection5.size() != testCollection1.size());
    REQUIRE_NOTHROW(testCollection1.remove('b'));
    REQUIRE_NOTHROW(testCollection1.remove('x'));
    REQUIRE(testCollection5.size() == testCollection1.size());
    REQUIRE(testCollection5 == testCollection1);

    testCollection1.push_back('b');
    auto testCollection6 = collection{ 1LU, 1.0f, 'c', 'b' };
    REQUIRE(testCollection6 == testCollection1);
    testCollection1.push_back('d');
    REQUIRE(testCollection6 != testCollection1);

    collection testCollection7 = { 1LU, 1.0f, 'c', 'b' };
    REQUIRE(testCollection7.size() == 4);
    REQUIRE(!testCollection7.empty());
    testCollection7.clear();
    REQUIRE(testCollection7.size() == 0);
    REQUIRE(testCollection7.empty());

    REQUIRE_NOTHROW(testCollection7.reserve(100));
    REQUIRE(testCollection7.capacity() == 100);

    counter4.store(0);
    collection testCollection8(1, 1U, static_cast<short>(42));
    // test concept constraint
    auto testFunction = [&counter4]<std::integral... Ts>(collection<Ts...> &integralCollection) {
        integralCollection.visit([&counter4]<std::integral T>(const T &) noexcept {
            REQUIRE(std::is_integral_v<T>);
            counter4++; });
    };
    testFunction(testCollection8);
    REQUIRE(counter4 == 3);
}

TEST_CASE("mini collection benchmark", "[!benchmark]") {
    auto miniTest = []() {
        auto                time_start = std::chrono::system_clock::now();
        opencmw::collection c{ 2UL, 1.0f, 'a' };
        c.visit([](size_t &arg) noexcept { arg *= 1; },
                [](float &arg) noexcept { arg *= 1; },
                [](char &arg) noexcept { arg *= 1; },
                []<typename T>(T &arg) noexcept { arg *= 1; });
        return std::chrono::system_clock::now() - time_start;
    };
    BENCHMARK("collection") {
        return miniTest();
    };
}