#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"

#include <Debug.hpp>
#include <IoClassSerialiser.hpp>
#include <Utils.hpp>
#include <catch2/catch.hpp>
#include <iostream>
#include <string_view>

#include <units/isq/si/length.h>
#include <units/isq/si/speed.h>

using namespace units::isq;
using namespace units::isq::si;
using NoUnit = units::dimensionless<units::one>;
using namespace std::literals;

struct DataX {
    int8_t                         byteValue        = 1;
    int16_t                        shortValue       = 2;
    int32_t                        intValue         = 3;
    int64_t                        longValue        = 4;
    float                          floatValue       = 5.0F;
    double                         doubleValue      = 6.0;
    std::string                    stringValue      = "bare string";
    std::string const              constStringValue = "unmodifiable string";
    std::array<double, 10>         doubleArray      = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    std::array<double, 10> const   constDoubleArray = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    std::vector<float>             floatVector      = { 0.1F, 1.1F, 2.1F, 3.1F, 4.1F, 5.1F, 6.1F, 8.1F, 9.1F, 9.1F };
    opencmw::MultiArray<double, 2> doubleMatrix{ { 1, 3, 7, 4, 2, 3 }, { 2, 3 } };
    std::shared_ptr<DataX>         nested;

    DataX()                              = default;
    bool operator==(const DataX &) const = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(DataX, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, constStringValue, doubleArray, floatVector, doubleMatrix, nested)

TEST_CASE("JsonDeserialisation", "[JsonClassSerialiser]") {
    opencmw::debug::resetStats();
    {
        opencmw::IoBuffer buffer;
        auto              cars_json = R"({ "test":[ { "val1":1, "val2":2 }, { "val1":1, "val2":2 } ] })";
        buffer.reserve_spare(strlen(cars_json));
        buffer.putRaw(cars_json);
        std::cout << "Prepared json data: " << buffer.asString() << std::endl;
        DataX foo;
        auto  result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, foo);
        std::cout << "deserialised: \n";
        for (auto e : result.exceptions) {
            std::cout << " ! " << e.what() << std::endl;
        }
        for (auto f : result.additionalFields) {
            std::cout << " + " << std::get<0>(f) << std::endl;
        }
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

TEST_CASE("JsonSerialisation", "[JsonClassSerialiser]") {
    opencmw::debug::resetStats();
    {
        opencmw::IoBuffer buffer;
        DataX             foo;
        foo.doubleValue = 42.23;
        foo.nested      = std::make_shared<DataX>();
        opencmw::serialise<opencmw::Json>(buffer, foo);
        std::cout << "serialised: " << buffer.asString() << std::endl;
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

#pragma clang diagnostic pop