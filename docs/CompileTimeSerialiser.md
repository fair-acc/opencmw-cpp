# Use of Compile-Time Reflection in OpenCMW
OpenCWM (de-)serialiser tooling is based on the IoClassSerialiser, IoSerialiser, and IoBuffer class hierarchy as outlined in detail [here](IoSerialiser.md).  
These (de-)serialisers heavily rely upon [compile-time-reflection](docs/CompileTimeSerialiser.md) to efficiently transform domain-objects to and from the given wire-format (binary, JSON, ...).
Compile-time reflection will become part of [C++23](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0592r4.html) as described by [David Sankel et al. , “C++ Extensions for Reflection”, ISO/IEC CD TS 23619, N4856](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/n4856.pdf).
Until then, the compile-time reflection is emulated by the [refl-cpp](https://github.com/veselink1/refl-cpp) (header-only) library through a `constexpr` visitor-pattern and -- for the use in opencmw --
simplified/feature-reduced `ENABLE_REFLECTION_FOR(...)` macro:

```cpp
struct className {
    const std::string unique_name = "myCustomClassName";
    int         field1;
    // ...
    float       field2;
    std::string field3;
};
ENABLE_REFLECTION_FOR(className, field1, field2, field3) 
```
The visitor macro derives all the necessary information from the provides class name and field names and stores that information in `constexpr` structures. 
The advantage these `constexpr` structures is that the code is evaluated, largely optimised and, for example,  dead/unused code removed during compile-time 
which greatly boost the run-time performance. Still, since objects sent from clients to the service can (potentially) contain arbitrary field information 
and/or field definitions that are unknown to the service side, there are some remaining run-time consistency checks. We are striving for that potentially 
in-service communication could become fully resolved and optimised during compile-time to get the best possible performance. 
Field names that are not listed are implicitly considered `private` and not taken into account for (de-)serialisation or other debugging feature such as automatically  

The following types and (nested) class structures are supported:
Beside common primitive and STL container types, 
 * primitives `T`: `uint8_t` `int8_t`, `int16_t`, `int32_t`, `int64_t`, `float`, `double`, `std::string` and `std::string_view`
 * stl-container: `std::array<T,N>`, `std::vector<T,N>`, `std::list<T>`, `std::unsorted_map<T>`, `std::queue<T>`, `std::set<T>`
 * common opencmw container: `opencmw:MultiArray<T, N_DIM>`

N.B. regarding support/extensions for `unsigned` arithmetic types see [here](###-unsigned-data-types). Based on this reflection pattern, there are some
useful helper functions in the `opencmw::utils` namespace (requires `#include <Utils.hpp>`) that can be used for debugging, automatic 'fmt::format' and 'iostream' 
printouts, or introspection of classes or differences between classes of the same type. for example:
```cpp
className a{ 1, 0.5F, "Hello World!"};
className b{ 1, 0.501F, "Γειά σου Κόσμε!"};

std::cout << fmt::format("class info a: {}\n", a);
std::cout << ClassInfoVerbose << "class info b: " << b << '\n';
diffView(std::cout, a, b);
```

yielding the following console output (N.B. ANSI colouring of fields that differ):
<pre>
class info a: className(field1=1, field2=0.5, field3=Hello World!)
class info b: className(
  0: int                  className::field1             = 1
  1: float                className::field2             = 0.501
  2: string               className::field3             = Γειά σου Κόσμε!
 )
diffView: className(
  0: int                  className::field1             = 1
  1: float                className::field2             = <span style="color:red">0.5 vs. 0.501</span> 
  2: string               className::field3             = <span style="color:red">Hello World! vs. Γειά σου Κόσμε! </span>
 )
</pre>
Please see [SimpleReflectionExample.cpp](../concepts/serialiser/SimpleReflectionExample.cpp) for more details.

### User-specific types
It is possible to extend and to provide custom serialisation schemes for any other arbitrary type or struct, class or constructs thereof. For example, while the above struct `className` can already be automatically (de-)serialised, it is possible to provide a custom
`IoSerialiser` serialiser definition for it (N.B. here `Yas` is a stand-in for any other protocol):
```cpp
template<>
struct IoSerialiser<YaS, className> {
    static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<OTHER>(); }

    constexpr static bool    serialise(IoBuffer &buffer, const ClassField & /*field*/, const className & value) noexcept {
        buffer.put<uint8_t>(getDataTypeId());
        buffer.put<std::string>(MyCustomType::unique_name); // needed to identify class structure when de-serialising
        // add custom sequence of IoBuffer put statements, e.g.
        buffer.put<int>(value.field1)
        buffer.put<float>(value.field2)
        buffer.put<std::string>(value.field3)
        return std::is_constant_evaluated();
    }

    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, className &value) {
        const auto typeID = buffer.get<uint8_t>();
        if (getDataTypeId() != typeID) {
            // skip or throw an exception
        }
        const std::string unique_class_type_name = buffer.get<std::string>();
        if (unique_class_type_name != MyCustomType::unique_name) {
            // skip or throw an exception, feel free to add additional type checks
        }
        value.field1 = buffer.get<int>();
        value.field2 = buffer.get<float>();
        value.field3 = buffer.get<std::string>();
        return std::is_constant_evaluated();
    }
};
```
The `unique_name` field can be also omitted and replaced by refl-cpp's `refl::reflect(value).name` class name helper definition, requiring only a `ENABLE_REFLECTION_FOR(className)` definition.

### Type Annotations
provides also an optional light-weight `constexpr` annotation template wrapper `Annotated<type, unit, description> ...` that can be used to provide some extra meta-information (e.g. unit, descriptions, etc.)
that in turn can be used to (re-)generate and document the class definition (e.g. for other programming languages or projects that do not have the primary domain-object definition at hand)
or to generate a generic [OpenAPI](https://swagger.io/specification/) definition. For example:
```cpp
using opencmw::Annotated;
struct otherClass {
    Annotated<float, "°C", "device specific temperature">                        temperature     = 23.2F;
    Annotated<float, "A", "this is the current from ...">                        current         = 42.F;
    Annotated<float, "MeV/u", "SIS18 energy at injection before being captured"> injectionEnergy = 8.44F;
    // [..]

    // just good common practise to define some operators
    bool operator==(const otherClass &) const = default;
};
ENABLE_REFLECTION_FOR(otherClass, temperature, current, injectionEnergy)
// [..]
// printout example for annotated class
otherClass c;
std::cout << "class info for annotated class: " << c << '\n';
```
yielding the following console output:
<pre>
class info for annotated class: otherClass(
  0: float                otherClass::temperature       = 23.2   // [°C] - device specific temperature
  1: float                otherClass::current           = 42.0   // [A] - this is the current from ...
  2: float                otherClass::injectionEnergy   = 8.44   // [MeV/u] - SIS18 energy at injection before being captured
 )
</pre>
Please see [SimpleReflectionExample.cpp](../concepts/serialiser/SimpleReflectionExample.cpp) for more details.

### Unsigned Data Types
Except for `uint8_t`, the OpenCMW (de-)serialiser supports only signed arithmetic types by default but can easily be extended for `unsigned` data types. 
The rational behind not enabling all arithmetic types is since we aim at compatibility with other non-C++ languages, for example Java, that natively often 
do not support `unsigned` data types well enough. However, if you want to enable all -- and also  `unsigned` -- types (because you work only with C/C++), 
you may define the following before loading the first OpenCMW header file:
```cpp
#define OPENCMW_ENABLE_UNSIGNED_SUPPORT 1
#include <opencmw.hpp>
// ...
```

### more to come ...