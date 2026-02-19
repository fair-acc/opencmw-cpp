#pragma clang diagnostic push
#pragma ide diagnostic ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-magic-numbers"
#include <catch2/catch.hpp>

#include <iostream>
#include <ranges>
#include <string_view>

#include <Debug.hpp>
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
    bool                            operator==(const SimpleTestData &o) const { // deep comparison function
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
    bool                                       operator==(const SimpleTestDataMapAsNested &o) const { // deep comparison function
        return a == o.a && ab == o.ab && abc == o.abc && b == o.b && c == o.c && cd == o.cd && d == o.d && ((!e && !o.e) || *e == *(o.e)) && f == o.f && g == o.g;
    }
    bool operator==(const SimpleTestData &o) const { // deep comparison function
        return a == o.a && ab == o.ab && abc == o.abc && b == o.b && c == o.c && cd == o.cd && d == o.d && ((!e && !o.e) || *e == *(o.e)) && f == o.f && g == o.g;
    }
};
} // namespace ioserialiser_cmwlight_test
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestData, a, ab, abc, b, c, cd, ce, d, e, f, g)
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestDataMapAsNested, a, ab, abc, b, c, cd, ce, d, e, f, g)
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestDataMapNested, g1, g2, g3)

// small utility function that prints the content of a string in the classic hexedit way with address, hexadecimal and ascii representations
static std::string hexview(const std::string_view value, const std::size_t bytesPerLine = 4) {
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

        opencmw::serialise<CmwLight>(buffer, data);
        opencmw::serialise<CmwLight>(bufferMap, dataMap);
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
        opencmw::serialise<CmwLight>(buffer, data);
        buffer.reset();
        auto result = opencmw::deserialise<CmwLight, ProtocolCheck::LENIENT>(buffer, data2);
        std::cout << std::format("deserialised object (std::format): {}\n", data2);
        std::cout << "deserialisation messages: " << result << std::endl;
        REQUIRE(result.setFields["root"] == std::vector<bool>{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1 });
        REQUIRE(result.additionalFields.empty());
        REQUIRE(result.exceptions.empty());

        std::cout << "\nand now the other way round!\n\n";
        buffer.clear();
        opencmw::serialise<CmwLight>(buffer, data2);
        buffer.reset();
        auto result_back = opencmw::deserialise<CmwLight, ProtocolCheck::LENIENT>(buffer, data);
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
        opencmw::serialise<CmwLight>(buffer, input);
        buffer.reset();
        REQUIRE(buffer.size() == sizeof(int32_t) /* map size */ + refl::reflect(input).members.size /* map entries */ * (sizeof(int32_t) /* string lengths */ + sizeof(uint8_t) /* type */ + sizeof(int32_t) /* int */) + 2 + 4 + 4 /* strings + \0 */);
        // std::cout << hexview(buffer.asString());

        // deserialise
        std::map<std::string, int> deserialised{};
        DeserialiserInfo           info;
        auto                       field = opencmw::detail::newFieldHeader<CmwLight, true>(buffer, "map", 0, deserialised, -1);
        FieldHeaderReader<CmwLight>::get<ProtocolCheck::IGNORE>(buffer, info, field);
        IoSerialiser<CmwLight, START_MARKER>::deserialise(buffer, field, START_MARKER_INST);
        IoSerialiser<CmwLight, std::map<std::string, int>>::deserialise(buffer, field, deserialised);

        // check for correctness
        REQUIRE(deserialised.size() == 3);
        REQUIRE(deserialised["8"] == 23);
        REQUIRE(deserialised["foo"] == 13);
        REQUIRE(deserialised["bar"] == 37);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

TEST_CASE("IoClassSerialiserCmwLight deserialise variant map", "[IoClassSerialiser]") {
    using namespace opencmw;
    using namespace ioserialiser_cmwlight_test;
    debug::resetStats();
    {
        const std::string_view                                        expected{ "\x03\x00\x00\x00" // 3 fields
                                         "\x02\x00\x00\x00"
                                                                                "a\x00"
                                                                                "\x03"
                                                                                "\x23\x00\x00\x00" // "a" -> int:0x23
                                         "\x02\x00\x00\x00"
                                                                                "b\x00"
                                                                                "\x06"
                                                                                "\xEC\x51\xB8\x1E\x85\xEB\xF5\x3F" // "b" -> double:1.337
                                         "\x02\x00\x00\x00"
                                                                                "c\x00"
                                                                                "\x07"
                                                                                "\x04\x00\x00\x00"
                                                                                "foo\x00" // "c" -> "foo"
            ,
            45 };
        std::map<std::string, std::variant<int, double, std::string>> map{ { "a", 0x23 }, { "b", 1.37 }, { "c", "foo" } };

        IoBuffer                                                      buffer;
        auto                                                          field = opencmw::detail::newFieldHeader<CmwLight, true>(buffer, "map", 0, map, -1);
        IoSerialiser<CmwLight, std::map<std::string, std::variant<int, double, std::string>>>::serialise(buffer, field, map);
        buffer.reset();

        // std::print("expected:\n{}\ngot:\n{}\n", hexview(expected), hexview(buffer.asString()));
        REQUIRE(buffer.asString() == expected);
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
struct Empty {};
struct StatusProperty {
    int control;
    std::vector<bool> detailedStatus;
    std::vector<std::string> detailedStatus_labels;
    std::vector<int> detailedStatus_severity;
    std::vector<int> error_codes;
    std::vector<std::string> error_cycle_names;
    std::vector<std::string> error_messages;
    std::vector<long> error_timestamps;
    bool interlock;
    bool modulesReady;
    bool opReady;
    int powerState;
    int status;
};
} // namespace opencmw::serialiser::cmwlighttests
ENABLE_REFLECTION_FOR(opencmw::serialiser::cmwlighttests::CmwLightHeaderOptions, b, e)
ENABLE_REFLECTION_FOR(opencmw::serialiser::cmwlighttests::CmwLightHeader, x_2, x_0, x_1, f, x_7, d, x_3)
ENABLE_REFLECTION_FOR(opencmw::serialiser::cmwlighttests::DigitizerVersion, classVersion, deployUnitVersion, fesaVersion, gr_flowgraph_version, gr_digitizer_version, daqAPIVersion)
ENABLE_REFLECTION_FOR(opencmw::serialiser::cmwlighttests::StatusProperty, control, detailedStatus, detailedStatus_labels, detailedStatus_severity, error_codes, error_cycle_names, error_messages, error_timestamps, interlock, modulesReady, opReady, powerState, status)
REFL_TYPE(opencmw::serialiser::cmwlighttests::Empty)
REFL_END

TEST_CASE("IoClassSerialiserCmwLight Deserialise rda3 data", "[IoClassSerialiser]") {
    // ensure that important rda3 messages can be properly deserialized
    using namespace opencmw;
    debug::resetStats();
    using namespace opencmw::serialiser::cmwlighttests;
    {
        std::vector<uint8_t> rda3ConnectReply = { //
            0x07, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, /**/ 0x30, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, //
            0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x31, /**/ 0x00, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, //
            0x00, 0x00, 0x00, 0x32, 0x00, 0x01, 0x03, 0x02, /**/ 0x00, 0x00, 0x00, 0x33, 0x00, 0x08, 0x02, 0x00, //
            0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x62, 0x00, /**/ 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            0x00, 0x02, 0x00, 0x00, 0x00, 0x65, 0x00, 0x08, /**/ 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, //
            0x37, 0x00, 0x01, 0x70, 0x02, 0x00, 0x00, 0x00, /**/ 0x64, 0x00, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00, //
            0x02, 0x00, 0x00, 0x00, 0x66, 0x00, 0x07, 0x01, /**/ 0x00, 0x00, 0x00, 0x00 };                       //
        IoBuffer       buffer{ rda3ConnectReply.data(), rda3ConnectReply.size() };
        CmwLightHeader deserialised;
        auto           result = opencmw::deserialise<CmwLight, ProtocolCheck::LENIENT>(buffer, deserialised);
        REQUIRE(result.additionalFields.empty());
        REQUIRE(result.exceptions.empty());
        REQUIRE(result.setFields["root"sv].size() == 7);

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
        //  reply req type: session confirm
        std::vector<uint8_t> data = {                                                    0x07, 0x00, 0x00, 0x00, //              ....
            0x02, 0x00, 0x00, 0x00, 0x30, 0x00, 0x04, 0x09, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, // ....0... ........
            0x00, 0x00, 0x00, 0x31, 0x00, 0x07, 0x08, 0x00, /**/ 0x00, 0x00, 0x47, 0x53, 0x43, 0x44, 0x30, 0x30, // ...1.... ..GSCD00
            0x32, 0x00, 0x02, 0x00, 0x00, 0x00, 0x32, 0x00, /**/ 0x01, 0x0b, 0x02, 0x00, 0x00, 0x00, 0x33, 0x00, // 2.....2. ......3.
            0x08, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, /**/ 0x00, 0x65, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, // ........ .e......
            0x02, 0x00, 0x00, 0x00, 0x37, 0x00, 0x01, 0x00, /**/ 0x02, 0x00, 0x00, 0x00, 0x64, 0x00, 0x07, 0x25, // ....7... ....d..%
            0x01, 0x00, 0x00, 0x52, 0x65, 0x6d, 0x6f, 0x74, /**/ 0x65, 0x48, 0x6f, 0x73, 0x74, 0x49, 0x6e, 0x66, // ...Remot eHostInf
            0x6f, 0x49, 0x6d, 0x70, 0x6c, 0x5b, 0x6e, 0x61, /**/ 0x6d, 0x65, 0x3d, 0x66, 0x65, 0x73, 0x61, 0x2d, // oImpl[na me=fesa-
            0x65, 0x78, 0x70, 0x6c, 0x6f, 0x72, 0x65, 0x72, /**/ 0x2d, 0x61, 0x70, 0x70, 0x3b, 0x20, 0x75, 0x73, // explorer -app; us
            0x65, 0x72, 0x4e, 0x61, 0x6d, 0x65, 0x3d, 0x61, /**/ 0x6b, 0x72, 0x69, 0x6d, 0x6d, 0x3b, 0x20, 0x61, // erName=a krimm; a
            0x70, 0x70, 0x49, 0x64, 0x3d, 0x5b, 0x61, 0x70, /**/ 0x70, 0x3d, 0x66, 0x65, 0x73, 0x61, 0x2d, 0x65, // ppId=[ap p=fesa-e
            0x78, 0x70, 0x6c, 0x6f, 0x72, 0x65, 0x72, 0x2d, /**/ 0x61, 0x70, 0x70, 0x3b, 0x76, 0x65, 0x72, 0x3d, // xplorer- app;ver=
            0x31, 0x39, 0x2e, 0x30, 0x2e, 0x30, 0x3b, 0x75, /**/ 0x69, 0x64, 0x3d, 0x61, 0x6b, 0x72, 0x69, 0x6d, // 19.0.0;u id=akrim
            0x6d, 0x3b, 0x68, 0x6f, 0x73, 0x74, 0x3d, 0x53, /**/ 0x59, 0x53, 0x50, 0x43, 0x30, 0x30, 0x38, 0x3b, // m;host=S YSPC008;
            0x70, 0x69, 0x64, 0x3d, 0x31, 0x39, 0x31, 0x36, /**/ 0x31, 0x36, 0x3b, 0x5d, 0x3b, 0x20, 0x70, 0x72, // pid=1916 16;]; pr
            0x6f, 0x63, 0x65, 0x73, 0x73, 0x3d, 0x66, 0x65, /**/ 0x73, 0x61, 0x2d, 0x65, 0x78, 0x70, 0x6c, 0x6f, // ocess=fe sa-explo
            0x72, 0x65, 0x72, 0x2d, 0x61, 0x70, 0x70, 0x3b, /**/ 0x20, 0x70, 0x69, 0x64, 0x3d, 0x31, 0x39, 0x31, // rer-app;  pid=191
            0x36, 0x31, 0x36, 0x3b, 0x20, 0x61, 0x64, 0x64, /**/ 0x72, 0x65, 0x73, 0x73, 0x3d, 0x74, 0x63, 0x70, // 616; add ress=tcp
            0x3a, 0x2f, 0x2f, 0x53, 0x59, 0x53, 0x50, 0x43, /**/ 0x30, 0x30, 0x38, 0x3a, 0x30, 0x3b, 0x20, 0x73, // ://SYSPC 008:0; s
            0x74, 0x61, 0x72, 0x74, 0x54, 0x69, 0x6d, 0x65, /**/ 0x3d, 0x32, 0x30, 0x32, 0x34, 0x2d, 0x30, 0x37, // tartTime =2024-07
            0x2d, 0x30, 0x34, 0x20, 0x31, 0x31, 0x3a, 0x31, /**/ 0x31, 0x3a, 0x31, 0x32, 0x3b, 0x20, 0x63, 0x6f, // -04 11:1 1:12; co
            0x6e, 0x6e, 0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e, /**/ 0x54, 0x69, 0x6d, 0x65, 0x3d, 0x41, 0x62, 0x6f, // nnection Time=Abo
            0x75, 0x74, 0x20, 0x61, 0x67, 0x6f, 0x3b, 0x20, /**/ 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x3d, // ut ago;  version=
            0x31, 0x30, 0x2e, 0x33, 0x2e, 0x30, 0x3b, 0x20, /**/ 0x6c, 0x61, 0x6e, 0x67, 0x75, 0x61, 0x67, 0x65, // 10.3.0;  language
            0x3d, 0x4a, 0x61, 0x76, 0x61, 0x5d, 0x31, 0x00, /**/ 0x02, 0x00, 0x00, 0x00, 0x66, 0x00, 0x07, 0x08, // =Java]1. ....f...
            0x00, 0x00, 0x00, 0x56, 0x65, 0x72, 0x73, 0x69, /**/ 0x6f, 0x6e, 0x00 };                             //...Versi on.
        IoBuffer       buffer{ data.data(), data.size() };
        CmwLightHeader deserialised;
        auto           result = opencmw::deserialise<CmwLight, ProtocolCheck::LENIENT>(buffer, deserialised);
        REQUIRE(result.additionalFields.empty());
        REQUIRE(result.exceptions.empty());
        REQUIRE(result.setFields["root"sv].size() == 7);
    }
    {
        // Reply with Req Type = Reply, gets sent after get request
        std::vector<uint8_t> data =  {                                                               0x06, 0x00, //                ..
            0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x63, 0x6c, /**/ 0x61, 0x73, 0x73, 0x56, 0x65, 0x72, 0x73, 0x69, // ......cl assVersi
            0x6f, 0x6e, 0x00, 0x07, 0x06, 0x00, 0x00, 0x00, /**/ 0x36, 0x2e, 0x30, 0x2e, 0x30, 0x00, 0x0e, 0x00, // on...... 6.0.0...
            0x00, 0x00, 0x64, 0x61, 0x71, 0x41, 0x50, 0x49, /**/ 0x56, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x00, // ..daqAPI Version.
            0x07, 0x04, 0x00, 0x00, 0x00, 0x32, 0x2e, 0x30, /**/ 0x00, 0x12, 0x00, 0x00, 0x00, 0x64, 0x65, 0x70, // .....2.0 .....dep
            0x6c, 0x6f, 0x79, 0x55, 0x6e, 0x69, 0x74, 0x56, /**/ 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x00, 0x07, // loyUnitV ersion..
            0x06, 0x00, 0x00, 0x00, 0x36, 0x2e, 0x30, 0x2e, /**/ 0x30, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x66, 0x65, // ....6.0. 0.....fe
            0x73, 0x61, 0x56, 0x65, 0x72, 0x73, 0x69, 0x6f, /**/ 0x6e, 0x00, 0x07, 0x06, 0x00, 0x00, 0x00, 0x37, // saVersio n......7
            0x2e, 0x33, 0x2e, 0x30, 0x00, 0x15, 0x00, 0x00, /**/ 0x00, 0x67, 0x72, 0x5f, 0x64, 0x69, 0x67, 0x69, // .3.0.... .gr_digi
            0x74, 0x69, 0x7a, 0x65, 0x72, 0x5f, 0x76, 0x65, /**/ 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x00, 0x07, 0x08, // tizer_ve rsion...
            0x00, 0x00, 0x00, 0x35, 0x2e, 0x31, 0x2e, 0x34, /**/ 0x2e, 0x30, 0x00, 0x15, 0x00, 0x00, 0x00, 0x67, // ...5.1.4 .0.....g
            0x72, 0x5f, 0x66, 0x6c, 0x6f, 0x77, 0x67, 0x72, /**/ 0x61, 0x70, 0x68, 0x5f, 0x76, 0x65, 0x72, 0x73, // r_flowgr aph_vers
            0x69, 0x6f, 0x6e, 0x00, 0x07, 0x08, 0x00, 0x00, /**/ 0x00, 0x35, 0x2e, 0x30, 0x2e, 0x32, 0x2e, 0x30, // ion..... .5.0.2.0
            0x00, 0x01, 0x62, 0x03, 0x00, 0x00, 0x00, 0x02, /**/ 0x00, 0x00, 0x00, 0x35, 0x00, 0x04, 0x88, 0x39, // ..b..... ...5...9
            0xfe, 0x41, 0x88, 0xf7, 0xde, 0x17, 0x02, 0x00, /**/ 0x00, 0x00, 0x36, 0x00, 0x04, 0x00, 0x00, 0x00, // .A...... ..6.....
            0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, /**/ 0x00, 0x78, 0x00, 0x08, 0x03, 0x00, 0x00, 0x00, // ........ .x......
            0x09, 0x00, 0x00, 0x00, 0x61, 0x63, 0x71, 0x53, /**/ 0x74, 0x61, 0x6d, 0x70, 0x00, 0x04, 0x88, 0x39, // ....acqS tamp...9
            0xfe, 0x41, 0x88, 0xf7, 0xde, 0x17, 0x05, 0x00, /**/ 0x00, 0x00, 0x74, 0x79, 0x70, 0x65, 0x00, 0x03, // .A...... ..type..
            0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, /**/ 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x00, // ........ version.
            0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x03} ;                                                     // .......
        IoBuffer         buffer{ data.data(), data.size() };
        DigitizerVersion deserialised;
        auto             result = opencmw::deserialise<CmwLight, ProtocolCheck::LENIENT>(buffer, deserialised);
        REQUIRE(result.additionalFields.empty());
        REQUIRE(result.exceptions.empty());
        REQUIRE(result.setFields["root"sv].size() == 6);
    }
    {
        std::vector<uint8_t> data{ //
            0x0d, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, /**/ 0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x00, /**/ 0x03, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, /**/ 0x00, 0x64, 0x65, 0x74, 0x61, 0x69, 0x6c, 0x65, //  ........ control. ........ .detaile
            0x64, 0x53, 0x74, 0x61, 0x74, 0x75, 0x73, 0x00, /**/ 0x09, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, /**/ 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x01, 0x16, /**/ 0x00, 0x00, 0x00, 0x64, 0x65, 0x74, 0x61, 0x69, //  dStatus. ........ ........ ...detai
            0x6c, 0x65, 0x64, 0x53, 0x74, 0x61, 0x74, 0x75, /**/ 0x73, 0x5f, 0x6c, 0x61, 0x62, 0x65, 0x6c, 0x73, /**/ 0x00, 0x10, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, /**/ 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0f, 0x00, //  ledStatu s_labels ........ ........
            0x00, 0x00, 0x6d, 0x79, 0x53, 0x74, 0x61, 0x74, /**/ 0x75, 0x73, 0x4c, 0x61, 0x62, 0x65, 0x6c, 0x31, /**/ 0x00, 0x0f, 0x00, 0x00, 0x00, 0x6d, 0x79, 0x53, /**/ 0x74, 0x61, 0x74, 0x75, 0x73, 0x4c, 0x61, 0x62, //  ..myStat usLabel1 .....myS tatusLab
            0x65, 0x6c, 0x32, 0x00, 0x18, 0x00, 0x00, 0x00, /**/ 0x64, 0x65, 0x74, 0x61, 0x69, 0x6c, 0x65, 0x64, /**/ 0x53, 0x74, 0x61, 0x74, 0x75, 0x73, 0x5f, 0x73, /**/ 0x65, 0x76, 0x65, 0x72, 0x69, 0x74, 0x79, 0x00, //  el2..... detailed Status_s everity.
            0x0c, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, /**/ 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, /**/ 0x00, 0x65, 0x72, 0x72, 0x6f, 0x72, 0x5f, 0x63, //  ........ ........ ........ .error_c
            0x6f, 0x64, 0x65, 0x73, 0x00, 0x0c, 0x01, 0x00, /**/ 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //  odes.... ........ ........ ........
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //  ........ ........ ........ ........
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x65, 0x72, /**/ 0x72, 0x6f, 0x72, 0x5f, 0x63, 0x79, 0x63, 0x6c, //  ........ ........ ......er ror_cycl
            0x65, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x73, 0x00, /**/ 0x10, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, /**/ 0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, /**/ 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, //  e_names. ........ ........ ........
            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, /**/ 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, /**/ 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, /**/ 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, //  ........ ........ ........ ........
            0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, /**/ 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, /**/ 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, /**/ 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, //  ........ ........ ........ ........
            0x01, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, /**/ 0x00, 0x65, 0x72, 0x72, 0x6f, 0x72, 0x5f, 0x6d, /**/ 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x73, 0x00, /**/ 0x10, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, //  ........ .error_m essages. ........
            0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, /**/ 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, /**/ 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, /**/ 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //  ........ ........ ........ ........
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, /**/ 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, /**/ 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, /**/ 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, //  ........ ........ ........ ........
            0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, /**/ 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, /**/ 0x01, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, /**/ 0x00, 0x65, 0x72, 0x72, 0x6f, 0x72, 0x5f, 0x74, //  ........ ........ ........ .error_t
            0x69, 0x6d, 0x65, 0x73, 0x74, 0x61, 0x6d, 0x70, /**/ 0x73, 0x00, 0x0d, 0x01, 0x00, 0x00, 0x00, 0x10, /**/ 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //  imestamp s....... ........ ........
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //  ........ ........ ........ ........
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //  ........ ........ ........ ........
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //  ........ ........ ........ ........
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /**/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, /**/ 0x00, 0x00, 0x00, 0x69, 0x6e, 0x74, 0x65, 0x72, //  ........ ........ ........ ...inter
            0x6c, 0x6f, 0x63, 0x6b, 0x00, 0x00, 0x00, 0x0d, /**/ 0x00, 0x00, 0x00, 0x6d, 0x6f, 0x64, 0x75, 0x6c, /**/ 0x65, 0x73, 0x52, 0x65, 0x61, 0x64, 0x79, 0x00, /**/ 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x6f, 0x70, //  lock.... ...modul esReady. ......op
            0x52, 0x65, 0x61, 0x64, 0x79, 0x00, 0x00, 0x01, /**/ 0x0b, 0x00, 0x00, 0x00, 0x70, 0x6f, 0x77, 0x65, /**/ 0x72, 0x53, 0x74, 0x61, 0x74, 0x65, 0x00, 0x03, /**/ 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, //  Ready... ....powe rState.. ........
            0x73, 0x74, 0x61, 0x74, 0x75, 0x73, 0x00, 0x03, /**/ 0x01, 0x00, 0x00, 0x00};                                                                                                                                  //  status.. ....
        IoBuffer         buffer{ data.data(), data.size() };
        REQUIRE(buffer.size() == 748);
        // deserialise once into an empty struct to verify that the fields can be correctly skipped and end up in the deserialiserInfo
        Empty empty;
        auto result = opencmw::deserialise<CmwLight, ProtocolCheck::LENIENT>(buffer, empty);
        REQUIRE(result.additionalFields.size() == 13); // [root::control, root::detailedStatus, root::detailedStatus_labels, root::detailedStatus_severity, root::error_codes, root::error_cycle_names, root::error_messages, root::error_timestamps, root::interlock, root::modulesReady, root::opReady, root::powerState, root::status] [3, 9, 16, 12, 12, 16, 16, 13, 0, 0, 0, 3, 3]
        REQUIRE(result.exceptions.size() == 13); // each missing field also produces an excception
        REQUIRE(result.setFields["root"].empty());
        // deserialise into the correct domain object
        buffer.reset();
        StatusProperty status;
        auto result2 = opencmw::deserialise<CmwLight, ProtocolCheck::LENIENT>(buffer, status);
        REQUIRE(result2.additionalFields.empty());
        REQUIRE(result2.exceptions.empty());
        REQUIRE(result2.setFields["root"sv].size() == 13);
        REQUIRE(status.control == 0);
        REQUIRE(status.detailedStatus == std::vector{true, true});
        REQUIRE(status.detailedStatus_labels == std::vector{"myStatusLabel1"s, "myStatusLabel2"s});
        REQUIRE(status.detailedStatus_severity == std::vector{0, 0});
        REQUIRE(status.error_codes.size() == 16);
        REQUIRE(status.error_cycle_names.size() == 16);
        REQUIRE(status.error_messages.size() == 16);
        REQUIRE(status.error_timestamps.size() == 16);
        REQUIRE_FALSE(status.interlock);
        REQUIRE(status.modulesReady);
        REQUIRE(status.opReady);
        REQUIRE(status.powerState == 1);
        REQUIRE(status.status == 1);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}
