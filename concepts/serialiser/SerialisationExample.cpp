#include <YaSerialiser.hpp>
#include <Utils.hpp>
#include <iostream>

// SI units -- include what you need
#include <units/isq/si/electric_current.h>
#include <units/isq/si/energy.h>
#include <units/isq/si/thermodynamic_temperature.h>
using namespace units::isq::si; // for short-hand notation

struct className {
    int         field1;
    float       field2;
    std::string field3;

    // just good common practise to define some operators
    bool operator==(const className &) const = default;
};
ENABLE_REFLECTION_FOR(className, field1, field2, field3)

struct classNameMissing {
    int   field1;
    float field2;

    // just good common practise to define some operators
    bool operator==(const classNameMissing &) const = default;
};
ENABLE_REFLECTION_FOR(classNameMissing, field1, field2)

using opencmw::Annotated;
struct otherClass {
    Annotated<float, thermodynamic_temperature<kelvin>, "device specific temperature">        temperature     = 23.2F;
    Annotated<float, electric_current<ampere>, "this is the current from ...">                current         = 42.F;
    Annotated<float, energy<electronvolt>, "SIS18 energy at injection before being captured"> injectionEnergy = 8.44e6F;
    std::unique_ptr<classNameMissing>                                                         nested;
    // [..]

    // just good common practise to define some operators
    bool operator==(const otherClass &) const = default;
};
ENABLE_REFLECTION_FOR(otherClass, temperature, current, injectionEnergy, nested)

struct otherClassV2 {
    Annotated<float, thermodynamic_temperature<kelvin>, "device specific temperature">        temperature     = 23.2F;
    Annotated<float, electric_current<ampere>, "this is the current from ...">                current         = 42.F;
    Annotated<float, energy<electronvolt>, "SIS18 energy at injection before being captured"> injectionEnergy = 8.44e6F;
    Annotated<double, length<millimetre>, "beam position in x direction">                     beamPositionX   = 2.1;
    std::unique_ptr<className>                                                                nested;
    // [..]

    // just good common practise to define some operators
    bool operator==(const otherClassV2 &) const = default;
};

ENABLE_REFLECTION_FOR(otherClassV2, temperature, current, injectionEnergy, beamPositionX, nested)

using namespace std::string_literals;
using namespace opencmw;
using namespace opencmw::utils; // for operator<< and fmt::format overloading

/**
 * Serialisation example with nested classes and deserialisation into different type.
 */
int main() {
    // printout example for annotated class
    otherClass c{ 1.2f, 2.3f, 3.4f, std::make_unique<classNameMissing>(1, 0.42f) };
    c.injectionEnergy = 8.3e6;
    otherClassV2 d{ 1.3f, 2.4f, 3.5f, 3.0, nullptr };
    std::cout << "class info for annotated class: " << c << '\n';
    std::cout << "class info for annotated class2: " << d << '\n';

    // simple serialisation example:
    IoBuffer buffer;
    // serialise 'a' into the byte buffer
    opencmw::serialise<opencmw::YaS, true>(buffer, c);

    try {
        auto result = opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::LENIENT>(buffer, d);
        std::cout << "serialiser result: " << result << std::endl;
    } catch (ProtocolException &e) { // TODO: add protocol exception and field name/mismatch interface here
        std::cout << "caught: " << e << std::endl;
    } catch (...) {
        std::cout << "caught unknown exception " << std::endl;
    }
    std::cout << fmt::format("finished simple serialise-deserialise identity -- IoBbuffer required {} bytes\n", buffer.size());

    std::cout << "class info for annotated class after deserialisation: " << d << '\n';
}
