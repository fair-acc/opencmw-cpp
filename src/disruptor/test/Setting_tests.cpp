#include <catch2/catch.hpp>

#include <BasicSetting.hpp>

TEST_CASE("BasicSetting Real-Time tests", "[BasicSetting]") {
    using namespace opencmw;
    using opencmw::MutableOption::RealTimeMutable;
    BasicSetting<int, RealTimeMutable> setting;

    SECTION("setting value in real-time thread") {
        auto guardedValue = setting.accessGuard<AccessType::RealTime>();
        REQUIRE(guardedValue == 0);
        REQUIRE(!std::is_const_v<std::remove_reference_t<decltype(guardedValue.get())>>);
        guardedValue = 40;
        guardedValue += 2;
        REQUIRE(guardedValue == 42);
    }
    setting.replace<AccessType::RealTime>(43);

    SECTION("getting value in non-real-time thread") {
        auto guardedValue = setting.accessGuard<AccessType::NonRealTime>();
        REQUIRE(guardedValue == 43);
        REQUIRE(std::is_const_v<std::remove_reference_t<decltype(guardedValue.get())>>);
        // guardedValue = 43; // should not compile
        REQUIRE(guardedValue == 43);
    }
}

TEST_CASE("BasicSetting Non-Real-Time tests", "[BasicSetting]") {
    using namespace opencmw;
    using opencmw::MutableOption::NonRealTimeMutable;
    BasicSetting<int, NonRealTimeMutable> setting;

    SECTION("setting value in real-time thread") {
        auto guardedValue = setting.accessGuard<AccessType::NonRealTime>();
        REQUIRE(guardedValue == 0);
        REQUIRE(!std::is_const_v<std::remove_reference_t<decltype(guardedValue.get())>>);
        guardedValue = 42;
        REQUIRE(guardedValue == 42);
    }

    setting.replace<AccessType::NonRealTime>(43);

    SECTION("getting value in non-real-time thread") {
        auto guardedValue = setting.accessGuard<AccessType::RealTime>();
        REQUIRE(guardedValue == 43);
        REQUIRE(std::is_const_v<std::remove_reference_t<decltype(guardedValue.get())>>);
        // guardedValue = 43; // should not compile
        REQUIRE(guardedValue == 43);
    }
}
