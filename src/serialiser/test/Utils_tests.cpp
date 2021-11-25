#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"

#include <Debug.hpp>
#include <Utils.hpp>
#include <algorithm>
#include <catch2/catch.hpp>
#include <iostream>
#include <string_view>

#include <units/isq/si/electric_current.h>
#include <units/isq/si/energy.h>
#include <units/isq/si/length.h>
#include <units/isq/si/mass.h>
#include <units/isq/si/resistance.h>
#include <units/isq/si/speed.h>
#include <units/isq/si/time.h>

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

struct ClassA {
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
    auto operator<=>(const ClassA &) const = default;
    bool operator==(const ClassA &) const  = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(ClassA, annByteValue, annShortValue, annIntValue, annLongValue, annFloatValue, annDoubleValue, annStringValue, annDoubleArray, annFloatVector)
// ENABLE_REFLECTION_FOR(NestedData, annByteValue, annShortValue, annIntValue, annLongValue, annFloatValue, annDoubleValue, annDoubleArray, annFloatVector)

struct ClassB {
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
    ClassA                             nestedData;
    Annotated<double, resistance<ohm>> annotatedValue = 0.1;

    ClassB()                                          = default;
    bool operator==(const ClassB &) const             = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(ClassB, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, constStringValue, doubleArray, floatVector, doubleMatrix, nestedData, annotatedValue)

TEST_CASE("diff view test non-nested", "[Utils]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
    using namespace opencmw::utils; // for operator<< and fmt::format overloading
    debug::resetStats();
    {
        ClassA data;
        ClassA data2;
        std::cerr << ClassInfoVerbose;
        std::cerr << " data1 " << data << std::endl;
        std::cerr << " data2 " << data2 << std::endl;
        diffView(std::cerr, data, data2);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}
TEST_CASE("diff view test nested", "[Utils]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
    using namespace opencmw::utils; // for operator<< and fmt::format overloading
    debug::resetStats();
    {
        ClassB data;
        ClassB data2;
        std::cerr << ClassInfoVerbose;
        std::cerr << " data1 " << data << std::endl;  //TODO: add/remove breakpoint here -- segfaults here
        std::cerr << " data2 " << data2 << std::endl; //TODO: add/remove breakpoint here -- segfaults here
        diffView(std::cerr, data, data2);             // TODO: add/remove breakpoint here -- segfaults here
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

TEST_CASE("reflectable object stdout flat", "[Utils]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
    using namespace opencmw::utils; // for operator<< and fmt::format overloading
    debug::resetStats();
    {
        std::ostringstream os;
        ClassA             data;
        os << ClassInfoVerbose << data; // TODO: add/remove breakpoint here -- segfaults here
        // does not work because of the tabs inside of the output
        //         REQUIRE(os.str() == R"--(ClassA(
        //   0: Annotated<int8_t>  ClassA::annByteValue               = 11     // [m] - nested int8_t
        //   1: Annotated<int16_t> ClassA::annShortValue              = 12     // [s] - custom description for int16_t
        //   2: Annotated<int32_t> ClassA::annIntValue                = 13     // [] - custom description for int32_t
        //   3: Annotated<int64_t> ClassA::annLongValue               = 14     // [] - custom description for int64_t
        //   4: Annotated<float_t> ClassA::annFloatValue              = 15     // [GeV] - custom description for float
        //   5: Annotated<double_t>    ClassA::annDoubleValue             = 16     // [kg] - custom description for double
        //   6: Annotated<string>  ClassA::annStringValue             = nested string  // [] - custom description for string
        //   7: Annotated<array<double_t,10>>ClassA::annDoubleArray             = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}  // [] -
        //   8: Annotated<vector<float_t>> ClassA::annFloatVector             = {0.1, 1.1, 2.1, 3.1, 4.1, 5.1, 6.1, 8.1, 9.1, 9.1}  // [] -
        //  ))--");
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

TEST_CASE("Annotated stdout", "[Utils]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
    using namespace opencmw::utils; // for operator<< and fmt::format overloading
    debug::resetStats();
    {
        std::ostringstream                                              os;
        Annotated<std::string, NoUnit, "custom description for string"> annStringValue = std::string("nested string");
        os << ClassInfoVerbose << annStringValue;
        REQUIRE(os.str() == "nested string  // [] - custom description for string");
    }
    {
        std::ostringstream os;
        int64_t            longValue = 4;
        os << ClassInfoVerbose << longValue;
        REQUIRE(os.str() == "4");
    }
    {
        std::ostringstream os;
        float              floatValue = 5.0F;
        os << ClassInfoVerbose << floatValue;
        REQUIRE(os.str() == "5");
    }
    {
        std::ostringstream os;
        double             doubleValue = 6.0;
        os << ClassInfoVerbose << doubleValue;
        REQUIRE(os.str() == "6");
    }
    {
        std::ostringstream os;
        std::string        stringValue = "bare string";
        os << ClassInfoVerbose << stringValue;
        REQUIRE(os.str() == "bare string");
    }
    {
        std::ostringstream os;
        std::string const  constStringValue = "unmodifiable string";
        os << ClassInfoVerbose << constStringValue;
        REQUIRE(os.str() == "unmodifiable string");
    }
    {
        std::ostringstream                                           os;
        Annotated<int64_t, NoUnit, "custom description for int64_t"> annLongValue = 14;
        os << ClassInfoVerbose << annLongValue;
        REQUIRE(os.str() == "14     // [] - custom description for int64_t");
    }
    {
        std::ostringstream                                                         os;
        Annotated<float, energy<gigaelectronvolt>, "custom description for float"> annFloatValue = 15.0F;
        os << ClassInfoVerbose << annFloatValue;
        REQUIRE(os.str() == "15     // [GeV] - custom description for float");
    }
    {
        std::ostringstream                                                     os;
        Annotated<double, mass<kilogram>, "custom description for double", RW> annDoubleValue = 16.0;
        os << ClassInfoVerbose << annDoubleValue;
        REQUIRE(os.str() == "16     // [kg] - custom description for double");
    }
    {
        std::ostringstream                        os;
        Annotated<std::array<double, 10>, NoUnit> annDoubleArray = std::array<double, 10>{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        os << ClassInfoVerbose << annDoubleArray;
        REQUIRE(os.str() == "{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}  // [] - ");
    }
    {
        std::ostringstream                    os;
        Annotated<std::vector<float>, NoUnit> annFloatVector = std::vector{ 0.1f, 1.1f, 2.1f, 3.1f, 4.1f, 5.1f, 6.1f, 8.1f, 9.1f, 9.1f };
        os << ClassInfoVerbose << annFloatVector;
        REQUIRE(os.str() == "{0.1, 1.1, 2.1, 3.1, 4.1, 5.1, 6.1, 8.1, 9.1, 9.1}  // [] - ");
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

#pragma clang diagnostic pop
