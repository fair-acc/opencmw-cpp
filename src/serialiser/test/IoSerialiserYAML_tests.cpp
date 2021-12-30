#include <algorithm>
#include <catch2/catch.hpp>
#include <Debug.hpp>
#include <IoSerialiserYAML.hpp>
#include <iostream>
#include <string_view>
#include <Utils.hpp>

#include <units/isq/si/electric_current.h>
#include <units/isq/si/energy.h>
#include <units/isq/si/length.h>
#include <units/isq/si/mass.h>
#include <units/isq/si/resistance.h>
#include <units/isq/si/speed.h>
#include <units/isq/si/time.h>

using namespace opencmw;
using namespace units::isq;
using namespace units::isq::si;
using namespace std::literals;

using opencmw::Annotated;
using opencmw::NoUnit;
using opencmw::ProtocolCheck;
using opencmw::ExternalModifier::RO;
using opencmw::ExternalModifier::RW;
using opencmw::ExternalModifier::RW_DEPRECATED;
using opencmw::ExternalModifier::RW_PRIVATE;

struct NestedData {
    Annotated<int8_t, length<metre>, "nested int8_t">                          annByteValue   = 11;
    Annotated<int16_t, si::time<second>, "custom description for int16_t">     annShortValue  = 12;
    Annotated<int32_t, NoUnit, "custom description for int32_t">               annIntValue    = 13;
    Annotated<int64_t, NoUnit, "custom description for int64_t">               annLongValue   = 14;
    Annotated<float, energy<gigaelectronvolt>, "custom description for float"> annFloatValue  = 15.0F;
    Annotated<double, mass<kilogram>, "custom description for double", RW>     annDoubleValue = 16.0;
    Annotated<std::string, NoUnit, "custom description for string">            annStringValue = std::string("nested string");
    Annotated<std::array<double, 10>, NoUnit>                                  annDoubleArray = std::array<double, 10>{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    Annotated<std::vector<float>, NoUnit>                                      annFloatVector = std::vector{ 0.1f, 1.1f, 2.1f, 3.1f, 4.1f, 5.1f, 6.1f, 8.1f, 9.1f, 9.1f };

    // some default operator
    auto operator<=>(const NestedData &) const = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(NestedData, annByteValue, annShortValue, annIntValue, annLongValue, annFloatValue, annDoubleValue, annStringValue, annDoubleArray, annFloatVector)

struct DataY {
    int8_t                             byteValue        = 1;
    int16_t                            shortValue       = 2;
    int32_t                            intValue         = 3;
    int64_t                            longValue        = 4;
    float                              floatValue       = 5.0F;
    double                             doubleValue      = 6.0;
    std::string                        stringValue      = "bare string";
    std::string const                  constStringValue = "unmodifiable string";
    std::array<double, 10>             doubleArray      = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    std::array<double, 10> const       constDoubleArray = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    std::vector<float>                 floatVector      = { 0.1F, 1.1F, 2.1F, 3.1F, 4.1F, 5.1F, 6.1F, 8.1F, 9.1F, 9.1F };
    opencmw::MultiArray<double, 2>     doubleMatrix{ { 1, 3, 7, 4, 2, 3 }, { 2, 3 } };
    NestedData                         nestedData;
    Annotated<double, resistance<ohm>> annotatedValue = 0.1;
    //
    // Annotated<std::map<std::string, std::string>, NoUnit, "a simple std::map"> map = {{"key1","value1"},{"key2","value2"},{"key3","value3"},{"key4","value4"}};
    std::map<std::string, std::string> map                             = { { "key1", "value1" }, { "key2", "value2" }, { "key3", "value3" }, { "key4", "value4" }, { "key5", "value5" }, { "key6", "value6" } };
    std::map<std::string, std::string> smallMap                        = { { "key1", "value1" } };

    bool                               operator==(const DataY &) const = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(DataY, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, constStringValue, doubleArray, floatVector, /*doubleMatrix,*/ nestedData, annotatedValue, map, smallMap)

template<opencmw::SerialiserProtocol protocol, opencmw::ReflectableClass T>
void checkSerialiserIdentity(opencmw::IoBuffer &buffer, const T &a, T &b) {
    buffer.clear();
    opencmw::serialise<protocol>(buffer, a);

    try {
        buffer.reset();
        opencmw::deserialise<protocol, ProtocolCheck::IGNORE>(buffer, b);
    } catch (const opencmw::ProtocolException &e) {
        std::cout << "caught ProtocolException " << opencmw::typeName<std::remove_reference_t<decltype(e)>> << " - " << what() << std::endl;
        REQUIRE(false);
    } catch (const std::exception &e) {
        std::cout << "caught exception " << opencmw::typeName<std::remove_reference_t<decltype(e)>> << " - " << what() << std::endl;
        REQUIRE(false);
    } catch (...) {
        std::cout << "caught unknown exception "
                  << " - " << what() << std::endl;
        REQUIRE(false);
    }
}

TEST_CASE("basic YAML serialisation", "[IoClassSerialiserYAML]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
    using namespace opencmw::utils; // for operator<< and fmt::format overloading
    debug::resetStats();
    {
        debug::Timer timer("IoClassSerialiser basic syntax", 30);
        IoBuffer     buffer;
        DataY        data;

        std::cout << fmt::format("buffer size (before): {} bytes\n", buffer.size());

        DataY data2;
        REQUIRE(data == data2);
        data2.byteValue      = 30;
        data2.shortValue     = 3;
        data2.stringValue    = "change me";
        data2.annotatedValue = 0.2;
        data2.doubleArray[3] = 99;
        data2.floatVector.clear();
        data2.nestedData.annByteValue  = '\0';
        data2.nestedData.annFloatValue = 12.0F;
        data2.nestedData.annFloatValue *= 2.0F;
        data2.nestedData.annStringValue    = "different text";
        data2.nestedData.annDoubleArray[3] = 99;
        //        data2.doubleMatrix(0U, 0U)         = 42;
        data2.nestedData.annFloatVector.clear();
        data2.map["key7"] = "value7";
        data2.smallMap.clear();
        REQUIRE(data != data2);

        //        std::cout << "object (short): " << ClassInfoShort << data << '\n';
        //        std::cout << fmt::format("object (fmt): {}\n", data);
        //        std::cout << "object (long):  " << ClassInfoVerbose << data << '\n';

        opencmw::serialise<opencmw::YAML>(buffer, data);
        std::cout << "YAML - output:\n"
                  << buffer.asString() << std::endl;

        // check (de-)serialisation identity
        std::cout << ClassInfoVerbose << "before: ";
        diffView(std::cout, data, data2);
        checkSerialiserIdentity<opencmw::YAML>(buffer, data, data2);
        std::cout << fmt::format("buffer size (after): {} bytes\n", buffer.size());
        std::cout << "after: " << std::flush;
        diffView(std::cout, data, data2);
        REQUIRE(data == data2);
        //
        //        REQUIRE(data.doubleMatrix(0U, 0U) == data2.doubleMatrix(0U, 0U));
        //        REQUIRE(data.doubleMatrix(1U, 2U) == data2.doubleMatrix(1U, 2U));
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}
