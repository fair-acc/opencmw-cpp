#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#include <catch2/catch.hpp>

#include <algorithm>
#include <iostream>
#include <opencmw.hpp>
#include <string_view>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast" // suppress warning caused by gsl-lite: https://github.com/gsl-lite/gsl-lite/issues/325
#include <units/isq/si/electric_current.h>
#include <units/isq/si/energy.h>
#include <units/isq/si/length.h>
#include <units/isq/si/mass.h>
#include <units/isq/si/resistance.h>
#include <units/isq/si/speed.h>
#include <units/isq/si/time.h>
#pragma GCC diagnostic pop

#include <Debug.hpp>
#include <IoSerialiserYaS.hpp>

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
    bool operator==(const NestedData &) const  = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(NestedData, annByteValue, annShortValue, annIntValue, annLongValue, annFloatValue, annDoubleValue, annStringValue, annDoubleArray, annFloatVector)

struct Data {
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

    Data()                                            = default;
    bool operator==(const Data &) const               = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(Data, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, constStringValue, doubleArray, floatVector, doubleMatrix, nestedData, annotatedValue)

template<opencmw::SerialiserProtocol protocol, opencmw::ReflectableClass T>
void checkSerialiserIdentity(opencmw::IoBuffer &buffer, const T &a, T &b) {
    buffer.clear();
    opencmw::serialise<protocol>(buffer, a);

    try {
        buffer.reset();
        opencmw::deserialise<protocol, ProtocolCheck::IGNORE>(buffer, b);
    } catch (std::exception &e) {
        std::cout << "caught exception " << opencmw::typeName<std::remove_reference_t<decltype(e)>> << std::endl;
        REQUIRE(false);
    } catch (opencmw::ProtocolException &e) {
        std::cout << "caught exception " << opencmw::typeName<std::remove_reference_t<decltype(e)>> << std::endl;
        REQUIRE(false);
    } catch (...) {
        std::cout << "caught unknown exception " << std::endl;
        REQUIRE(false);
    }
}

TEST_CASE("IoClassSerialiser basic syntax", "[IoClassSerialiser]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
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
        data2.nestedData.annByteValue  = '\0';
        data2.nestedData.annFloatValue = 12.0F;
        data2.nestedData.annFloatValue *= 2.0F;
        data2.nestedData.annStringValue    = "different text";
        data2.nestedData.annDoubleArray[3] = 99;
        data2.doubleMatrix(0U, 0U)         = 42;
        data2.nestedData.annFloatVector.clear();
        REQUIRE(data != data2);

        opencmw::serialise<opencmw::YaS>(buffer, data);
        std::cout << "object (short): " << ClassInfoShort << data << '\n';
        std::cout << fmt::format("object (fmt): {}\n", data);
        std::cout << "object (long):  " << ClassInfoVerbose << data << '\n';

        // check (de-)serialisation identity
        std::cout << ClassInfoVerbose << "before: ";
        diffView(std::cout, data, data2);
        checkSerialiserIdentity<opencmw::YaS>(buffer, data, data2);
        std::cout << fmt::format("buffer size (after): {} bytes\n", buffer.size());
        std::cout << "after: " << std::flush;
        diffView(std::cout, data, data2);
        REQUIRE(data == data2);

        REQUIRE(data.doubleMatrix(0U, 0U) == data2.doubleMatrix(0U, 0U));
        REQUIRE(data.doubleMatrix(1U, 2U) == data2.doubleMatrix(1U, 2U));
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

struct NestedDataWithDifferences {
    Annotated<int8_t, length<metre>, "nested int8_t">                             annByteValue   = 11;
    Annotated<int16_t, si::time<second>, "custom description for int16_t">        annShortValue  = 12;
    Annotated<float, NoUnit, "type mismatch">                                     annIntValue    = 13; // <- type mismatch
    Annotated<int64_t, length<metre>, "unit mismatch">                            annLongValue   = 14; // <- unit mismatch
    Annotated<float, energy<gigaelectronvolt>, "custom description for float">    annFloatValue  = 15.0F;
    Annotated<double, mass<kilogram>, "custom description for double", RO>        annDoubleValue = 16.0;                         // <- read-only specifier
    Annotated<std::string, NoUnit, "deprecation notice", RW_DEPRECATED>           annStringValue = std::string("nested string"); // <- extra deprecation specifier
    Annotated<std::array<double, 10>, NoUnit, "private field notice", RW_PRIVATE> annDoubleArray = std::array<double, 10>{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    // Annotated<std::vector<float>, NoUnit>                                                annFloatVector; // <- missing field
    Annotated<std::string, NoUnit, "custom description for string"> annExtraValue = std::string("nested string"); // <- extra value

    // some default operator
    auto operator<=>(const NestedDataWithDifferences &) const = default;
    bool operator==(const NestedDataWithDifferences &) const  = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(NestedDataWithDifferences, annByteValue, annShortValue, annIntValue, annLongValue, annFloatValue, annDoubleValue, annStringValue, annDoubleArray, annExtraValue)

TEST_CASE("IoClassSerialiser protocol mismatch", "[IoClassSerialiser]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
    debug::resetStats();
    {
        debug::Timer              timer("IoClassSerialiser protocol mismatch", 30);
        IoBuffer                  buffer;
        NestedData                data;
        NestedDataWithDifferences data2;
        REQUIRE(typeid(decltype(data)) != typeid(decltype(data2)));

        opencmw::serialise<opencmw::YaS, true>(buffer, data);

        buffer.reset();
        const auto infoNoExceptions = opencmw::deserialise<YaS, ProtocolCheck::IGNORE>(buffer, data2);
        REQUIRE(infoNoExceptions.exceptions.empty());

        buffer.reset();
        bool caughtRequiredException = false;
        try {
            opencmw::deserialise<YaS, ProtocolCheck::ALWAYS>(buffer, data2);
        } catch (ProtocolException &e) {
            caughtRequiredException = true;
        } catch (std::exception &e) {
            std::cerr << "generic exception: " << e.what() << std::endl;
        } catch (...) {
            FAIL("unknown exception");
        }
        REQUIRE(caughtRequiredException); // did not catch exception

        buffer.reset();
        auto info = opencmw::deserialise<YaS, ProtocolCheck::LENIENT>(buffer, data2);
        std::cout << " info: {}\n"
                  << info;
        REQUIRE(7 == info.exceptions.size());
        REQUIRE(1 == info.additionalFields.size());
        REQUIRE(1 == info.setFields.size());
        REQUIRE(9 == (info.setFields["root"].size()));
        REQUIRE(5 == std::count_if(info.setFields["root"].begin(), info.setFields["root"].end(), [](bool bit) { return bit == true; }));
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

TEST_CASE("IoClassSerialiser ExternalModifier tests", "[IoClassSerialiser]") {
    using namespace opencmw;
    STATIC_REQUIRE(is_readonly(ExternalModifier::RO));
    STATIC_REQUIRE(!is_readonly(ExternalModifier::RW));
    STATIC_REQUIRE(is_readonly(ExternalModifier::RO_DEPRECATED));
    STATIC_REQUIRE(!is_readonly(ExternalModifier::RW_DEPRECATED));
    STATIC_REQUIRE(is_readonly(ExternalModifier::RO_PRIVATE));
    STATIC_REQUIRE(!is_readonly(ExternalModifier::RW_PRIVATE));
    STATIC_REQUIRE(is_readonly(ExternalModifier::UNKNOWN));

    STATIC_REQUIRE(!is_deprecated(ExternalModifier::RO));
    STATIC_REQUIRE(!is_deprecated(ExternalModifier::RW));
    STATIC_REQUIRE(is_deprecated(ExternalModifier::RO_DEPRECATED));
    STATIC_REQUIRE(is_deprecated(ExternalModifier::RW_DEPRECATED));
    STATIC_REQUIRE(!is_deprecated(ExternalModifier::RO_PRIVATE));
    STATIC_REQUIRE(!is_deprecated(ExternalModifier::RW_PRIVATE));
    STATIC_REQUIRE(is_deprecated(ExternalModifier::UNKNOWN));

    STATIC_REQUIRE(!is_private(ExternalModifier::RO));
    STATIC_REQUIRE(!is_private(ExternalModifier::RW));
    STATIC_REQUIRE(!is_private(ExternalModifier::RO_DEPRECATED));
    STATIC_REQUIRE(!is_private(ExternalModifier::RW_DEPRECATED));
    STATIC_REQUIRE(is_private(ExternalModifier::RO_PRIVATE));
    STATIC_REQUIRE(is_private(ExternalModifier::RW_PRIVATE));
    STATIC_REQUIRE(is_private(ExternalModifier::UNKNOWN));
}

TEST_CASE("IoClassSerialiser basic typeName tests", "[IoClassSerialiser]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
    debug::resetStats();
    {
        // signed integer values
        REQUIRE(typeName<std::byte> == "int8_t");
        REQUIRE(typeName<std::byte const> == "int8_t const");
        REQUIRE(typeName<const std::byte> == "int8_t const");
        REQUIRE(typeName<int8_t> == "int8_t");
        REQUIRE(typeName<int8_t const> == "int8_t const");
        REQUIRE(typeName<char> == "byte");
        REQUIRE(typeName<char const> == "byte const");
        REQUIRE(typeName<int16_t> == "int16_t");
        REQUIRE(typeName<int16_t const> == "int16_t const");
        REQUIRE(typeName<int32_t> == "int32_t");
        REQUIRE(typeName<int32_t const> == "int32_t const");
        REQUIRE(typeName<int64_t> == "int64_t");
        REQUIRE(typeName<int64_t const> == "int64_t const");
        REQUIRE(typeName<long long> == "int128_t");
        REQUIRE(typeName<long long const> == "int128_t const");
        // unsigned integer values
        REQUIRE(typeName<uint8_t> == "uint8_t");
        REQUIRE(typeName<uint8_t const> == "uint8_t const");
        REQUIRE(typeName<unsigned char> == "uint8_t");
        REQUIRE(typeName<unsigned char const> == "uint8_t const");
        REQUIRE(typeName<uint16_t> == "uint16_t");
        REQUIRE(typeName<uint16_t const> == "uint16_t const");
        REQUIRE(typeName<uint32_t> == "uint32_t");
        REQUIRE(typeName<uint32_t const> == "uint32_t const");
        REQUIRE(typeName<uint64_t> == "uint64_t");
        REQUIRE(typeName<uint64_t const> == "uint64_t const");
        REQUIRE(typeName<unsigned long long> == "uint128_t");
        REQUIRE(typeName<unsigned long long const> == "uint128_t const");

        // floating point
        REQUIRE(typeName<float_t> == "float_t");
        REQUIRE(typeName<float_t const> == "float_t const");

        REQUIRE(typeName<std::string> == "string");
        REQUIRE(typeName<std::string_view> == "string");
        REQUIRE(typeName<std::string const> == "string const");
        REQUIRE(typeName<std::string_view const> == "string const");
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

struct SmartPointerClass {
    int                                          e0 = 0;
    length<metre>                                ea = 0_q_m;
    Annotated<int, si::electric_current<ampere>> e1 = 1;
    std::unique_ptr<int>                         e2 = std::make_unique<int>(2);
    std::unique_ptr<Annotated<int, NoUnit>>      e3 = std::make_unique<Annotated<int, NoUnit>>(3);
    std::shared_ptr<Annotated<int, NoUnit>>      e4 = std::make_shared<Annotated<int, NoUnit>>(4);
    std::shared_ptr<int>                         e5;
    std::unique_ptr<int>                         e6;
    std::unique_ptr<SmartPointerClass>           nested;

    template<opencmw::SmartPointerType A, opencmw::SmartPointerType B>
    [[nodiscard]] constexpr bool equalValues(const A &a, const B &b) const noexcept {
        if (a.get() == nullptr && b.get() == nullptr) return true;
        if (a.get() == nullptr && b.get() != nullptr) return false;
        if (a.get() != nullptr && b.get() == nullptr) return false;
        return (*a.get() == *b.get());
    }

    bool operator==(const SmartPointerClass &other) const {
        // compare by value only -- default comparator doesn't work here since they compare the references of smart pointer
        if (e0 != other.e0) return false;
        if (e1 != other.e1) return false;
        if (!equalValues(e2, other.e2)) return false;
        if (!equalValues(e3, other.e3)) return false;
        if (!equalValues(e4, other.e4)) return false;
        if (!equalValues(e5, other.e5)) return false;
        if (!equalValues(e6, other.e6)) return false;

        if (!nested && !other.nested) return true;
        if ((nested && !other.nested) || (!nested && other.nested)) {
            return false;
        }
        if (*nested != *other.nested) return false;
        return true;
    }
};
ENABLE_REFLECTION_FOR(SmartPointerClass, e0, e1, e2, e3, e4, e5, e6, nested)

TEST_CASE("IoClassSerialiser smart pointer", "[IoClassSerialiser]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
    debug::resetStats();
    {
        IoBuffer          buffer;
        SmartPointerClass data;
        REQUIRE(data == data);

        SmartPointerClass data2;
        // check for smart pointer function
        REQUIRE(!is_smart_pointer<decltype(data.e0)>);
        REQUIRE(!is_smart_pointer<decltype(data.e1)>);
        REQUIRE(is_smart_pointer<decltype(data.e2)>);
        REQUIRE(is_smart_pointer<decltype(data.e3)>);
        REQUIRE(is_smart_pointer<decltype(data.e4)>);
        REQUIRE(is_smart_pointer<decltype(data.e5)>);
        REQUIRE(is_smart_pointer<decltype(data.e6)>);

        // check for smart pointer concept

        std::cout << "object (short): " << ClassInfoShort << data << '\n';
        REQUIRE(data == data2);
        data.e0 = data.e0 + 10;
        data.ea = 10_q_m + data.ea;
        data.e1 += 10_q_A;
        //        data.e1        = 10_q_A + data.e1;
        data.e1 = 2 * data.e1;
        std::cout << "annotated field " << data.e1.getUnit() << std::endl;
        //        using TypeA = decltype(data.e1);
        //        using TypeB = decltype(10_q_A);
        //        using Rep = std::common_type_t<typename TypeA::rep, typename TypeB::rep>;
        //        std::cout << "type A" << typeName<TypeA> << std::endl;
        //        std::cout << "type B" << typeName<TypeB> << std::endl;
        //        std::cout << "Rep   " << typeName<Rep> << std::endl;
        //        std::cout << "quantity " << typeName<units::detail::common_quantity_impl<TypeA, TypeB, Rep>::type> << std::endl;
        // data.e1        = data.e1 + data.e1;
        *data.e2 = *data.e2 + 10;
        *data.e3 = *data.e3 + 10;
        *data.e4 = *data.e4 + 10;

        diffView(std::cout, data, data2);
        REQUIRE(data != data2);
        checkSerialiserIdentity<opencmw::YaS>(buffer, data, data2);
        REQUIRE(data == data2);

        data.e5 = std::make_shared<int>(42);
        data.e6 = std::make_unique<int>(42);
        REQUIRE(data != data2);
        checkSerialiserIdentity<opencmw::YaS>(buffer, data, data2);
        REQUIRE(data == data2);

        // test nested class identity
        data.nested = std::make_unique<SmartPointerClass>();
        REQUIRE(data != data2);
        data.nested->e0 = 10;
        diffView(std::cout, data, data2);

        REQUIRE(data != data2);
        checkSerialiserIdentity<opencmw::YaS>(buffer, data, data2);
        REQUIRE(data == data2);
        diffView(std::cout, data, data2);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}
struct TestData {
    opencmw::Annotated<double, si::speed<metre_per_second>, "custom description", RW, "groupA"> value;
    // bla blaa

    explicit TestData(double val = 0)
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
        std::cout << "type name: " << opencmw::typeName<int[2]> << '\n'; // NOLINT
        const int a[2] = { 1, 2 };
        std::cout << "type name: " << opencmw::typeName<decltype(a)> << '\n';
        std::cout << "type name: " << opencmw::typeName<short *> << '\n';
        std::cout << "type name: " << opencmw::typeName<const short *> << '\n';
        std::cout << "type name: " << opencmw::typeName<short *const> << '\n';

        opencmw::IoBuffer buffer;
        TestData          data;
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    opencmw::debug::resetStats();
}

TEST_CASE("IoSerialiser basic syntax", "[IoSerialiser]") {
    opencmw::debug::resetStats();
    {
        opencmw::debug::Timer timer("IoSerialiser basic syntax", 30);

        opencmw::IoBuffer     buffer;
        TestData              data(42);

        REQUIRE(opencmw::is_annotated<decltype(data.value)> == true);
        std::cout << fmt::format("buffer size (before): {} bytes\n", buffer.size());

        opencmw::FieldHeaderWriter<opencmw::YaS>::template put<true>(buffer, unmove(FieldDescriptionShort{ .fieldName = "fieldNameA" }), 43.0);
        opencmw::FieldHeaderWriter<opencmw::YaS>::template put<true>(buffer, unmove(FieldDescriptionShort{ .fieldName = "fieldNameB" }), data.value.value());
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
            opencmw::IoSerialiser<protocol, T>::serialise(buffer, FieldDescriptionShort{ .fieldName = std::string(opencmw::typeName<T>) + "TestDataClass" }, value);
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
            opencmw::IoSerialiser<protocol, T>::deserialise(buffer, FieldDescriptionShort{ .fieldName = std::string(opencmw::typeName<T>) + "TestDataClass" }, actual);
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

TEST_CASE("IoClassSerialiser protocol error tests", "[IoClassSerialiser]") {
    using namespace opencmw;
    IoBuffer buffer;
    Data     data;

    Data     data2;
    REQUIRE(data == data2);
    data.stringValue = "changed";
    REQUIRE(data != data2);

    opencmw::serialise<opencmw::YaS>(buffer, data);

    {
        buffer.reset();
        auto info = opencmw::deserialise<YaS, ProtocolCheck::LENIENT>(buffer, data2);
        REQUIRE(info.exceptions.empty());
    }

    buffer.clear();
    buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(R"({ "float1": 2.3, "test": { "intArray": [1, 2, 3], "val1":13.37e2, "val2":"bar"}, "int1": 42})"sv);
    std::cout << "Prepared json data: " << buffer.asString() << std::endl;
    {
        buffer.reset();
        auto info = opencmw::deserialise<YaS, ProtocolCheck::LENIENT>(buffer, data2);
        std::cout << " info: {}\n"
                  << info << std::endl;
        REQUIRE(info.exceptions.size() == 1); // N.B. invalid protocol
    }
}

struct SpecialData {
    std::unordered_map<std::string, std::string> map1;
    std::map<std::string, std::string>           map2;
    std::map<float, int>                         map3;

    bool                                         operator==(const SpecialData &) const = default;
};
ENABLE_REFLECTION_FOR(SpecialData, map1, map2, map3)

TEST_CASE("IoClassSerialiser map & Co.", "[IoClassSerialiser]") {
    using namespace opencmw;
    IoBuffer    buffer;
    SpecialData data;

    SpecialData data2;
    REQUIRE(data == data2);
    data.map1["newKey1"] = "value1";
    data.map2["newKey2"] = "value2";
    data.map3[0.123F]    = 10;
    REQUIRE(data != data2);

    opencmw::serialise<opencmw::YaS>(buffer, data);

    {
        buffer.reset();
        auto info = opencmw::deserialise<YaS, ProtocolCheck::LENIENT>(buffer, data2);
        std::cout << " info1: {}\n"
                  << info << std::endl;
        std::cout << " info2: {}\n"
                  << ClassInfoVerbose << info << std::endl;
        REQUIRE(info.exceptions.empty());
    }
    //    diffView(std::cout, data, data2);
    //    REQUIRE(data == data2);
}

struct NestedMapData {
    std::unordered_map<int, std::map<std::string, std::string>> map1;
    std::map<std::string, std::vector<int>>                     mapOfVector;
    bool                                                        operator==(const NestedMapData &) const = default;
};
ENABLE_REFLECTION_FOR(NestedMapData, map1, mapOfVector)

TEST_CASE("IoClassSerialiser nested maps", "[IoClassSerialiser]") {
    using namespace opencmw;
    IoBuffer      buffer;
    NestedMapData data;
    NestedMapData data2;
    REQUIRE(data == data2);
    data.map1.insert({ 1337, std::map<std::string, std::string>{} });
    data.map1[1337].insert({ "foo", "bar" });
    data.mapOfVector.insert({ "primes", { 1, 2, 3, 5, 7 } });
    REQUIRE(data != data2);

    opencmw::serialise<opencmw::YaS>(buffer, data);

    {
        buffer.reset();
        auto info = opencmw::deserialise<YaS, ProtocolCheck::LENIENT>(buffer, data2);
        std::cout << " info1: {}\n"
                  << info << std::endl;
        std::cout << " info2: {}\n"
                  << ClassInfoVerbose << info << std::endl;
        REQUIRE(info.exceptions.empty());
    }
}
#pragma clang diagnostic pop
