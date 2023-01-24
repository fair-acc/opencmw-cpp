#pragma clang diagnostic push
#pragma ide diagnostic   ignored "LoopDoesntUseConditionVariableInspection"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#include <catch2/catch.hpp>

#include <iostream>
#include <string>

#include <MustacheSerialiser.hpp>

using namespace std::string_literals;
using namespace opencmw;

struct ServicesList {
    std::vector<std::string> services;
};
ENABLE_REFLECTION_FOR(ServicesList, services)

struct ServiceName {
    ServiceName(std::string s)
        : name(std::move(s)) {}
    std::string name;
};
ENABLE_REFLECTION_FOR(ServiceName, name)

struct ServiceNamesList {
    std::vector<std::string> services;
};
ENABLE_REFLECTION_FOR(ServiceNamesList, services)

struct AddressEntry {
    int                                                  id;
    Annotated<std::string, NoUnit, "Name of the person"> name;
    std::string                                          street;
    int                                                  streetNumber;
    std::string                                          postalCode;
    std::string                                          city;
    MultiArray<double, 2>                                multiArray;
    bool                                                 isCurrent;
};
ENABLE_REFLECTION_FOR(AddressEntry, name, street, streetNumber, postalCode, city, multiArray, isCurrent)
// ENABLE_REFLECTION_FOR(AddressEntry, name, street, streetNumber, postalCode, city, isCurrent)

TEST_CASE("MustacheSerialization: value with vector of strings", "[Mustache][MustacheValueSerialiser]") {
    std::stringstream str;
    ServicesList      servicesList;
    servicesList.services = { "a", "b", "c" };
    opencmw::mustache::serialise("Services", str,
            std::pair<std::string, const ServicesList &>{ "result"s, servicesList });

    REQUIRE(str.str() == "<html><li><span>a</span></li><li><span>b</span></li><li><span>c</span></li></html>\n");
}

TEST_CASE("MustacheSerialization: value with vector of objects", "[Mustache][MustacheNestedValueSerialiser]") {
    std::stringstream str;
    ServiceNamesList  servicesList;
    servicesList.services.emplace_back("a");
    servicesList.services.emplace_back("b");
    servicesList.services.emplace_back("c");
    opencmw::mustache::serialise("Services", str,
            std::pair<std::string, const ServiceNamesList &>{ "result"s, servicesList });

    REQUIRE(str.str() == "<html><li><span>a</span></li><li><span>b</span></li><li><span>c</span></li></html>\n");
}

TEST_CASE("MustacheSerialization: value with fallback serialisation", "[Mustache][MustacheFallbackValueSerialiser]") {
    {
        AddressEntry address;
        address.isCurrent    = false;
        address.streetNumber = 0;
        std::stringstream str;

        opencmw::mustache::serialise("Address", str,
                std::pair<std::string, const AddressEntry &>{ "result"s, address });

        REQUIRE(str.str() == R"""([name::][street::][streetNumber::0][postalCode::][city::][multiArray::{"dims": [0, 0],"values": []}][isCurrent::false]
)""");
    }
    {
        AddressEntry address{
            .id           = 0,
            .name         = "Holmes, Sherlock",
            .street       = "Baker Street",
            .streetNumber = 221, // 221b
            .postalCode   = "",
            .city         = "London",
            .multiArray   = { { 1.337, 23.42, 42.23, 13.37 }, { 2, 2 } },
            .isCurrent    = true
        };
        std::stringstream str;
        opencmw::mustache::serialise("Address", str,
                std::pair<std::string, const AddressEntry &>{ "result"s, address });

        REQUIRE(str.str() == R"""([name::Holmes, Sherlock][street::Baker Street][streetNumber::221][postalCode::][city::London][multiArray::{"dims": [2, 2],"values": [1.337e+00, 2.342e+01, 4.223e+01, 1.337e+01]}][isCurrent::true]
)""");
    }
}

struct MustacheDataWithSet {
    std::set<std::string> strings;
    std::set<float>       floats;
};
ENABLE_REFLECTION_FOR(MustacheDataWithSet, strings, floats)

TEST_CASE("MustacheSerialization: value with set of strings and set of floats", "[Mustache][MustacheValueSerialiser]") {
    std::stringstream   str;
    MustacheDataWithSet data{ .strings = { "Set", "of", "Strings" }, .floats = { 1.337f, 4.2f, 2.3f } };
    opencmw::mustache::serialise("", str, std::pair<std::string, const MustacheDataWithSet &>{ "result"s, data });

    REQUIRE(str.str() == R"""([strings::["Set", "Strings", "of"]][floats::[1.337e+00, 2.3e+00, 4.2e+00]]
)""");
}

#pragma clang diagnostic pop
