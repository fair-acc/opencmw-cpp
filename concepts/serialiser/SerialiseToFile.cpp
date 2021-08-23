#include <IoClassSerialiser.hpp>
#include <Utils.hpp>
#include <iostream>
#include <fstream>

// SI units -- include what you need
#include <units/isq/si/electric_current.h>
#include <units/isq/si/energy.h>
#include <units/isq/si/thermodynamic_temperature.h>
using namespace units::isq::si; // for short-hand notation

using opencmw::Annotated;
struct otherClass {
    Annotated<float, thermodynamic_temperature<kelvin>, "device specific temperature">        temperature     = 23.2F;
    Annotated<float, electric_current<ampere>, "this is the current from ...">                current         = 42.F;
    Annotated<float, energy<electronvolt>, "SIS18 energy at injection before being captured"> injectionEnergy = 8.44e6F;
    std::string name = "TestStruct";
    std::unique_ptr<otherClass> nested;
    // [..]

    // just good common practise to define some operators
    bool operator==(const otherClass &) const = default;
};
ENABLE_REFLECTION_FOR(otherClass, temperature, current, injectionEnergy, name, nested)

using namespace std::string_literals;
using namespace opencmw;
using namespace opencmw::utils; // for operator<< and fmt::format overloading

/**
 * Serialisation example with nested classes and deserialisation into different type.
 */
int main() {
    // printout example for annotated class
    otherClass c{1.2f,2.3f,3.4f,"fubar",std::make_unique<otherClass>()};
    c.injectionEnergy = 8.3e6;
    std::cout << "class info for annotated class: " << c << std::endl;
    otherClass d{};

    IoBuffer buffer;
    try {
        opencmw::serialise<opencmw::YaS, true>(buffer, c);
        std::ofstream outfile("out.bin", std::ios::binary);
        outfile.write(reinterpret_cast<char*>(buffer.data()), static_cast<long int>(buffer.size()));
        outfile.close();
        auto result = opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::LENIENT>(buffer, d);
        std::cout << "serialiser result: " << result << std::endl;
    } catch (ProtocolException &e) { // TODO: add protocol exception and field name/mismatch interface here
        std::cout << "caught: " << e << std::endl;
    } catch (...) {
        std::cout << "caught unknown exception " << std::endl;
    }
    std::cout << fmt::format("finished simple serialise-deserialise identity -- IoBbuffer required {} bytes\n", buffer.size());
}
