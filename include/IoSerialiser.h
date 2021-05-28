#ifndef OPENCMW_IOSERIALISER_H
#define OPENCMW_IOSERIALISER_H

#include <IoBuffer.h>
#include <MultiArray.hpp>
#include <list>
#include <queue>

#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-c-arrays"

namespace opencmw {

/* unit-/description-type annotation variant #2 C++20 compatible (to note: cool non-type template arguments and of course the space-ship <=> operator */
template<typename T, const StringLiteral unit = "", const StringLiteral description = "", const StringLiteral direction = "", const StringLiteral... groups>
struct Annotated {
    // clang-format off
    using String = std::string_view;
    T value;
    //constexpr Annotated() = delete;
    constexpr Annotated() = default;
    constexpr Annotated(const T &initValue) noexcept : value(initValue) {}
    constexpr Annotated(T &&t) : value(std::move(t)) {}
    constexpr ~Annotated()       = default;
    constexpr Annotated &operator=(const T &newValue) { value = newValue; return *this; }
    //    constexpr Annotated &operator=(const Annotated &other) {
    //        if (this == &other) {
    //            return *this;
    //        }
    //        value = other.value;
    //        return *this;
    //    }
    constexpr void                 isAnnotated() const noexcept {}
    [[nodiscard]] constexpr String getUnit() const noexcept { return String(unit.value); }
    [[nodiscard]] constexpr String getDescription() const noexcept { return String(description.value); }
    [[nodiscard]] constexpr String getDirection() const noexcept { return String(direction.value, direction.size); }
    [[nodiscard]] constexpr String typeName() const noexcept { return opencmw::typeName<T>(); }
    constexpr                      operator T &() { return value; }
    auto                           operator<=>(const Annotated &) const noexcept = default;
    //constexpr auto                      operator<=>(const Annotated &) const noexcept = default;

    template<typename T2, const StringLiteral ounit = "", const StringLiteral odescription = "", const StringLiteral odirection = "", const StringLiteral... ogroups>
    constexpr bool operator==(const T2 &rhs) const noexcept {
        if (value != rhs.value) return false;
        return getUnit() == rhs.getUnit();
    }

    T &            operator()() noexcept { return value; }
    constexpr void operator+=(const T &a) noexcept { value += a; }
    constexpr void operator-=(const T &a) noexcept { value -= a; }
    constexpr void operator*=(const T &a) noexcept { value *= a; }
    constexpr void operator/=(const T &a) { value /= a; }
    constexpr void operator*=(const Annotated &a) noexcept {
        value *= a.value; // N.B. actually also changes 'unit' -- implement? Nice semantic but performance....?
    }
    Annotated operator+(const Annotated &rhs) { return this->value += rhs.value; } //TODO: complete along the line of the units-library
    Annotated operator-(const Annotated &rhs) { return this->value -= rhs.value; }
    Annotated operator*(const Annotated &rhs) { return this->value *= rhs.value; }
    Annotated operator/(const Annotated &rhs) { return this->value /= rhs.value; }
    template<typename O> Annotated operator+(const O &rhs) { return this->value += rhs.value; }
    template<typename O> Annotated operator-(const O &rhs) { return this->value -= rhs.value; }
    template<typename O> Annotated operator*(const O &rhs) { return this->value *= rhs.value; }
    template<typename O> Annotated operator/(const O &rhs) { return this->value /= rhs.value; }

    //friend constexpr std::ostream &operator<<(std::ostream &os, const Annotated &m) { return os << m.value; }
    // clang-format on
};

template<typename>
struct isAnnotated : public std::false_type {};

template<typename T, const StringLiteral unit, const StringLiteral description, const StringLiteral direction>
struct isAnnotated<Annotated<T, unit, description, direction>> : public std::true_type {};

template<class T>
concept AnnotatedType = isAnnotated<T>::value;

template<typename T>
constexpr bool isAnnotatedMember(T const &) { return isAnnotated<T>::value; }

template<typename T>
constexpr T getAnnotatedMember(const T &annotatedValue) {
    return annotatedValue;
}

template<typename T, const StringLiteral unit, const StringLiteral description, const StringLiteral direction>
constexpr T getAnnotatedMember(const Annotated<T, unit, description, direction> &annotatedValue) {
    return annotatedValue.value;
}

template<StringLiteral protocol>
struct Protocol {
    constexpr static const char *protocolName() {
        return protocol.value;
    }
};

template<typename T>
concept SerialiserProtocol = requires { T::protocolName(); }; // TODO: find neater check via is_protocol

//template<typename T>
//static constexpr uint8_t getDataTypeId() { return 0xFF; } // default value

using ClassField = std::string_view; // as a place-holder for the reflected type info

/// generic protocol definition -> should throw exception when used in production code
template<typename T, SerialiserProtocol protocol>
struct IoSerialiser {
    constexpr static uint8_t getDataTypeId() { return 0xFF; } // default value

    constexpr static bool    serialise(IoBuffer & /*buffer*/, const ClassField &field, const T &value) noexcept {
        std::cout << fmt::format("{:<4} - serialise-generic: {} {} value: {} - constexpr?: {}\n",
                protocol::protocolName(), typeName<T>(), field, value,
                std::is_constant_evaluated());
        return std::is_constant_evaluated();
    }

    constexpr static bool deserialise(IoBuffer & /*buffer*/, const ClassField &field, const T &value) noexcept {
        std::cout << fmt::format("{:<4} - deserialise-generic: {} {} value: {} - constexpr?: {}\n",
                protocol::protocolName(), typeName<T>(), field, value,
                std::is_constant_evaluated());
        return std::is_constant_evaluated();
    }
};

template<SerialiserProtocol protocol, typename T>
bool serialisePartial(IoBuffer &buffer, const T & /*obj*/) noexcept {
    bool state = false;
    if (std::is_constant_evaluated() || true) {
        state |= IoSerialiser<int, protocol>::serialise(buffer, "intField", 2);
        state |= IoSerialiser<double, protocol>::serialise(buffer, "doubleField", 2.3);
        state |= IoSerialiser<short, protocol>::serialise(buffer, "shortField", 3);
        state |= IoSerialiser<std::string_view, protocol>::serialise(buffer, "stringField", "Hello World!");
    }
    return state;
}

} // namespace opencmw

/* #################################################################################### */
/* ### Yet-Another-Serialiser - YaS Definitions ####################################### */
/* #################################################################################### */
struct START_MARKER {};
struct END_MARKER {};
struct OTHER {};

constexpr static START_MARKER START_MARKER_INST;
constexpr static END_MARKER   END_MARKER_INST;

namespace opencmw {
struct YaS : Protocol<"YaS"> {
};

namespace yas {
static const int              VERSION_MAGIC_NUMBER = -1;    // '-1' since CmwLight cannot have a negative number of entries
static const std::string_view PROTOCOL_NAME        = "YaS"; // Yet another Serialiser implementation
static const uint8_t          VERSION_MAJOR        = 1;
static const uint8_t          VERSION_MINOR        = 0;
static const uint8_t          VERSION_MICRO        = 0;
static const uint8_t          ARRAY_TYPE_OFFSET = 100U;

// clang-format off
template<typename T> static constexpr uint8_t getDataTypeId() { return 0xFF; } // default value
template<> constexpr uint8_t getDataTypeId<START_MARKER>() { return 0; }
template<> constexpr uint8_t getDataTypeId<bool>() { return 1; }
template<> constexpr uint8_t getDataTypeId<int8_t>() { return 2; }
template<> constexpr uint8_t getDataTypeId<int16_t>() { return 3; }
template<> constexpr uint8_t getDataTypeId<int32_t>() { return 4; }
template<> constexpr uint8_t getDataTypeId<int64_t>() { return 5; }
template<> constexpr uint8_t getDataTypeId<float>() { return 6; }
template<> constexpr uint8_t getDataTypeId<double>() { return 7; }
template<> constexpr uint8_t getDataTypeId<char>() { return 8; }
template<> constexpr uint8_t getDataTypeId<std::string>() { return 9; }

template<> constexpr uint8_t getDataTypeId<bool[]>() { return 101; }
template<> constexpr uint8_t getDataTypeId<std::vector<bool>>() { return 101; }
template<> constexpr uint8_t getDataTypeId<int8_t[]>() { return 102; }
template<> constexpr uint8_t getDataTypeId<std::vector<int8_t>>() { return 102; }
template<> constexpr uint8_t getDataTypeId<int16_t[]>() { return 103; }
template<> constexpr uint8_t getDataTypeId<std::vector<int16_t>>() { return 103; }
template<> constexpr uint8_t getDataTypeId<int32_t[]>() { return 104; }
template<> constexpr uint8_t getDataTypeId<std::vector<int32_t>>() { return 104; }
template<> constexpr uint8_t getDataTypeId<int64_t[]>() { return 105; }
template<> constexpr uint8_t getDataTypeId<std::vector<int64_t>>() { return 105; }
template<> constexpr uint8_t getDataTypeId<float[]>() { return 106; }
template<> constexpr uint8_t getDataTypeId<std::vector<float>>() { return 106; }
template<> constexpr uint8_t getDataTypeId<double[]>() { return 107; }
template<> constexpr uint8_t getDataTypeId<std::vector<double>>() { return 107; }
template<> constexpr uint8_t getDataTypeId<char[]>() { return 108; }
template<> constexpr uint8_t getDataTypeId<std::vector<char>>() { return 108; }

template<> constexpr uint8_t getDataTypeId<std::string[]>() { return 109; }
template<> constexpr uint8_t getDataTypeId<std::string_view[]>() { return 109; }

// template<> constexpr uint8_t getDataTypeId<START_MARKER>() { return 200; }
// template<> constexpr uint8_t getDataTypeId<enum>()          { return 201; }
// template<typename T> constexpr uint8_t getDataTypeId<std::list<T>>()     { return 202; }
// template<> constexpr uint8_t getDataTypeId<std::unsorted_map>() { return 203; }
// template<> constexpr uint8_t getDataTypeId<std::queue>()    { return 204; }
// template<> constexpr uint8_t getDataTypeId<std::set>()      { return 205; }

template<>
constexpr uint8_t getDataTypeId<OTHER>() { return 0xFD; }

template<>
constexpr uint8_t getDataTypeId<END_MARKER>() { return 0xFE; }

// clang-format on
} // namespace yas

template<typename T>
struct IoSerialiser<T, YaS> {
    static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); } // default value
};

template<Number T> // catches all numbers
struct IoSerialiser<T, YaS> {
    static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); }
    constexpr static bool    serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        buffer.put(getAnnotatedMember(value));
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) noexcept {
        value = buffer.get<T>();
        return std::is_constant_evaluated();
    }
};

template<StringLike T>
struct IoSerialiser<T, YaS> {
    static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); }
    constexpr static bool    serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        //        std::cout << fmt::format("{} - serialise-String_like: {} {} == {} - constexpr?: {}\n",
        //            YaS::protocolName(), typeName<T>(), field, value, std::is_constant_evaluated());
        buffer.put<T>(getAnnotatedMember(value)); // N.B. ensure that the wrapped value and not the annotation itself is serialised
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) noexcept {
        value = buffer.get<std::string>();
        //        std::cout << fmt::format("{} - de-serialise-String_like: {} {} == {}  {} - constexpr?: {}\n",
        //            YaS::protocolName(), typeName<T>(), field, value, value.size(), std::is_constant_evaluated());
        return std::is_constant_evaluated();
    }
};

template<ArrayOrVector T>
struct IoSerialiser<T, YaS> {
    static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); }
    constexpr static bool    serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        buffer.put(std::array<int32_t,1>{static_cast<int32_t>(value.size())});
        buffer.put(value);
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) noexcept {
        buffer.getArray<std::array<int32_t, 1>>(); // todo: verify dimensions
        buffer.getArray(value);
        return std::is_constant_evaluated();
    }
};

template <MultiArrayType T>
struct IoSerialiser<T, YaS> {
    static constexpr uint8_t getDataTypeId() {
        // std::cout << fmt::format("getDataTypeID<{}>() = {}\n", typeName<typename T::value_type>(), yas::getDataTypeId<typename T::value_type>());
        return yas::ARRAY_TYPE_OFFSET + yas::getDataTypeId<typename T::value_type>(); }
    constexpr static bool    serialise(IoBuffer &buffer, const ClassField & field, const T &value) noexcept {
        std::cout << fmt::format("{} - serialise-MultiArray: {} {} == {} - constexpr?: {}, typeid = {}\n", YaS::protocolName(), typeName<T>(), field, value, std::is_constant_evaluated(), getDataTypeId());
        buffer.put(value.dimensions());
        buffer.put(value.elements()); // todo: account for strides and offsets (possibly use iterators?)
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) noexcept {
        value.dimensions() = buffer.getArray<typename T::size_t_, T::n_dims_>(); // todo: verify dimensions, use template for dimension
        value.element_count() = 1;
        value.stride(T::n_dims_ - 1) = 1;
        value.offset(T::n_dims_ - 1) = 0;
        for (auto i = T::n_dims_ - 1; i > 0; i--) {
            value.element_count() *= value.dimensions()[i - 1];
            value.stride(i - 1) = value.stride(i) * value.dimensions()[i];
            value.offset(i - 1) = 0;
        }
        buffer.getArray(value.elements());
        return std::is_constant_evaluated();
    }
};


template<>
struct IoSerialiser<START_MARKER, YaS> {
    static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<START_MARKER>(); }

    constexpr static bool    serialise(IoBuffer &buffer, const ClassField & /*field*/, const START_MARKER & /*value*/) noexcept {
        buffer.put<uint8_t>(getDataTypeId());
        return std::is_constant_evaluated();
    }

    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, const START_MARKER &) {
        const auto markerByte = buffer.get<uint8_t>();
        if (getDataTypeId() != markerByte) {
            //TODO: convert to appropriate protocol exception
            std::cerr << fmt::format("START_MARKER ** error ** reached start of struct pos {} vs size {} - marker value {} (should) vs. {} (is)\n",
                    buffer.position(), buffer.size(), getDataTypeId(), markerByte);
        }
        return std::is_constant_evaluated();
    }
};

template<>
struct IoSerialiser<END_MARKER, YaS> {
    static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<END_MARKER>(); }
    static bool              serialise(IoBuffer &buffer, const ClassField & /*field*/, const END_MARKER & /*value*/) noexcept {
        buffer.put<uint8_t>(getDataTypeId()); // N.B. ensure that the wrapped value and not the annotation itself is serialised
        return std::is_constant_evaluated();
    }

    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, const END_MARKER &) {
        const auto markerByte = buffer.get<uint8_t>();
        if (getDataTypeId() != markerByte) { //TODO: convert to appropriate protocol exception
            std::cerr << fmt::format("END_MARKER ** error ** reached end of struct pos {} vs size {} - marker value {} (should) vs. {} (is)\n",
                    buffer.position(), buffer.size(), getDataTypeId(), markerByte);
        }
        return std::is_constant_evaluated();
    }
};

template<SerialiserProtocol protocol, typename DataType>
std::size_t putFieldHeader(IoBuffer &buffer, const std::string_view &fieldName, const DataType &data, const bool writeMetaInfo = false) {
    using StrippedDataType         = decltype(getAnnotatedMember(data));
    constexpr int32_t dataTypeSize = static_cast<int32_t>(sizeof(StrippedDataType));
    buffer.ensure(((fieldName.length() + 18) * sizeof(uint8_t)) + dataTypeSize);

    // -- offset 0 vs. field start
    const std::size_t headerStart = buffer.size();
    buffer.put(static_cast<uint8_t>(IoSerialiser<StrippedDataType, protocol>::getDataTypeId())); // data type ID
    buffer.put(static_cast<int32_t>(std::hash<std::string_view>{}(fieldName)));                  // unique hashCode identifier -- TODO: unify across C++/Java & optimise performance
    const std::size_t dataStartOffsetPosition = buffer.size();
    buffer.put(-1); // dataStart offset
    const int32_t     dataSize         = is_supported_number<DataType>::value ? dataTypeSize : -1;
    const std::size_t dataSizePosition = buffer.size();
    buffer.put(dataSize);  // dataSize (N.B. 'headerStart' + 'dataStart + dataSize' == start of next field header
    buffer.put(fieldName); // full field name

    if constexpr (requires { data.isAnnotated(); data.getUnit(); }) {
        if (writeMetaInfo) {
            buffer.put(std::string_view(data.getUnit()));
            buffer.put(std::string_view(data.getDescription()));
            buffer.put(std::string_view(data.getDirection()));
            // TODO: write group meta data
            //final String[] groups = fieldDescription.getFieldGroups().toArray(new String[0]);
            //buffer.putStringArray(groups, groups.length);
            //buffer.put<std::string[]>({""});
        }
    }

    const std::size_t dataStartPosition         = buffer.size();
    const std::size_t dataStartOffset           = (dataStartPosition - headerStart);     // -- offset dataStart calculations
    buffer.at<int32_t>(dataStartOffsetPosition) = static_cast<int32_t>(dataStartOffset); // write offset to dataStart

    // from hereon there are data specific structures that are written to the IoBuffer
    IoSerialiser<decltype(getAnnotatedMember(data)), protocol>::serialise(buffer, fieldName, getAnnotatedMember(data));

    // add just data-end position
    buffer.at<int32_t>(dataSizePosition) = static_cast<int32_t>(buffer.size() - dataStartPosition); // write data size

    return dataSizePosition; // N.B. exported for adjusting START_MARKER -> END_MARKER data size to be adjustable and thus skippable
}

} // namespace opencmw

/* #################################################################################### */
/* ### CmwLight Definitions ########################################################### */
/* #################################################################################### */

namespace opencmw {
struct CmwLight : Protocol<"CmwLight"> {
};

namespace cmwlight {

}

template<Number T>
struct IoSerialiser<T, CmwLight> { // catch all template
    constexpr static bool serialise(IoBuffer &buffer, const ClassField &field, const T &value) noexcept {
        if (std::is_constant_evaluated()) {
            using namespace std::literals;
            buffer.put(field);
            buffer.template put(value);
            return true;
        }
        std::cout << fmt::format("{} - serialise-catch-all: {} {} value: {} - constexpr?: {}\n",
                CmwLight::protocolName(), typeName<T>(), field, value,
                std::is_constant_evaluated());
        return false;
    }
};
} // namespace opencmw

#pragma clang diagnostic pop
#endif //OPENCMW_IOSERIALISER_H
