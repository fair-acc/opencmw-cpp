#pragma clang diagnostic push
#pragma ide diagnostic ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-magic-numbers"
#include <catch2/catch.hpp>

#include <iostream>
#include <ranges>
#include <string_view>

#include <Debug.hpp>
#include <Formatter.hpp>
#include <IoSerialiserCmwLight.hpp>
#include <opencmw.hpp>

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
    std::map<std::string, int>      g{ { "g1", 1 }, { "g2", 2 }, { "g3", 3 } };
    bool                            operator==(const ioserialiser_cmwlight_test::SimpleTestData &o) const { // deep comparison function
        return a == o.a && ab == o.ab && abc == o.abc && b == o.b && c == o.c && cd == o.cd && d == o.d && ((!e && !o.e) || *e == *(o.e)) && f == o.f && g == o.g;
    }
};
struct SimpleTestDataMapNested {
    int  g1;
    int  g2;
    int  g3;

    bool operator==(const SimpleTestDataMapNested &o) const = default;
    bool operator==(const std::map<std::string, int> &o) const {
        return g1 == o.at("g1") && g2 == o.at("g2") && g3 == o.at("g3");
    }
};
struct SimpleTestDataMapAsNested {
    int                                        a   = 1337;
    float                                      ab  = 13.37f;
    double                                     abc = 42.23;
    std::string                                b   = "hello";
    std::array<int, 3>                         c{ 3, 2, 1 };
    std::vector<double>                        cd{ 2.3, 3.4, 4.5, 5.6 };
    std::vector<std::string>                   ce{ "hello", "world" };
    opencmw::MultiArray<double, 2>             d{ { 1, 2, 3, 4, 5, 6 }, { 2, 3 } };
    std::unique_ptr<SimpleTestDataMapAsNested> e = nullptr;
    std::set<std::string>                      f{ "one", "two", "three" };
    SimpleTestDataMapNested                    g{ 1, 2, 3 };
    bool                                       operator==(const ioserialiser_cmwlight_test::SimpleTestDataMapAsNested &o) const { // deep comparison function
        return a == o.a && ab == o.ab && abc == o.abc && b == o.b && c == o.c && cd == o.cd && d == o.d && ((!e && !o.e) || *e == *(o.e)) && f == o.f && g == o.g;
    }
    bool operator==(const ioserialiser_cmwlight_test::SimpleTestData &o) const { // deep comparison function
        return a == o.a && ab == o.ab && abc == o.abc && b == o.b && c == o.c && cd == o.cd && d == o.d && ((!e && !o.e) || *e == *(o.e)) && f == o.f && g == o.g;
    }
};
} // namespace ioserialiser_cmwlight_test
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestData, a, ab, abc, b, c, cd, ce, d, e, f, g)
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestDataMapAsNested, a, ab, abc, b, c, cd, ce, d, e, f, g)
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestDataMapNested, g1, g2, g3)

// small utility function that prints the content of a string in the classic hexedit way with address, hexadecimal and ascii representations
static std::string hexview(const std::string_view value, std::size_t bytesPerLine = 4) {
    std::string result;
    result.reserve(value.size() * 4);
    std::string alpha; // temporarily store the ascii representation
    alpha.reserve(8 * bytesPerLine);
    std::size_t i = 0;
    for (auto c : value) {
        if (i % (bytesPerLine * 8) == 0) {
            result.append(std::format("{0:#08x} - {0:04} | ", i)); // print address in hex and decimal
        }
        result.append(std::format("{:02x} ", c));
        alpha.append(std::format("{}", std::isprint(c) ? c : '.'));
        if ((i + 1) % 8 == 0) {
            result.append("   ");
            alpha.append(" ");
        }
        if ((i + 1) % (bytesPerLine * 8) == 0) {
            result.append(std::format("   {}\n", alpha));
            alpha.clear();
        }
        i++;
    }
    result.append(std::format("{:{}}   {}\n", "", 3 * (9 * bytesPerLine - alpha.size()), alpha));
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
        std::cout << std::format("buffer size (before): {} bytes\n", buffer.size());

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
                      .g   = { 6, 6, 6 } }),
            .f   = { "four", "five" },
            .g   = { 4, 5, 6 }
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
                      .g   = { { "g1", 6 }, { "g2", 6 }, { "g3", 6 } } }),
            .f   = { "four", "five" },
            .g   = { { "g1", 4 }, { "g2", 5 }, { "g3", 6 } }
        };

        // check that empty buffer cannot be deserialised
        buffer.put(0L);
        // CHECK_THROWS_AS((opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data)), ProtocolException);
        buffer.clear();

        SimpleTestDataMapAsNested data2;
        REQUIRE(data != data2);
        SimpleTestDataMapAsNested dataMap2;
        REQUIRE(dataMap != dataMap2);
        std::cout << "object (short): " << ClassInfoShort << data << '\n';
        std::cout << std::format("object (std::format): {}\n", data);
        std::cout << "object (long):  " << ClassInfoVerbose << data << '\n';

        opencmw::serialise<opencmw::CmwLight>(buffer, data);
        opencmw::serialise<opencmw::CmwLight>(bufferMap, dataMap);
        std::cout << std::format("buffer size (after): {} bytes\n", buffer.size());

        buffer.put("a\0df"sv);    // add some garbage after the serialised object to check if it is handled correctly
        bufferMap.put("a\0df"sv); // add some garbage after the serialised object to check if it is handled correctly

        buffer.reset();
        bufferMap.reset();

        std::cout << "buffer contentsSubObject: \n"
                  << hexview(buffer.asString()) << "\n";
        std::cout << "buffer contentsMap: \n"
                  << hexview(bufferMap.asString()) << "\n";

        REQUIRE(buffer.asString() == bufferMap.asString());

        // TODO: fix this case
        // auto result = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data2);
        // std::cout << "deserialised object (long):  " << ClassInfoVerbose << data2 << '\n';
        // std::cout << "deserialisation messages: " << result << std::endl;
        //// REQUIRE(data == data2);

        // auto result2 = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(bufferMap, dataMap2);
        // std::cout << "deserialised object (long):  " << ClassInfoVerbose << dataMap2 << '\n';
        // std::cout << "deserialisation messages: " << result2 << std::endl;
        // REQUIRE(dataMap == dataMap2);
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
    std::map<std::string, int>      g{ { "g1", 1 }, { "g2", 2 }, { "g3", 3 } };
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
        std::cout << std::format("buffer size (before): {} bytes\n", buffer.size());

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
            .g{ { "g1", 1 }, { "g2", 2 }, { "g3", 3 } }
        };
        SimpleTestDataMoreFields data2;
        std::cout << std::format("object (std::format): {}\n", data);
        opencmw::serialise<opencmw::CmwLight>(buffer, data);
        buffer.reset();
        auto result = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data2);
        std::cout << std::format("deserialised object (std::format): {}\n", data2);
        std::cout << "deserialisation messages: " << result << std::endl;
        REQUIRE(result.setFields["root"] == std::vector<bool>{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1 });
        REQUIRE(result.additionalFields.empty());
        REQUIRE(result.exceptions.empty());

        std::cout << "\nand now the other way round!\n\n";
        buffer.clear();
        opencmw::serialise<opencmw::CmwLight>(buffer, data2);
        buffer.reset();
        auto result_back = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(buffer, data);
        std::cout << std::format("deserialised object (std::format): {}\n", data);
        std::cout << "deserialisation messages: " << result_back << std::endl;
        REQUIRE(result_back.setFields["root"] == std::vector<bool>{ 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1 });
        REQUIRE(result_back.additionalFields.size() == 8);
        REQUIRE(result_back.exceptions.size() == 8);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

namespace ioserialiser_cmwlight_test {
struct IntegerMap {
    int x_8 = 1336; // fieldname gets mapped to "8"
    int foo = 42;
    int bar = 45;
};
} // namespace ioserialiser_cmwlight_test
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::IntegerMap, x_8, foo, bar)

TEST_CASE("IoClassSerialiserCmwLight deserialise into map", "[IoClassSerialiser]") {
    using namespace opencmw;
    using namespace ioserialiser_cmwlight_test;
    debug::resetStats();
    {
        // serialise
        IoBuffer   buffer;
        IntegerMap input{ 23, 13, 37 };
        opencmw::serialise<opencmw::CmwLight>(buffer, input);
        buffer.reset();
        REQUIRE(buffer.size() == sizeof(int32_t) /* map size */ + refl::reflect(input).members.size /* map entries */ * (sizeof(int32_t) /* string lengths */ + sizeof(uint8_t) /* type */ + sizeof(int32_t) /* int */) + 2 + 4 + 4 /* strings + \0 */);
        // std::cout << hexview(buffer.asString());

        // deserialise
        std::map<std::string, int> deserialised{};
        DeserialiserInfo           info;
        auto                       field = opencmw::detail::newFieldHeader<CmwLight, true>(buffer, "map", 0, deserialised, -1);
        opencmw::FieldHeaderReader<CmwLight>::template get<ProtocolCheck::IGNORE>(buffer, info, field);
        IoSerialiser<CmwLight, START_MARKER>::deserialise(buffer, field, START_MARKER_INST);
        opencmw::IoSerialiser<CmwLight, std::map<std::string, int>>::deserialise(buffer, field, deserialised);

        // check for correctness
        REQUIRE(deserialised.size() == 3);
        REQUIRE(deserialised["8"] == 23);
        REQUIRE(deserialised["foo"] == 13);
        REQUIRE(deserialised["bar"] == 37);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

namespace opencmw::serialiser::cmwlighttests {
struct CmwLightHeaderOptions {
    int64_t                            b; // SOURCE_ID
    std::map<std::string, std::string> e;
    // can potentially contain more and arbitrary data
    // accessors to make code more readable
    int64_t                           &sourceId() { return b; }
    std::map<std::string, std::string> sessionBody;
};
struct CmwLightHeader {
    int8_t                                 x_2; // REQ_TYPE_TAG
    int64_t                                x_0; // ID_TAG
    std::string                            x_1; // DEVICE_NAME
    std::string                            f;   // PROPERTY_NAME
    int8_t                                 x_7; // UPDATE_TYPE
    std::string                            d;   // SESSION_ID
    std::unique_ptr<CmwLightHeaderOptions> x_3;
    // accessors to make code more readable
    int8_t                                 &requestType() { return x_2; }
    int64_t                                &id() { return x_0; }
    std::string                            &device() { return x_1; }
    std::string                            &property() { return f; }
    int8_t                                 &updateType() { return x_7; }
    std::string                            &sessionId() { return d; }
    std::unique_ptr<CmwLightHeaderOptions> &options() { return x_3; }
};
struct DigitizerVersion {
    std::string classVersion;
    std::string deployUnitVersion;
    std::string fesaVersion;
    std::string gr_flowgraph_version;
    std::string gr_digitizer_version;
    std::string daqAPIVersion;
};
} // namespace opencmw::serialiser::cmwlighttests
ENABLE_REFLECTION_FOR(opencmw::serialiser::cmwlighttests::CmwLightHeaderOptions, b, e)
ENABLE_REFLECTION_FOR(opencmw::serialiser::cmwlighttests::CmwLightHeader, x_2, x_0, x_1, f, x_7, d, x_3)
ENABLE_REFLECTION_FOR(opencmw::serialiser::cmwlighttests::DigitizerVersion, classVersion, deployUnitVersion, fesaVersion, gr_flowgraph_version, gr_digitizer_version, daqAPIVersion)

TEST_CASE("IoClassSerialiserCmwLight Deserialise rda3 data", "[IoClassSerialiser]") {
    // ensure that important rda3 messages can be properly deserialized
    using namespace opencmw;
    debug::resetStats();
    using namespace opencmw::serialiser::cmwlighttests;
    {
        std::string_view rda3ConnectReply = "\x07\x00\x00\x00\x02\x00\x00\x00\x30\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x31\x00\x07\x01\x00\x00\x00\x00\x02\x00\x00\x00\x32\x00\x01\x03\x02\x00\x00\x00\x33\x00\x08\x02\x00\x00\x00\x02\x00\x00\x00\x62\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x65\x00\x08\x00\x00\x00\x00\x02\x00\x00\x00\x37\x00\x01\x70\x02\x00\x00\x00\x64\x00\x07\x01\x00\x00\x00\x00\x02\x00\x00\x00\x66\x00\x07\x01\x00\x00\x00\x00"sv;
        //        0   1   2   3   4   5   6   7      8   9   a   b   c   d   e   f
        // 000 "\x07\x00\x00\x00\x02\x00\x00\x00" "\x30\x00\x04\x00\x00\x00\x00\x00"
        // 010 "\x00\x00\x00\x02\x00\x00\x00\x31" "\x00\x07\x01\x00\x00\x00\x00\x02"
        // 020 "\x00\x00\x00\x32\x00\x01\x03\x02" "\x00\x00\x00\x33\x00\x08\x02\x00"
        // 030 "\x00\x00\x02\x00\x00\x00\x62\x00" "\x04\x00\x00\x00\x00\x00\x00\x00"
        // 040 "\x00\x02\x00\x00\x00\x65\x00\x08" "\x00\x00\x00\x00\x02\x00\x00\x00"
        // 050 "\x37\x00\x01\x70\x02\x00\x00\x00" "\x64\x00\x07\x01\x00\x00\x00\x00"
        // 060 "\x02\x00\x00\x00\x66\x00\x07\x01" "\x00\x00\x00\x00"sv
        IoBuffer       buffer{ rda3ConnectReply.data(), rda3ConnectReply.size() };
        CmwLightHeader deserialised;
        auto           result = opencmw::deserialise<CmwLight, opencmw::ProtocolCheck::LENIENT>(buffer, deserialised);

        REQUIRE(deserialised.requestType() == 3);
        REQUIRE(deserialised.id() == 0);
        REQUIRE(deserialised.device().empty());
        REQUIRE(deserialised.property().empty());
        REQUIRE(deserialised.updateType() == 0x70);
        REQUIRE(deserialised.sessionId().empty());
        REQUIRE(deserialised.options()->sourceId() == 0);
        REQUIRE(deserialised.options()->sessionBody.empty());
    }
    {
        std::string_view data = "\x07\x00\x00\x00\x02\x00\x00\x00\x30\x00\x04\x09\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x31\x00\x07\x08\x00\x00\x00\x47\x53\x43\x44\x30\x30\x32\x00\x02\x00\x00\x00\x32\x00\x01\x0b\x02\x00\x00\x00\x33\x00\x08\x01\x00\x00\x00\x02\x00\x00\x00\x65\x00\x08\x00\x00\x00\x00\x02\x00\x00\x00\x37\x00\x01\x00\x02\x00\x00\x00\x64\x00\x07\x25\x01\x00\x00\x52\x65\x6d\x6f\x74\x65\x48\x6f\x73\x74\x49\x6e\x66\x6f\x49\x6d\x70\x6c\x5b\x6e\x61\x6d\x65\x3d\x66\x65\x73\x61\x2d\x65\x78\x70\x6c\x6f\x72\x65\x72\x2d\x61\x70\x70\x3b\x20\x75\x73\x65\x72\x4e\x61\x6d\x65\x3d\x61\x6b\x72\x69\x6d\x6d\x3b\x20\x61\x70\x70\x49\x64\x3d\x5b\x61\x70\x70\x3d\x66\x65\x73\x61\x2d\x65\x78\x70\x6c\x6f\x72\x65\x72\x2d\x61\x70\x70\x3b\x76\x65\x72\x3d\x31\x39\x2e\x30\x2e\x30\x3b\x75\x69\x64\x3d\x61\x6b\x72\x69\x6d\x6d\x3b\x68\x6f\x73\x74\x3d\x53\x59\x53\x50\x43\x30\x30\x38\x3b\x70\x69\x64\x3d\x31\x39\x31\x36\x31\x36\x3b\x5d\x3b\x20\x70\x72\x6f\x63\x65\x73\x73\x3d\x66\x65\x73\x61\x2d\x65\x78\x70\x6c\x6f\x72\x65\x72\x2d\x61\x70\x70\x3b\x20\x70\x69\x64\x3d\x31\x39\x31\x36\x31\x36\x3b\x20\x61\x64\x64\x72\x65\x73\x73\x3d\x74\x63\x70\x3a\x2f\x2f\x53\x59\x53\x50\x43\x30\x30\x38\x3a\x30\x3b\x20\x73\x74\x61\x72\x74\x54\x69\x6d\x65\x3d\x32\x30\x32\x34\x2d\x30\x37\x2d\x30\x34\x20\x31\x31\x3a\x31\x31\x3a\x31\x32\x3b\x20\x63\x6f\x6e\x6e\x65\x63\x74\x69\x6f\x6e\x54\x69\x6d\x65\x3d\x41\x62\x6f\x75\x74\x20\x61\x67\x6f\x3b\x20\x76\x65\x72\x73\x69\x6f\x6e\x3d\x31\x30\x2e\x33\x2e\x30\x3b\x20\x6c\x61\x6e\x67\x75\x61\x67\x65\x3d\x4a\x61\x76\x61\x5d\x31\x00\x02\x00\x00\x00\x66\x00\x07\x08\x00\x00\x00\x56\x65\x72\x73\x69\x6f\x6e\x00"sv;
        //  reply req type: session confirm
        //                                                              \x07 \x00 \x00 \x00                ....
        // \x02 \x00 \x00 \x00 \x30 \x00 \x04 \x09  \x00 \x00 \x00 \x00 \x00 \x00 \x00 \x02   ....0... ........
        // \x00 \x00 \x00 \x31 \x00 \x07 \x08 \x00  \x00 \x00 \x47 \x53 \x43 \x44 \x30 \x30   ...1.... ..GSCD00
        // \x32 \x00 \x02 \x00 \x00 \x00 \x32 \x00  \x01 \x0b \x02 \x00 \x00 \x00 \x33 \x00   2.....2. ......3.
        // \x08 \x01 \x00 \x00 \x00 \x02 \x00 \x00  \x00 \x65 \x00 \x08 \x00 \x00 \x00 \x00   ........ .e......
        // \x02 \x00 \x00 \x00 \x37 \x00 \x01 \x00  \x02 \x00 \x00 \x00 \x64 \x00 \x07 \x25   ....7... ....d..%
        // \x01 \x00 \x00 \x52 \x65 \x6d \x6f \x74  \x65 \x48 \x6f \x73 \x74 \x49 \x6e \x66   ...Remot eHostInf
        // \x6f \x49 \x6d \x70 \x6c \x5b \x6e \x61  \x6d \x65 \x3d \x66 \x65 \x73 \x61 \x2d   oImpl[na me=fesa-
        // \x65 \x78 \x70 \x6c \x6f \x72 \x65 \x72  \x2d \x61 \x70 \x70 \x3b \x20 \x75 \x73   explorer -app; us
        // \x65 \x72 \x4e \x61 \x6d \x65 \x3d \x61  \x6b \x72 \x69 \x6d \x6d \x3b \x20 \x61   erName=a krimm; a
        // \x70 \x70 \x49 \x64 \x3d \x5b \x61 \x70  \x70 \x3d \x66 \x65 \x73 \x61 \x2d \x65   ppId=[ap p=fesa-e
        // \x78 \x70 \x6c \x6f \x72 \x65 \x72 \x2d  \x61 \x70 \x70 \x3b \x76 \x65 \x72 \x3d   xplorer- app;ver=
        // \x31 \x39 \x2e \x30 \x2e \x30 \x3b \x75  \x69 \x64 \x3d \x61 \x6b \x72 \x69 \x6d   19.0.0;u id=akrim
        // \x6d \x3b \x68 \x6f \x73 \x74 \x3d \x53  \x59 \x53 \x50 \x43 \x30 \x30 \x38 \x3b   m;host=S YSPC008;
        // \x70 \x69 \x64 \x3d \x31 \x39 \x31 \x36  \x31 \x36 \x3b \x5d \x3b \x20 \x70 \x72   pid=1916 16;]; pr
        // \x6f \x63 \x65 \x73 \x73 \x3d \x66 \x65  \x73 \x61 \x2d \x65 \x78 \x70 \x6c \x6f   ocess=fe sa-explo
        // \x72 \x65 \x72 \x2d \x61 \x70 \x70 \x3b  \x20 \x70 \x69 \x64 \x3d \x31 \x39 \x31   rer-app;  pid=191
        // \x36 \x31 \x36 \x3b \x20 \x61 \x64 \x64  \x72 \x65 \x73 \x73 \x3d \x74 \x63 \x70   616; add ress=tcp
        // \x3a \x2f \x2f \x53 \x59 \x53 \x50 \x43  \x30 \x30 \x38 \x3a \x30 \x3b \x20 \x73   ://SYSPC 008:0; s
        // \x74 \x61 \x72 \x74 \x54 \x69 \x6d \x65  \x3d \x32 \x30 \x32 \x34 \x2d \x30 \x37   tartTime =2024-07
        // \x2d \x30 \x34 \x20 \x31 \x31 \x3a \x31  \x31 \x3a \x31 \x32 \x3b \x20 \x63 \x6f   -04 11:1 1:12; co
        // \x6e \x6e \x65 \x63 \x74 \x69 \x6f \x6e  \x54 \x69 \x6d \x65 \x3d \x41 \x62 \x6f   nnection Time=Abo
        // \x75 \x74 \x20 \x61 \x67 \x6f \x3b \x20  \x76 \x65 \x72 \x73 \x69 \x6f \x6e \x3d   ut ago;  version=
        // \x31 \x30 \x2e \x33 \x2e \x30 \x3b \x20  \x6c \x61 \x6e \x67 \x75 \x61 \x67 \x65   10.3.0;  language
        // \x3d \x4a \x61 \x76 \x61 \x5d \x31 \x00  \x02 \x00 \x00 \x00 \x66 \x00 \x07 \x08   =Java]1. ....f...
        // \x00 \x00 \x00 \x56 \x65 \x72 \x73 \x69  \x6f \x6e \x00                           ...Versi on.
        IoBuffer       buffer{ data.data(), data.size() };
        CmwLightHeader deserialised;
        auto           result = opencmw::deserialise<CmwLight, opencmw::ProtocolCheck::LENIENT>(buffer, deserialised);
    }
    {
        std::string_view data = "\x06\x00\x00\x00\x02\x00\x00\x00\x30\x00\x04\x09\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x31\x00\x07\x01\x00\x00\x00\x00\x02\x00\x00\x00\x32\x00\x01\x03\x02\x00\x00\x00\x37\x00\x01\x00\x02\x00\x00\x00\x64\x00\x07\x01\x00\x00\x00\x00\x02\x00\x00\x00\x66\x00\x07\x01\x00\x00\x00\x00\x01\xc3\x06\x00\x00\x00\x0d\x00\x00\x00\x63\x6c\x61\x73\x73\x56\x65\x72\x73\x69\x6f\x6e\x00\x07\x06\x00\x00\x00\x36\x2e\x30\x2e\x30\x00\x0e\x00\x00\x00\x64\x61\x71\x41\x50\x49\x56\x65\x72\x73\x69\x6f\x6e\x00\x07\x04\x00\x00\x00\x32\x2e\x30\x00\x12\x00\x00\x00\x64\x65\x70\x6c\x6f\x79\x55\x6e\x69\x74\x56\x65\x72\x73\x69\x6f\x6e\x00\x07\x06\x00\x00\x00\x36\x2e\x30\x2e\x30\x00\x0c\x00\x00\x00\x66\x65\x73\x61\x56\x65\x72\x73\x69\x6f\x6e\x00\x07\x06\x00\x00\x00\x37\x2e\x33\x2e\x30\x00\x15\x00\x00\x00\x67\x72\x5f\x64\x69\x67\x69\x74\x69\x7a\x65\x72\x5f\x76\x65\x72\x73\x69\x6f\x6e\x00\x07\x08\x00\x00\x00\x35\x2e\x31\x2e\x34\x2e\x30\x00\x15\x00\x00\x00\x67\x72\x5f\x66\x6c\x6f\x77\x67\x72\x61\x70\x68\x5f\x76\x65\x72\x73\x69\x6f\x6e\x00\x07\x08\x00\x00\x00\x35\x2e\x30\x2e\x32\x2e\x30\x00\x01\x62\x03\x00\x00\x00\x02\x00\x00\x00\x35\x00\x04\x88\x39\xfe\x41\x88\xf7\xde\x17\x02\x00\x00\x00\x36\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x78\x00\x08\x03\x00\x00\x00\x09\x00\x00\x00\x61\x63\x71\x53\x74\x61\x6d\x70\x00\x04\x88\x39\xfe\x41\x88\xf7\xde\x17\x05\x00\x00\x00\x74\x79\x70\x65\x00\x03\x02\x00\x00\x00\x08\x00\x00\x00\x76\x65\x72\x73\x69\x6f\x6e\x00\x03\x01\x00\x00\x00\x00\x03";
        // Reply with Req Type = Reply, gets sent after get request
        //                          \x06 \x00 \x00  \x00 \x02 \x00 \x00 \x00 \x30 \x00 \x04        ... .....0..
        // \x09 \x00 \x00 \x00 \x00 \x00 \x00 \x00  \x02 \x00 \x00 \x00 \x31 \x00 \x07 \x01   ........ ....1...
        // \x00 \x00 \x00 \x00 \x02 \x00 \x00 \x00  \x32 \x00 \x01 \x03 \x02 \x00 \x00 \x00   ........ 2.......
        // \x37 \x00 \x01 \x00 \x02 \x00 \x00 \x00  \x64 \x00 \x07 \x01 \x00 \x00 \x00 \x00   7....... d.......
        // \x02 \x00 \x00 \x00 \x66 \x00 \x07 \x01  \x00 \x00 \x00 \x00 \x01 \xc3 \x06 \x00   ....f... ........
        // \x00 \x00 \x0d \x00 \x00 \x00 \x63 \x6c  \x61 \x73 \x73 \x56 \x65 \x72 \x73 \x69   ......cl assVersi
        // \x6f \x6e \x00 \x07 \x06 \x00 \x00 \x00  \x36 \x2e \x30 \x2e \x30 \x00 \x0e \x00   on...... 6.0.0...
        // \x00 \x00 \x64 \x61 \x71 \x41 \x50 \x49  \x56 \x65 \x72 \x73 \x69 \x6f \x6e \x00   ..daqAPI Version.
        // \x07 \x04 \x00 \x00 \x00 \x32 \x2e \x30  \x00 \x12 \x00 \x00 \x00 \x64 \x65 \x70   .....2.0 .....dep
        // \x6c \x6f \x79 \x55 \x6e \x69 \x74 \x56  \x65 \x72 \x73 \x69 \x6f \x6e \x00 \x07   loyUnitV ersion..
        // \x06 \x00 \x00 \x00 \x36 \x2e \x30 \x2e  \x30 \x00 \x0c \x00 \x00 \x00 \x66 \x65   ....6.0. 0.....fe
        // \x73 \x61 \x56 \x65 \x72 \x73 \x69 \x6f  \x6e \x00 \x07 \x06 \x00 \x00 \x00 \x37   saVersio n......7
        // \x2e \x33 \x2e \x30 \x00 \x15 \x00 \x00  \x00 \x67 \x72 \x5f \x64 \x69 \x67 \x69   .3.0.... .gr_digi
        // \x74 \x69 \x7a \x65 \x72 \x5f \x76 \x65  \x72 \x73 \x69 \x6f \x6e \x00 \x07 \x08   tizer_ve rsion...
        // \x00 \x00 \x00 \x35 \x2e \x31 \x2e \x34  \x2e \x30 \x00 \x15 \x00 \x00 \x00 \x67   ...5.1.4 .0.....g
        // \x72 \x5f \x66 \x6c \x6f \x77 \x67 \x72  \x61 \x70 \x68 \x5f \x76 \x65 \x72 \x73   r_flowgr aph_vers
        // \x69 \x6f \x6e \x00 \x07 \x08 \x00 \x00  \x00 \x35 \x2e \x30 \x2e \x32 \x2e \x30   ion..... .5.0.2.0
        // \x00 \x01 \x62 \x03 \x00 \x00 \x00 \x02  \x00 \x00 \x00 \x35 \x00 \x04 \x88 \x39   ..b..... ...5...9
        // \xfe \x41 \x88 \xf7 \xde \x17 \x02 \x00  \x00 \x00 \x36 \x00 \x04 \x00 \x00 \x00   .A...... ..6.....
        // \x00 \x00 \x00 \x00 \x00 \x02 \x00 \x00  \x00 \x78 \x00 \x08 \x03 \x00 \x00 \x00   ........ .x......
        // \x09 \x00 \x00 \x00 \x61 \x63 \x71 \x53  \x74 \x61 \x6d \x70 \x00 \x04 \x88 \x39   ....acqS tamp...9
        // \xfe \x41 \x88 \xf7 \xde \x17 \x05 \x00  \x00 \x00 \x74 \x79 \x70 \x65 \x00 \x03   .A...... ..type..
        // \x02 \x00 \x00 \x00 \x08 \x00 \x00 \x00  \x76 \x65 \x72 \x73 \x69 \x6f \x6e \x00   ........ version.
        // \x03 \x01 \x00 \x00 \x00 \x00 \x03                                                 .......
        IoBuffer         buffer{ data.data(), data.size() };
        DigitizerVersion deserialised;
        auto             result = opencmw::deserialise<CmwLight, opencmw::ProtocolCheck::LENIENT>(buffer, deserialised);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}
