#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#include <catch2/catch.hpp>

#include <iostream>
#include <string_view>
#include <ranges>

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
    std::map<std::string, int>      g{{"g1", 1}, {"g2", 2}, {"g3", 3}};
    bool                            operator==(const ioserialiser_cmwlight_test::SimpleTestData &o) const { // deep comparison function
        return a == o.a && ab == o.ab && abc == o.abc && b == o.b && c == o.c && cd == o.cd && d == o.d && ((!e && !o.e) || *e == *(o.e)) && f == o.f && g == o.g;
    }
};
struct SimpleTestDataMapNested {
    int g1;
    int g2;
    int g3;

    bool operator==(const SimpleTestDataMapNested &o) const = default;
    bool operator==(const std::map<std::string, int> &o) const {
        return g1 == o.at("g1") && g2 == o.at("g2") && g3 == o.at("g3");
    }
};
struct SimpleTestDataMapAsNested {
    int                             a   = 1337;
    float                           ab  = 13.37f;
    double                          abc = 42.23;
    std::string                     b   = "hello";
    std::array<int, 3>              c{ 3, 2, 1 };
    std::vector<double>             cd{ 2.3, 3.4, 4.5, 5.6 };
    std::vector<std::string>        ce{ "hello", "world" };
    opencmw::MultiArray<double, 2>  d{ { 1, 2, 3, 4, 5, 6 }, { 2, 3 } };
    std::unique_ptr<SimpleTestDataMapAsNested> e = nullptr;
    std::set<std::string>           f{ "one", "two", "three" };
    SimpleTestDataMapNested         g{1, 2, 3};
    bool                            operator==(const ioserialiser_cmwlight_test::SimpleTestDataMapAsNested &o) const { // deep comparison function
        return a == o.a && ab == o.ab && abc == o.abc && b == o.b && c == o.c && cd == o.cd && d == o.d && ((!e && !o.e) || *e == *(o.e)) && f == o.f && g == o.g;
    }
    bool                            operator==(const ioserialiser_cmwlight_test::SimpleTestData &o) const { // deep comparison function
        return a == o.a && ab == o.ab && abc == o.abc && b == o.b && c == o.c && cd == o.cd && d == o.d && ((!e && !o.e) || *e == *(o.e)) && f == o.f && g == o.g;
    }
};
} // namespace ioserialiser_cmwlight_test
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestData, a, ab, abc, b, c, cd, ce, d, e, f, g)
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestDataMapAsNested, a, ab, abc, b, c, cd, ce, d, e, f, g)
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestDataMapNested, g1, g2, g3)

// small utility function that prints the content of a string in the classic hexedit way with address, hexadecimal and ascii representations
static std::string hexview (const std::string_view value, std::size_t bytesPerLine = 4) {
    std::string result;
    result.reserve(value.size() * 4);
    std::string alpha; // temporarily store the ascii representation
    alpha.reserve(8 * bytesPerLine);
    for (auto [i, c] : std::ranges::views::enumerate(value) ) {
        if (i % (bytesPerLine * 8) == 0) {
            result.append(fmt::format("{0:#08x} - {0:04} | ", i)); // print address in hex and decimal
        }
        result.append(fmt::format("{:02x} ", c));
        alpha.append(fmt::format("{}", std::isprint(c) ? c : '.'));
        if ((i+1) % 8 == 0) {
            result.append("   ");
            alpha.append(" ");
        }
        if ((i+1) % (bytesPerLine * 8) == 0) {
            result.append(fmt::format("   {}\n", alpha));
            alpha.clear();
        }
    }
    result.append(fmt::format("{:{}}   {}\n", "", 3 * (9 * bytesPerLine - alpha.size()), alpha));
    return result;
};

TEST_CASE("IoClassSerialiserCmwLight simple test", "[IoClassSerialiser]") {
    using namespace opencmw;
    using namespace ioserialiser_cmwlight_test;
    debug::resetStats();
    {
        debug::Timer timer("IoClassSerialiser basic syntax", 30);

        IoBuffer     buffer;
        IoBuffer     bufferMap;
        std::cout << fmt::format("buffer size (before): {} bytes\n", buffer.size());

        SimpleTestDataMapAsNested data{
            .a   = 30,
            .ab  = 1.2f,
            .abc = 1.23,
            .b   = "abc",
            .c   = { 5, 4, 3 },
            .cd  = { 2.1, 4.2 },
            .ce  = { "hallo", "welt" },
            .d   = { { 6, 5, 4, 3, 2, 1 }, { 3, 2 } },
            .e   = std::make_unique<SimpleTestDataMapAsNested>(SimpleTestDataMapAsNested{
                      .a   = 40,
                      .ab  = 2.2f,
                      .abc = 2.23,
                      .b   = "abcdef",
                      .c   = { 9, 8, 7 },
                      .cd  = { 3.1, 1.2 },
                      .ce  = { "ei", "gude" },
                      .d   = { { 6, 5, 4, 3, 2, 1 }, { 3, 2 } },
                      .e   = nullptr,
                      .g = {6,6,6}}),
            .f   = { "four", "five" },
            .g = {4, 5, 6}
        };

        SimpleTestData dataMap{
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
                        .e   = nullptr,
                        .g = {{"g1", 6}, {"g2", 6}, {"g3", 6}} }),
                .f   = { "four", "five" },
                .g = {{"g1", 4}, {"g2", 5}, {"g3", 6}}
        };

        // check that empty buffer cannot be deserialised
        buffer.put(0L);
        //CHECK_THROWS_AS((opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data)), ProtocolException);
        buffer.clear();

        SimpleTestDataMapAsNested data2;
        REQUIRE(data != data2);
        SimpleTestDataMapAsNested dataMap2;
        REQUIRE(dataMap != dataMap2);
        std::cout << "object (short): " << ClassInfoShort << data << '\n';
        std::cout << fmt::format("object (fmt): {}\n", data);
        std::cout << "object (long):  " << ClassInfoVerbose << data << '\n';

        opencmw::serialise<opencmw::CmwLight>(buffer, data);
        opencmw::serialise<opencmw::CmwLight>(bufferMap, dataMap);
        std::cout << fmt::format("buffer size (after): {} bytes\n", buffer.size());

        buffer.put("a\0df"sv); // add some garbage after the serialised object to check if it is handled correctly
        bufferMap.put("a\0df"sv); // add some garbage after the serialised object to check if it is handled correctly

        buffer.reset();
        bufferMap.reset();

        std::cout << "buffer contentsSubObject: \n" << hexview(buffer.asString()) << "\n";
        std::cout << "buffer contentsMap: \n" << hexview(bufferMap.asString()) << "\n";

        REQUIRE(buffer.asString() == bufferMap.asString());

        auto result = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data2);
        std::cout << "deserialised object (long):  " << ClassInfoVerbose << data2 << '\n';
        std::cout << "deserialisation messages: " << result << std::endl;
        //REQUIRE(data == data2);

        auto result2 = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(bufferMap, dataMap2);
        std::cout << "deserialised object (long):  " << ClassInfoVerbose << data2 << '\n';
        std::cout << "deserialisation messages: " << result << std::endl;
        REQUIRE(dataMap == dataMap2);
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
    std::map<std::string, int>      g{{"g1", 1}, {"g2", 2}, {"g3", 3}};
};
} // namespace ioserialiser_cmwlight_test
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestDataMoreFields, a2, ab2, abc2, b2, c2, cd2, ce2, d2, e2, a, ab, abc, b, c, cd, ce, d, e, f, g)

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
            .f   = { "four", "six" },
            .g{{"g1", 1}, {"g2", 2}, {"g3", 3}}
        };
        SimpleTestDataMoreFields data2;
        std::cout << fmt::format("object (fmt): {}\n", data);
        opencmw::serialise<opencmw::CmwLight>(buffer, data);
        buffer.reset();
        auto result = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data2);
        std::cout << fmt::format("deserialised object (fmt): {}\n", data2);
        std::cout << "deserialisation messages: " << result << std::endl;
        REQUIRE(result.setFields["root"] == std::vector<bool>{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1 });
        REQUIRE(result.additionalFields.empty());
        REQUIRE(result.exceptions.empty());

        std::cout << "\nand now the other way round!\n\n";
        buffer.clear();
        opencmw::serialise<opencmw::CmwLight>(buffer, data2);
        buffer.reset();
        auto result_back = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data);
        std::cout << fmt::format("deserialised object (fmt): {}\n", data);
        std::cout << "deserialisation messages: " << result_back << std::endl;
        REQUIRE(result_back.setFields["root"] == std::vector<bool>{ 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1 });
        REQUIRE(result_back.additionalFields.size() == 8);
        REQUIRE(result_back.exceptions.size() == 8);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}
