#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"

#include <Debug.hpp>
#include <IoSerialiserJson.hpp>
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

struct SimpleInner {
    std::string val1; std::string val2;
};
ENABLE_REFLECTION_FOR(SimpleInner, val1, val2)
struct Simple {
    std::shared_ptr<SimpleInner> test;
};
ENABLE_REFLECTION_FOR(Simple, test)

TEST_CASE("JsonDeserialisation", "[JsonSerialiser]") {
    opencmw::debug::resetStats();
    {
        opencmw::IoBuffer buffer;
        auto cars_json = R"({ "test": { "val1":"foo", "val2":"bar"}})";
        buffer.reserve_spare(strlen(cars_json));
        buffer.putRaw(cars_json);
        std::cout << "Prepared json data: " << buffer.asString() << std::endl;
        Simple foo;
        auto  result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, foo);
        std::cout << "deserialised: \n";
        for (auto e : result.exceptions) {
            std::cout << " ! " << e.what() << std::endl;
        }
        for (auto f : result.additionalFields) {
            std::cout << " + " << std::get<0>(f) << std::endl;
        }
        REQUIRE(foo.test.get()->val1 == "foo");
        REQUIRE(foo.test.get()->val2 == "bar");
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

TEST_CASE("JsonSerialisation", "[JsonSerialiser]") {
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

TEST_CASE("isNumber", "[JsonSerialiser]") {
    using namespace opencmw::json;
    REQUIRE(isJsonNumberChar('0'));
    REQUIRE(isJsonNumberChar('1'));
    REQUIRE(isJsonNumberChar('2'));
    REQUIRE(isJsonNumberChar('3'));
    REQUIRE(isJsonNumberChar('4'));
    REQUIRE(isJsonNumberChar('5'));
    REQUIRE(isJsonNumberChar('6'));
    REQUIRE(isJsonNumberChar('7'));
    REQUIRE(isJsonNumberChar('8'));
    REQUIRE(isJsonNumberChar('9'));
    REQUIRE(isJsonNumberChar('+'));
    REQUIRE(isJsonNumberChar('-'));
    REQUIRE(isJsonNumberChar('e'));
    REQUIRE(isJsonNumberChar('E'));
    REQUIRE(isJsonNumberChar('.'));
    REQUIRE_FALSE(isJsonNumberChar('a'));
}
TEST_CASE("readJsonString", "[JsonSerialiser]") {
    using namespace opencmw;
    using namespace opencmw::json;
    {
        IoBuffer buffer;
        buffer.putRaw(R"""("test String 123 " sfef)""");
        REQUIRE(readJsonString(buffer) == R"""(test String 123 )""");
    }
    // todo: make escape sequences work properly, we cannot return string views but have to copy into our own buffer
    // for now we focus on non escaped strings first
    // {
    //     IoBuffer buffer;
    //     buffer.putRaw("\"Hello\t\\\"special\\\"\nWorld!\"");
    //     REQUIRE(readJsonString(buffer) == "Hello\t\"special\"\nWorld!");
    // }
}

TEST_CASE("isWhitespace", "[JsonSerialiser]") {
    using namespace opencmw;
    using namespace opencmw::json;
    REQUIRE(isJsonWhitespace(' '));
    REQUIRE(isJsonWhitespace('\t'));
    REQUIRE(isJsonWhitespace('\n'));
    REQUIRE(isJsonWhitespace('\r'));
    REQUIRE_FALSE(isJsonWhitespace('a'));
}

TEST_CASE("consumeWhitespace", "[JsonSerialiser]") {
    using namespace opencmw;
    using namespace opencmw::json;
    IoBuffer buffer;
    buffer.putRaw("\n \t345 \r \t\tbcdef");
    REQUIRE(buffer.position() == 0);
    consumeJsonWhitespace(buffer);
    REQUIRE(buffer.position() == 3);
    buffer.set_position(5);
    consumeJsonWhitespace(buffer);
    REQUIRE(buffer.position() == 5);
    buffer.set_position(6);
    consumeJsonWhitespace(buffer);
    REQUIRE(buffer.position() == 11);
}

#pragma clang diagnostic pop