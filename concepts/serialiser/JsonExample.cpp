#include <array>
#include <Formatter.hpp>
#include <iostream>
#include <memory>
#include <opencmw.hpp>

#include <IoSerialiserJson.hpp>

struct DataY {
    int8_t                        byteValue   = 1;
    int16_t                       shortValue  = 2;
    int32_t                       intValue    = 3;
    int64_t                       longValue   = 4;
    float                         floatValue  = 5.0F;
    double                        doubleValue = 6.0;
    std::string                   stringValue;
    std::array<double, 10>        doubleArray = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    std::map<std::string, double> doubleMap{ std::pair<std::string, double>{ "Hello", 4 }, std::pair<std::string, double>{ "Map", 1.3 } };
    std::vector<float>            floatVector = { 0.1F, 1.1F, 2.1F, 3.1F, 4.1F, 5.1F, 6.1F, 8.1F, 9.1F, 9.1F };
    std::shared_ptr<DataY>        nested;

    DataY()                              = default;
    bool operator==(const DataY &) const = default;
};
// following is the visitor-pattern-macro that allows the compile-time reflections via refl-cpp
ENABLE_REFLECTION_FOR(DataY, byteValue, shortValue, intValue, longValue, floatValue, doubleValue, stringValue, doubleArray, doubleMap, floatVector, nested)

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

    DataY bar;                              // new object to serialise into
    std::cout << opencmw::ClassInfoVerbose; // enables a more verbose tree-like class info output
    opencmw::diffView(std::cout, foo, bar); // foo and bar should be different here

    auto result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, bar);
    opencmw::diffView(std::cout, foo, bar); // foo and bar should be the same here

    std::print(std::cout, "deserialisation finished: {}\n", result);
    opencmw::IoBuffer buffer2;
    opencmw::serialise<opencmw::Json>(buffer2, foo);
    std::cout << "serialised (again): " << buffer2.asString() << std::endl;
}
