#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"

#include <catch2/catch.hpp>
#include <Debug.hpp>
#include <IoSerialiserCmwLight.hpp>
#include <iostream>
#include <string_view>
#include <Utils.hpp>

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
    int                            a   = 1337;
    float                          ab  = 13.37f;
    double                         abc = 42.23;
    std::string                    b   = "hello";
    std::array<int, 3>             c{ 3, 2, 1 };
    std::vector<double>            cd{ 2.3, 3.4, 4.5, 5.6 };
    std::vector<std::string>       ce{ "hello", "world" };
    opencmw::MultiArray<double, 2> d{ { 1, 2, 3, 4, 5, 6 }, { 2, 3 } };
    // auto operator<=>(const SimpleTestData &) const = default; // commented because starship is missing for multi array
    bool operator==(const SimpleTestData &) const = default;
};
} // namespace ioserialiser_cmwlight_test
ENABLE_REFLECTION_FOR(ioserialiser_cmwlight_test::SimpleTestData, a, ab, abc, b, c, cd, ce, d)

TEST_CASE("IoClassSerialiserCmwLight simple test", "[IoClassSerialiser]") {
    using namespace opencmw;
    using namespace opencmw::utils; // for operator<< and fmt::format overloading
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
            .d   = { { 6, 5, 4, 3, 2, 1 }, { 3, 2 } }
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

#pragma clang diagnostic pop