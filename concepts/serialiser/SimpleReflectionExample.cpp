#include <IoSerialiserYaS.hpp>
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

using opencmw::Annotated;
struct otherClass {
    Annotated<float, thermodynamic_temperature<kelvin>, "device specific temperature">        temperature     = 23.2F;
    Annotated<float, electric_current<ampere>, "this is the current from ...">                current         = 42.F;
    Annotated<float, energy<electronvolt>, "SIS18 energy at injection before being captured"> injectionEnergy = 8.44e6F;
    // [..]

    // just good common practise to define some operators
    bool operator==(const otherClass &) const = default;
};
ENABLE_REFLECTION_FOR(otherClass, temperature, current, injectionEnergy)

using namespace std::string_literals;
using namespace opencmw;
using namespace opencmw::utils; // for operator<< and fmt::format overloading

int main() {
    className a{ 1, 0.5F, "Hello World!" };
    className b{ 1, 0.501F, "Γειά σου Κόσμε!" };

    std::cout << fmt::format("class info a: {}\n", a);
    std::cout << ClassInfoVerbose << "class info b: " << b << '\n';
    diffView(std::cout, a, b);

    // printout example for annotated class
    otherClass c;
    std::cout << "class info for annotated class: " << c << '\n';

    // simple serialisation example:
    IoBuffer buffer;
    assert(a != b && "a & b should be unequal here"); // just checking
    // serialise 'a' into the byte buffer
    opencmw::serialise<opencmw::YaS>(buffer, a);

    // de-serialise the byte buffer into 'b'
    try {
        opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::LENIENT>(buffer, b);
    } catch (...) { // TODO: add protocol exception and field name/mismatch interface here
        std::cout << "caught unknown exception " << std::endl;
    }
    assert(a == b && "a & b should be equal here"); // just checking
    std::cout << fmt::format("finished simple serialise-deserialise identity -- IoBuffer required {} bytes\n", buffer.size());
    diffView(std::cout, a, b);
    // N.B. the buffer size is larger than the mere field sizes because of additional meta information that is required
    // for safely transmitting the data via the network and between different programming languages.
}
