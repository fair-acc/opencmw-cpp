#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"

#include <Debug.hpp>
#include <IoClassSerialiser.hpp>
#include <IoSerialiser.hpp>
#include <Utils.hpp>
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
ENABLE_REFLECTION_FOR(NestedData, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, doubleArray, floatVector)

struct Data {
    int8_t                         byteValue        = 1;
    int16_t                        shortValue       = 2;
    int32_t                        intValue         = 3;
    int64_t                        longValue        = 4;
    float                          floatValue       = 5.0F;
    double                         doubleValue      = 6.0;
    std::string                    stringValue      = "bare string";
    std::string const              constStringValue = "unmodifiable string";
    std::array<double, 10>         doubleArray      = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    std::vector<float>             floatVector      = { 0.1f, 1.1f, 2.1f, 3.1f, 4.1f, 5.1f, 6.1f, 8.1f, 9.1f, 9.1f };
    opencmw::MultiArray<double, 2> doubleMatrix{ { 1, 3, 7, 4, 2, 3 }, { 2, 3 } };
    NestedData                     nestedData;
    Annotated<double, "Ohm">       annotatedValue = 0.1;

    Data()                                        = default;
    bool operator==(const Data &) const           = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(Data, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, constStringValue, doubleArray, floatVector, doubleMatrix, nestedData, annotatedValue)

template<opencmw::SerialiserProtocol protocol, opencmw::ReflectableClass T>
void checkSerialiserIdentity(opencmw::IoBuffer &buffer, const T &a, T &b) {
    buffer.reset();
    opencmw::serialise<protocol>(buffer, a);

    try {
        buffer.reset();
        opencmw::deserialise<opencmw::YaS>(buffer, b);
    } catch (std::exception &e) {
        std::cout << "caught exception " << opencmw::typeName<std::remove_reference_t<decltype(e)>>() << std::endl;
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
        data2.nestedData.byteValue  = '\0';
        data2.nestedData.floatValue = 12.0F;
        data2.nestedData.floatValue *= 2.0F;
        data2.nestedData.stringValue          = "different text";
        data2.nestedData.doubleArray.value[3] = 99;
        data2.doubleMatrix(0U, 0U)            = 42;
        data2.nestedData.floatVector.value.clear();
        REQUIRE(data != data2);

        opencmw::serialise<opencmw::YaS>(buffer, data);

        std::cout << "object (short): " << ClassInfoShort << data << '\n';
        std::cout << fmt::format("object (fmt): {}\n", data);
        std::cout << "object (long):  " << ClassInfoVerbose << data << '\n';

        // check (de-(serialisation identity
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

struct SmartPointerClass {
    int                                        e0 = 0;
    Annotated<int, "A">                        e1 = 1;
    std::unique_ptr<int>                       e2 = std::make_unique<int>(2);
    std::unique_ptr<Annotated<int, "unit e3">> e3 = std::make_unique<Annotated<int, "unit e3">>(3);
    std::shared_ptr<Annotated<int, "unit e3">> e4 = std::make_shared<Annotated<int, "unit e3">>(4);
    std::shared_ptr<int>                       e5;
    std::unique_ptr<int>                       e6;
    std::unique_ptr<SmartPointerClass>         nested;

    template<opencmw::SmartPointerType A, opencmw::SmartPointerType B>
    constexpr bool equalValues(const A &a, const B &b) const noexcept {
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
        if (*nested.get() != *other.nested.get()) return false;
        return true;
    }
};
ENABLE_REFLECTION_FOR(SmartPointerClass, e0, e1, e2, e3, e4, e5, e6, nested)

TEST_CASE("IoClassSerialiser smart pointer", "[IoClassSerialiser]") {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    using namespace opencmw;
    using namespace opencmw::utils; // for operator<< and fmt::format overloading
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
        data.e0        = data.e0 + 10;
        data.e1        = data.e1 + 10;
        *data.e2.get() = *data.e2.get() + 10;
        *data.e3.get() = *data.e3.get() + 10;
        *data.e4.get() = *data.e4.get() + 10;

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
        data.nested.get()->e0 = 10;
        std::cout << '\n';
        std::cout << "differ " << (data != data2) << "\n\n";
        diffView(std::cout, data, data2);
        std::cout << '\n';

        REQUIRE(data != data2);
        checkSerialiserIdentity<opencmw::YaS>(buffer, data, data2);
        REQUIRE(data == data2);
        diffView(std::cout, data, data2);
    }
    REQUIRE(opencmw::debug::dealloc == opencmw::debug::alloc); // a memory leak occurred
    debug::resetStats();
}

#pragma clang diagnostic pop
