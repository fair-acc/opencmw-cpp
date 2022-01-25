#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"

#include <catch2/catch.hpp>
#include <Debug.hpp>
#include <IoSerialiserJson.hpp>
#include <iostream>
#include <string_view>
#include <Utils.hpp>

#include <units/isq/si/length.h>
#include <units/isq/si/speed.h>

using namespace opencmw;
using namespace units::isq;
using namespace units::isq::si;
using NoUnit = units::dimensionless<units::one>;
using namespace std::literals;
using namespace std::string_view_literals;

struct DataX {
    bool                           boolValue   = true;
    int8_t                         byteValue   = 1;
    int16_t                        shortValue  = 2;
    int32_t                        intValue    = 3;
    int64_t                        longValue   = 4;
    float                          floatValue  = 5.0F;
    double                         doubleValue = 6.0;
    std::string                    stringValue = "default";
    std::array<double, 10>         doubleArray = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    std::vector<float>             floatVector = { 0.1F, 1.1F, 2.1F, 3.1F, 4.1F, 5.1F, 6.1F, 8.1F, 9.1F, 9.1F };
    opencmw::MultiArray<double, 2> doubleMatrix{ { 1, 3, 7, 4, 2, 3 }, { 2, 3 } };
    std::map<std::string, double>  doubleMap{ std::pair<std::string, double>{ "hello", 42.23 }, std::pair<std::string, double>{ "map", 1.337 } };
    std::shared_ptr<DataX>         nested;

    DataX() = default;
    bool operator==(const DataX &other) const { // deep comparison function
        return boolValue == other.boolValue && byteValue == other.byteValue && shortValue == other.shortValue && intValue == other.intValue && longValue == other.longValue && floatValue == other.floatValue && doubleValue == other.doubleValue && stringValue == other.stringValue && doubleArray == other.doubleArray && floatVector == other.floatVector && doubleMatrix == other.doubleMatrix && ((!nested && !other.nested) || *nested == *(other.nested));
    }
};
ENABLE_REFLECTION_FOR(DataX, boolValue, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, doubleArray, floatVector, doubleMatrix, doubleMap, nested)

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
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(R"({ "float1": 2.3, "test": { "intArray": [1, 2, 3], "val1":13.37e2, "val2":"bar"}, "int1": 42})"sv);
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

struct SimpleId {
    int id;
};
ENABLE_REFLECTION_FOR(SimpleId, id)

TEST_CASE("JsonDeserialisationInvalidJson", "[JsonSerialiser]") {
    opencmw::debug::resetStats();

    constexpr std::array testCases = {
        ""sv,
        R"({ "id" : 42 ])"sv,
        // R"({ "id" : 42)"sv, // TODO currently doesn't throw, ok or not?
        R"(}{)"
        ")"sv
    };

    for (const auto &testCase : testCases) {
        opencmw::IoBuffer buffer;
        std::cout << "Prepared json data: " << buffer.asString() << std::endl;
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(testCase);
        SimpleId foo;
        foo.id = 0;
        REQUIRE_THROWS((opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::ALWAYS>(buffer, foo)));
    }
}

TEST_CASE("JsonDeserialisationMissingField", "[JsonSerialiser]") {
    opencmw::debug::resetStats();
    {
        opencmw::IoBuffer buffer;
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(R"({ "float1": 2.3, "superfluousField": { "p":12 , "a":null,"x" : false, "q": [ "a", "s"], "z": [true , false ] },  "test": { "intArray" : [ 1,2, 3], "val1":13.37e2, "val2":"bar"}, "int1": 42})"sv);
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
    {
        std::vector<int>  test{ 1, 3, 3, 7 };
        opencmw::IoBuffer buffer;
        opencmw::IoSerialiser<opencmw::Json, std::vector<int>>::serialise(buffer, detail::newFieldHeader<opencmw::Json>(buffer, "test", 0, test, 0), test);
        REQUIRE(buffer.asString() == "[1, 3, 3, 7]");
        {
            std::vector<int> result;
            opencmw::IoSerialiser<opencmw::Json, std::vector<int>>::deserialise(buffer, detail::newFieldHeader<opencmw::Json>(buffer, "test", 0, result, 0), result);
            REQUIRE(test == result);
            REQUIRE(buffer.position() == 12);
        }
        buffer.set_position(0);
        {
            std::array<int, 4> resultArray;
            opencmw::IoSerialiser<opencmw::Json, std::array<int, 4>>::deserialise(buffer, detail::newFieldHeader<opencmw::Json>(buffer, "test", 0, resultArray, 0), resultArray);
            REQUIRE(test == std::vector<int>(resultArray.begin(), resultArray.end()));
            REQUIRE(buffer.position() == 12);
        }
    }
    { // empty vector
        std::vector<int>  test{};
        opencmw::IoBuffer buffer;
        opencmw::IoSerialiser<opencmw::Json, std::vector<int>>::serialise(buffer, detail::newFieldHeader<opencmw::Json>(buffer, "test", 0, test, 0), test);
        REQUIRE(buffer.asString() == "[]");
        std::vector<int> result;
        opencmw::IoSerialiser<opencmw::Json, std::vector<int>>::deserialise(buffer, detail::newFieldHeader<opencmw::Json>(buffer, "test", 0, result, 0), result);
        REQUIRE(test == result);
        REQUIRE(buffer.position() == 2);
    }
}

TEST_CASE("JsonSerialisation", "[JsonSerialiser]") {
    opencmw::debug::resetStats();
    std::cout << opencmw::utils::ClassInfoVerbose;
    {
        opencmw::IoBuffer buffer;
        DataX             foo;
        foo.doubleValue               = 42.23;
        foo.stringValue               = "test";
        foo.nested                    = std::make_shared<DataX>();
        foo.doubleMatrix[{ 1U, 1U }]  = 42.1337;
        foo.nested.get()->stringValue = "asdf";
        foo.nested.get()->doubleMap.emplace("key", 99.99);
        opencmw::serialise<opencmw::Json>(buffer, foo);
        std::cout << "serialised: " << buffer.asString() << std::endl;
        DataX bar;
        auto  result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, bar);
        opencmw::utils::diffView(std::cout, foo, bar);
        fmt::print(std::cout, "deserialisation finished: {}\n", result);
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
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(R"""("test String 123 " sfef)"""sv);
        REQUIRE(readString(buffer) == R"""(test String 123 )""");
    }
    {
        IoBuffer buffer;
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>("\"Hello\t\\\"special\\\"\nWorld!\""sv);
        REQUIRE(readString(buffer) == "Hello\t\"special\"\nWorld!");
    }
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

TEST_CASE("JsonSkipValue", "[JsonSerialiser]") {
    opencmw::debug::resetStats();
    {
        opencmw::IoBuffer buffer;
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(R"({ "float1": 2.3, "superfluousField": {"p": 12, "q": [ "a", "s"]}, "test": { "intArray": [1, 2, 3], "val1":13.37e2, "val2":"bar"}, "int1": 42})"sv);
        // skip whole object
        opencmw::json::skipValue(buffer);
        REQUIRE(buffer.position() == buffer.size()); // check that the whole object was skipped
        // skip "test" field
        buffer.set_position(65);
        opencmw::json::skipField(buffer);
        REQUIRE(buffer.position() == 128); // check that the subfield was skipped correctly
    }
}

TEST_CASE("consumeWhitespace", "[JsonSerialiser]") {
    using namespace opencmw;
    using namespace opencmw::json;
    IoBuffer buffer;
    buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>("\n \t345 \r \t\tbcdef"sv);
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