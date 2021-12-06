#include <IoSerialiserJson.hpp>
#include <Utils.hpp>
#include <array>
#include <iostream>
#include <memory>

struct DataY {
    int8_t                 byteValue   = 1;
    int16_t                shortValue  = 2;
    int32_t                intValue    = 3;
    int64_t                longValue   = 4;
    float                  floatValue  = 5.0F;
    double                 doubleValue = 6.0;
    std::string            stringValue;
    std::array<double, 10> doubleArray = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    // std::vector<float>             floatVector      = { 0.1F, 1.1F, 2.1F, 3.1F, 4.1F, 5.1F, 6.1F, 8.1F, 9.1F, 9.1F }; // causes SEGFAULT
    // opencmw::MultiArray<double, 2> doubleMatrix{ { 1, 3, 7, 4, 2, 3 }, { 2, 3 } };
    std::shared_ptr<DataY> nested;

    DataY()                              = default;
    bool operator==(const DataY &) const = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(DataY, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, doubleArray, /*floatVector,*/ nested)

/**
 * Serialisation example with nested classes and deserialisation into different type.
 */
int main() {
    opencmw::IoBuffer buffer;
    DataY             foo;
    foo.doubleValue               = 42.23;
    foo.stringValue               = "test";
    foo.nested                    = std::make_shared<DataY>();
    foo.nested.get()->stringValue = "asdf";
    opencmw::serialise<opencmw::Json>(buffer, foo);
    std::cout << "serialised: " << buffer.asString() << std::endl;
    DataY bar;
    auto  result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, bar);
    //opencmw::utils::diffView(std::cout, foo, bar); // todo: produces SEGFAULT
    fmt::print(std::cout, "deserialisation finished: {}\n", result);
    opencmw::IoBuffer buffer2;
    opencmw::serialise<opencmw::Json>(buffer2, foo);
    std::cout << "serialised (again): " << buffer2.asString() << std::endl;
}
