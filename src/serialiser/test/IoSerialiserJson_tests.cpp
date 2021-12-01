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
    int8_t  byteValue   = 1;
    int16_t shortValue  = 2;
    int32_t intValue    = 3;
    int64_t longValue   = 4;
    float   floatValue  = 5.0F;
    double  doubleValue = 6.0;
    // std::string            stringValue;
    std::array<double, 10> doubleArray = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    // std::vector<float>             floatVector      = { 0.1F, 1.1F, 2.1F, 3.1F, 4.1F, 5.1F, 6.1F, 8.1F, 9.1F, 9.1F };
    // opencmw::MultiArray<double, 2> doubleMatrix{ { 1, 3, 7, 4, 2, 3 }, { 2, 3 } };
    std::shared_ptr<DataX> nested;

    DataX()                              = default;
    bool operator==(const DataX &) const = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(DataX, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, /*stringValue,*/ doubleArray, /*floatVector,*/ nested)

struct SimpleInner {
    double           val1;
    std::string      val2;
    std::vector<int> intArray;
};
ENABLE_REFLECTION_FOR(SimpleInner, val1, val2, intArray)
struct Simple {
    float                        float1;
    int                          int1;
    std::shared_ptr<SimpleInner> test;
};
ENABLE_REFLECTION_FOR(Simple, float1, int1, test)

TEST_CASE("JsonDeserialisation", "[JsonSerialiser]") {
    opencmw::debug::resetStats();
    {
        opencmw::IoBuffer buffer;
        buffer.putRaw(R"({ "float1": 2.3, "test": { "intArray": [1, 2, 3], "val1":13.37e2, "val2":"bar"}, "int1": 42})");
        std::cout << "Prepared json data: " << buffer.asString() << std::endl;
        Simple foo;
        auto   result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, foo);
        fmt::print(std::cout, "deserialisation finished: {}\n", result);
        REQUIRE(foo.test.get()->val1 == 1337.0);
        REQUIRE(foo.test.get()->val2 == "bar");
        REQUIRE(foo.test.get()->intArray == std::vector{ 1, 2, 3 });
        REQUIRE(foo.int1 == 42);
        REQUIRE(foo.float1 == 2.3f);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

TEST_CASE("JsonDeserialisationMissingField", "[JsonSerialiser]") {
    opencmw::debug::resetStats();
    {
        opencmw::IoBuffer buffer;
        buffer.putRaw(R"({ "float1": 2.3, "superfluousField": {"p": 12, "q": [ "a", "s"]}, "test": { "intArray": [1, 2, 3], "val1":13.37e2, "val2":"bar"}, "int1": 42})");
        std::cout << "Prepared json data: " << buffer.asString() << std::endl;
        Simple foo;
        auto   result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, foo);
        fmt::print(std::cout, "deserialisation finished: {}\n", result);
        REQUIRE(foo.test.get()->val1 == 1337.0);
        REQUIRE(foo.test.get()->val2 == "bar");
        REQUIRE(foo.test.get()->intArray == std::vector{ 1, 2, 3 });
        REQUIRE(foo.int1 == 42);
        REQUIRE(foo.float1 == 2.3f);
        REQUIRE(result.additionalFields.size() == 1);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

TEST_CASE("JsonArraySerialisation", "[JsonSerialiser]") {
    std::vector<int>  test{ 1, 3, 3, 7 };
    opencmw::IoBuffer buffer;
    opencmw::IoSerialiser<opencmw::Json, std::vector<int>>::serialise(buffer, "test", test);
    REQUIRE(buffer.asString() == "[1, 3, 3, 7]");
    {
        std::vector<int> result;
        opencmw::IoSerialiser<opencmw::Json, std::vector<int>>::deserialise(buffer, "test", result);
        REQUIRE(test == result);
    }
    buffer.set_position(0);
    {
        std::array<int, 4> resultArray;
        opencmw::IoSerialiser<opencmw::Json, std::array<int, 4>>::deserialise(buffer, "test", resultArray);
        REQUIRE(test == std::vector<int>(resultArray.begin(), resultArray.end()));
    }
}

TEST_CASE("JsonSerialisation", "[JsonSerialiser]") {
    opencmw::debug::resetStats();
    std::cout << opencmw::utils::ClassInfoVerbose;
    {
        opencmw::IoBuffer buffer;
        DataX             foo;
        foo.doubleValue = 42.23;
        // foo.stringValue = "test";
        //foo.nested = std::make_shared<DataX>();
        //foo.nested.get()->stringValue = "asdf";
        opencmw::serialise<opencmw::Json>(buffer, foo);
        std::cout << "serialised: " << buffer.asString() << std::endl;
        DataX bar;
        auto  result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, bar);
        //opencmw::utils::diffView(std::cout, foo, bar); // todo: produces SEGFAULT
        fmt::print(std::cout, "deserialisation finished: {}\n", result);
        REQUIRE(foo.doubleValue == bar.doubleValue);
        REQUIRE(foo.doubleArray == bar.doubleArray);
        REQUIRE(foo.floatValue == bar.floatValue);
        REQUIRE(foo.intValue == bar.intValue);
        // REQUIRE(foo.stringValue == bar.stringValue);
        REQUIRE(foo.byteValue == bar.byteValue);
        REQUIRE(foo.shortValue == bar.shortValue);
        REQUIRE(foo == bar);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

TEST_CASE("isNumber", "[JsonSerialiser]") {
    using namespace opencmw::json;
    REQUIRE(isNumberChar('0'));
    REQUIRE(isNumberChar('1'));
    REQUIRE(isNumberChar('2'));
    REQUIRE(isNumberChar('3'));
    REQUIRE(isNumberChar('4'));
    REQUIRE(isNumberChar('5'));
    REQUIRE(isNumberChar('6'));
    REQUIRE(isNumberChar('7'));
    REQUIRE(isNumberChar('8'));
    REQUIRE(isNumberChar('9'));
    REQUIRE(isNumberChar('+'));
    REQUIRE(isNumberChar('-'));
    REQUIRE(isNumberChar('e'));
    REQUIRE(isNumberChar('E'));
    REQUIRE(isNumberChar('.'));
    REQUIRE_FALSE(isNumberChar('a'));
}
TEST_CASE("readString", "[JsonSerialiser]") {
    using namespace opencmw;
    using namespace opencmw::json;
    {
        IoBuffer buffer;
        buffer.putRaw(R"""("test String 123 " sfef)""");
        REQUIRE(readString(buffer) == R"""(test String 123 )""");
    }
    // todo: make escape sequences work properly, we cannot return string views but have to copy into our own buffer
    // for now we focus on non escaped strings first
    // {
    //     IoBuffer buffer;
    //     buffer.putRaw("\"Hello\t\\\"special\\\"\nWorld!\"");
    //     REQUIRE(readString(buffer) == "Hello\t\"special\"\nWorld!");
    // }
}

TEST_CASE("isWhitespace", "[JsonSerialiser]") {
    using namespace opencmw;
    using namespace opencmw::json;
    REQUIRE(isWhitespace(' '));
    REQUIRE(isWhitespace('\t'));
    REQUIRE(isWhitespace('\n'));
    REQUIRE(isWhitespace('\r'));
    REQUIRE_FALSE(isWhitespace('a'));
}

TEST_CASE("consumeWhitespace", "[JsonSerialiser]") {
    using namespace opencmw;
    using namespace opencmw::json;
    IoBuffer buffer;
    buffer.putRaw("\n \t345 \r \t\tbcdef");
    REQUIRE(buffer.position() == 0);
    consumeWhitespace(buffer);
    REQUIRE(buffer.position() == 3);
    buffer.set_position(5);
    consumeWhitespace(buffer);
    REQUIRE(buffer.position() == 5);
    buffer.set_position(6);
    consumeWhitespace(buffer);
    REQUIRE(buffer.position() == 11);
}

#pragma clang diagnostic pop