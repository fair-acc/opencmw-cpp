#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"

#include <Debug.h>
#include <IoClassSerialiser.h>
#include <IoSerialiser.h>
#include <Utils.h>
#include <catch2/catch.hpp>
#include <iostream>
#include <string_view>

using opencmw::Annotated;
using namespace std::literals;
struct NestedData {
    Annotated<int8_t, "unit1", "nested int8_t", "IN/OUT">                      byteValue   = 11;
    Annotated<int16_t, "unit2", "custom description for int16_t", "IN/OUT">    shortValue  = 12;
    Annotated<int32_t, "unit3", "custom description for int32_t", "IN/OUT">    intValue    = 13;
    Annotated<int64_t, "unit4", "custom description for int64_t", "IN/OUT">    longValue   = 14;
    Annotated<float, "unit5", "custom description for float", "IN/OUT">        floatValue  = 15.0F;
    Annotated<double, "unit6", "custom description for double", "IN/OUT">      doubleValue = 16.0;
    Annotated<std::string, "unit7", "custom description for string", "IN/OUT"> stringValue = std::string("nested string");
    Annotated<std::array<double, 10>, "unit8">                                 doubleArray = std::array<double, 10>{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    Annotated<std::vector<float>, "unit9">                                     floatVector = std::vector{ 0.1f, 1.1f, 2.1f, 3.1f, 4.1f, 5.1f, 6.1f, 8.1f, 9.1f, 9.1f };

    // some default operator
    auto operator<=>(const NestedData &) const = default;
    bool operator==(const NestedData &) const  = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
REFL_CUSTOM(NestedData, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, doubleArray, floatVector)

struct Data {
    int8_t                   byteValue   = 1;
    int16_t                  shortValue  = 2;
    int32_t                  intValue    = 3;
    int64_t                  longValue   = 4;
    float                    floatValue  = 5.0F;
    double                   doubleValue = 6.0;
    std::string              stringValue = "bare string";
    std::array<double, 10>   doubleArray = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    std::vector<float>       floatVector = { 0.1f, 1.1f, 2.1f, 3.1f, 4.1f, 5.1f, 6.1f, 8.1f, 9.1f, 9.1f };
    NestedData               nestedData;
    Annotated<double, "Ohm"> annotatedValue = 0.1;

    Data()                                  = default;
    auto operator<=>(const Data &) const    = default;
    bool operator==(const Data &) const     = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
REFL_CUSTOM(Data, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, doubleArray, floatVector, nestedData, annotatedValue) //TODO: reenable nestedData

TEST_CASE("IoClassSerialiser basic syntax", "[IoClassSerialiser]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
    using namespace opencmw::utils; // for operator<< and fmt::format overloading
    debug::resetStats();
    {
        debug::Timer timer("IoClassSerialiser basic syntax", 30);
        IoBuffer     buffer;
        Data         data;

        std::cout << fmt::format("buffer size (before): {} bytes\n", buffer.size());

        Data data2;
        REQUIRE(data == data2);
        data2.byteValue      = 30;
        data2.shortValue     = 3;
        data2.stringValue    = "change me";
        data2.annotatedValue = 0.2;
        data2.doubleArray[3] = 99;
        data2.floatVector.clear();
        data2.nestedData.byteValue            = '\0';
        data2.nestedData.floatValue           = 12.0F;
        data2.nestedData.stringValue          = "different text";
        data2.nestedData.doubleArray.value[3] = 99;
        data2.nestedData.floatVector.value.clear();
        REQUIRE(data != data2);

        opencmw::serialise<opencmw::YaS>(buffer, data);

        std::cout << "object (short): " << ClassInfoShort << data << '\n';
        std::cout << fmt::format("object (fmt): {}\n", data);
        std::cout << "object (long):  " << ClassInfoVerbose << data << '\n';

        std::cout << ClassInfoVerbose << "before: ";
        diffView(std::cout, data, data2);
        std::cout << fmt::format("buffer size (after): {} bytes\n", buffer.size());
        buffer.reset();

        try {
            opencmw::deserialise<opencmw::YaS>(buffer, data2);
        } catch (std::exception &e) {
            std::cout << "caught exception " << typeName<std::remove_reference_t<decltype(e)>>() << std::endl;
        } catch (...) {
            std::cout << "caught unknown exception " << std::endl;
        }
        std::cout << "after: " << std::flush;
        diffView(std::cout, data, data2);
    }
    //REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
    std::cout << "finished test\n";
}

#pragma clang diagnostic pop
