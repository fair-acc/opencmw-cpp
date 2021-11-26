#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"

#include <Debug.hpp>
#include <YaSerialiser.hpp>
#include <catch2/catch.hpp>
#include <iostream>
#include <string_view>

#include <units/isq/si/length.h>
#include <units/isq/si/speed.h>

using namespace units::isq;
using namespace units::isq::si;
using NoUnit = units::dimensionless<units::one>;
using namespace std::literals;
using opencmw::ExternalModifier::RW;

struct Data {
    opencmw::Annotated<double, si::speed<metre_per_second>, "custom description", RW, "groupA"> value;
    // bla blaa

    Data(double val = 0)
        : value(val){};
};

TEST_CASE("IoSerialiser syntax", "[IoSerialiser]") {
    opencmw::debug::resetStats();
    {
        opencmw::debug::Timer timer("IoSerialiser syntax", 30);
        std::cout << "run IoSerialiser test\n";
        std::cout << "type name: " << opencmw::typeName<std::byte> << '\n';
        std::cout << "type name: " << opencmw::typeName<char> << '\n';
        std::cout << "type name: " << opencmw::typeName<const char> << '\n';
        std::cout << "type name: " << opencmw::typeName<int[2]> << '\n'; //NOLINT
        const int a[2] = { 1, 2 };
        std::cout << "type name: " << opencmw::typeName<decltype(a)> << '\n';
        std::cout << "type name: " << opencmw::typeName<short *> << '\n';
        std::cout << "type name: " << opencmw::typeName<const short *> << '\n';
        std::cout << "type name: " << opencmw::typeName<short *const> << '\n';

        opencmw::IoBuffer buffer;
        Data              data;
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

TEST_CASE("IoSerialiser basic syntax", "[IoSerialiser]") {
    opencmw::debug::resetStats();
    {
        opencmw::debug::Timer timer("IoSerialiser basic syntax", 30);

        opencmw::IoBuffer     buffer;
        Data                  data(42);

        REQUIRE(opencmw::is_annotated<decltype(data.value)> == true);
        std::cout << fmt::format("buffer size (before): {} bytes\n", buffer.size());

        opencmw::FieldHeader<opencmw::YaS>::putFieldHeader<true>(buffer, "fieldNameA", strlen("fieldNameA"), std::move(43.0));
        opencmw::FieldHeader<opencmw::YaS>::putFieldHeader<true>(buffer, "fieldNameB", strlen("fieldNameB"), data.value.value());
        std::cout << fmt::format("buffer size (after): {} bytes\n", buffer.size());
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

TEST_CASE("IoSerialiser primitive numbers YaS", "[IoSerialiser]") {
    opencmw::debug::resetStats();
    {
        using namespace std::literals; // for using the ""sv operator
        opencmw::debug::Timer timer("IoSerialiser numbers", 30);

        opencmw::IoBuffer     buffer;
        auto                  oldBufferPosition = buffer.position();
        constexpr auto        expectedSize      = []<typename T>(const T &value) {
            if constexpr (opencmw::is_stringlike<T> && requires { value.size(); }) {
                return (value.size() + 1) * sizeof(char) + sizeof(int32_t); // '+1' for '\0' terminating character, 4 for storing the string length
            }
            return sizeof(value);
        };
        auto writeTest = [&buffer, &oldBufferPosition, &expectedSize]<typename T, opencmw::SerialiserProtocol protocol = opencmw::YaS>(T && value) {
            const auto &msg = fmt::format("writeTest(IoBuffer&, size_t&,({}){})", opencmw::typeName<T>, std::forward<T>(value));
            REQUIRE_MESSAGE(buffer.size() == oldBufferPosition, msg);
            opencmw::IoSerialiser<protocol, T>::serialise(buffer, std::string(opencmw::typeName<T>) + "TestDataClass", value);
            REQUIRE_MESSAGE((buffer.size() - oldBufferPosition) == expectedSize(value), msg);
            oldBufferPosition += expectedSize(value);
        };

        std::cout << fmt::format("buffer size (before): {} bytes\n", buffer.size());
        writeTest(static_cast<int8_t>(1));
        writeTest(static_cast<int16_t>(2));
        writeTest(3);
        writeTest(static_cast<int64_t>(4));
        writeTest(static_cast<float>(5));
        writeTest(static_cast<double>(6));
        writeTest(std::string("Hello World!"));
        writeTest("Hello World!"sv);
        writeTest(std::string("Γειά σου Κόσμε!"));
        writeTest("Γειά σου Κόσμε!"sv);

        buffer.reset();
        oldBufferPosition = buffer.position();
        REQUIRE(oldBufferPosition == 0);
        auto readTest = [&buffer, &oldBufferPosition, &expectedSize]<typename T, opencmw::SerialiserProtocol protocol = opencmw::YaS>(T expected) {
            const auto &msg = fmt::format("ioserialiser_basicReadTests(basicReadTest&, size_t&,({}){})", opencmw::typeName<T>, expected);
            T           actual;
            opencmw::IoSerialiser<protocol, T>::deserialise(buffer, std::string(opencmw::typeName<T>) + "TestDataClass", actual);
            REQUIRE_MESSAGE(actual == expected, msg);
            REQUIRE_MESSAGE((buffer.position() - oldBufferPosition) == expectedSize(expected), msg);
            oldBufferPosition = buffer.position();
        };
        readTest(static_cast<int8_t>(1));
        readTest(static_cast<int16_t>(2));
        readTest(3);
        readTest(static_cast<int64_t>(4));
        readTest(static_cast<float>(5));
        readTest(static_cast<double>(6));
        readTest(std::string("Hello World!"));
        readTest(std::string("Hello World!")); // read string that was written as string_view
        readTest(std::string("Γειά σου Κόσμε!"));
        readTest(std::string("Γειά σου Κόσμε!")); // read string that was written as string_view

        std::cout << fmt::format("buffer size (after): {} bytes\n", buffer.size());
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

#pragma clang diagnostic pop