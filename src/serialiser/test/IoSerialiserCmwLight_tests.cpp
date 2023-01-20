#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#include <catch2/catch.hpp>

#include <iostream>
#include <string_view>

#include <Debug.hpp>
#include <IoSerialiserCmwLight.hpp>

using namespace std::literals;

using opencmw::Annotated;
using opencmw::NoUnit;
using opencmw::ProtocolCheck;
using opencmw::ExternalModifier::RO;
using opencmw::ExternalModifier::RW;
using opencmw::ExternalModifier::RW_DEPRECATED;
using opencmw::ExternalModifier::RW_PRIVATE;

namespace ioserialiser_cmwlight_test {
struct SimpleTestData {
    int                             a   = 1337;
    float                           ab  = 13.37f;
    double                          abc = 42.23;
    std::string                     b   = "hello";
    std::array<int, 3>              c{ 3, 2, 1 };
    std::vector<double>             cd{ 2.3, 3.4, 4.5, 5.6 };
    std::vector<std::string>        ce{ "hello", "world" };
    opencmw::MultiArray<double, 2>  d{ { 1, 2, 3, 4, 5, 6 }, { 2, 3 } };
    std::unique_ptr<SimpleTestData> e = nullptr;
    std::set<std::string>           f{ "one", "two", "three" };
    bool                            operator==(const ioserialiser_cmwlight_test::SimpleTestData &other) const { // deep comparison function
        return a == other.a && ab == other.ab && abc == other.abc && b == other.b && c == other.c && cd == other.cd && d == other.d && ((!e && !other.e) || *e == *(other.e)) && f == other.f;
    }
};
} // namespace ioserialiser_cmwlight_test
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestData, a, ab, abc, b, c, cd, ce, d, e, f)

TEST_CASE("IoClassSerialiserCmwLight simple test", "[IoClassSerialiser]") {
    using namespace opencmw;
    using namespace ioserialiser_cmwlight_test;
    debug::resetStats();
    {
        debug::Timer timer("IoClassSerialiser basic syntax", 30);

        IoBuffer     buffer;
        std::cout << fmt::format("buffer size (before): {} bytes\n", buffer.size());

        SimpleTestData data{
            .a   = 30,
            .ab  = 1.2f,
            .abc = 1.23,
            .b   = "abc",
            .c   = { 5, 4, 3 },
            .cd  = { 2.1, 4.2 },
            .ce  = { "hallo", "welt" },
            .d   = { { 6, 5, 4, 3, 2, 1 }, { 3, 2 } },
            .e   = std::make_unique<SimpleTestData>(SimpleTestData{
                      .a   = 40,
                      .ab  = 2.2f,
                      .abc = 2.23,
                      .b   = "abcdef",
                      .c   = { 9, 8, 7 },
                      .cd  = { 3.1, 1.2 },
                      .ce  = { "ei", "gude" },
                      .d   = { { 6, 5, 4, 3, 2, 1 }, { 3, 2 } },
                      .e   = nullptr }),
            .f   = { "four", "five" }
        };

        // check that empty buffer cannot be deserialised
        buffer.put(0L);
        CHECK_THROWS_AS((opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data)), ProtocolException);
        buffer.clear();

        SimpleTestData data2;
        REQUIRE(data != data2);
        std::cout << "object (short): " << ClassInfoShort << data << '\n';
        std::cout << fmt::format("object (fmt): {}\n", data);
        std::cout << "object (long):  " << ClassInfoVerbose << data << '\n';

        opencmw::serialise<opencmw::CmwLight>(buffer, data);
        std::cout << fmt::format("buffer size (after): {} bytes\n", buffer.size());

        buffer.put("a\0df"sv); // add some garbage after the serialised object to check if it is handled correctly

        buffer.reset();
        auto result = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data2);
        std::cout << "deserialised object (long):  " << ClassInfoVerbose << data2 << '\n';
        std::cout << "deserialisation messages: " << result << std::endl;
        REQUIRE(data == data2);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

namespace ioserialiser_cmwlight_test {
struct SimpleTestDataMoreFields {
    int                             a2   = 1336;
    float                           ab2  = 13.36f;
    double                          abc2 = 42.22;
    std::string                     b2   = "bonjour";
    std::array<int, 3>              c2{ 7, 8, 9 };
    std::vector<double>             cd2{ 2.4, 3.6, 4.8, 5.0 };
    std::vector<std::string>        ce2{ "hello", "world" };
    opencmw::MultiArray<double, 2>  d2{ { 4, 5, 6, 7, 8, 9 }, { 2, 3 } };
    std::unique_ptr<SimpleTestData> e2  = nullptr;
    int                             a   = 1337;
    float                           ab  = 13.37f;
    double                          abc = 42.23;
    std::string                     b   = "hello";
    std::array<int, 3>              c{ 3, 2, 1 };
    std::vector<double>             cd{ 2.3, 3.4, 4.5, 5.6 };
    std::vector<std::string>        ce{ "hello", "world" };
    opencmw::MultiArray<double, 2>  d{ { 1, 2, 3, 4, 5, 6 }, { 2, 3 } };
    bool                            operator==(const SimpleTestDataMoreFields &) const = default;
    std::unique_ptr<SimpleTestData> e                                                  = nullptr;
    std::set<std::string>           f{ "one", "two", "three" };
};
} // namespace ioserialiser_cmwlight_test
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestDataMoreFields, a2, ab2, abc2, b2, c2, cd2, ce2, d2, e2, a, ab, abc, b, c, cd, ce, d, e, f)

#pragma clang diagnostic pop
TEST_CASE("IoClassSerialiserCmwLight missing field", "[IoClassSerialiser]") {
    using namespace opencmw;
    using namespace ioserialiser_cmwlight_test;
    debug::resetStats();
    {
        debug::Timer timer("IoClassSerialiser basic syntax", 30);

        IoBuffer     buffer;
        std::cout << fmt::format("buffer size (before): {} bytes\n", buffer.size());

        SimpleTestData data{
            .a   = 30,
            .ab  = 1.2f,
            .abc = 1.23,
            .b   = "abc",
            .c   = { 5, 4, 3 },
            .cd  = { 2.1, 4.2 },
            .ce  = { "hallo", "welt" },
            .d   = { { 6, 5, 4, 3, 2, 1 }, { 3, 2 } },
            .f   = { "four", "six" }
        };
        SimpleTestDataMoreFields data2;
        std::cout << fmt::format("object (fmt): {}\n", data);
        opencmw::serialise<opencmw::CmwLight>(buffer, data);
        buffer.reset();
        auto result = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data2);
        std::cout << fmt::format("deserialised object (fmt): {}\n", data2);
        std::cout << "deserialisation messages: " << result << std::endl;
        REQUIRE(result.setFields["root"] == std::vector<bool>{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1 });
        REQUIRE(result.additionalFields.empty());
        REQUIRE(result.exceptions.empty());

        std::cout << "\nand now the other way round!\n\n";
        buffer.clear();
        opencmw::serialise<opencmw::CmwLight>(buffer, data2);
        buffer.reset();
        auto result_back = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data);
        std::cout << fmt::format("deserialised object (fmt): {}\n", data);
        std::cout << "deserialisation messages: " << result_back << std::endl;
        REQUIRE(result_back.setFields["root"] == std::vector<bool>{ 1, 1, 1, 1, 1, 1, 1, 1, 0, 1 });
        REQUIRE(result_back.additionalFields.size() == 8);
        REQUIRE(result_back.exceptions.size() == 8);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}
