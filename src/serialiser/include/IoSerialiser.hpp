#ifndef OPENCMW_IOSERIALISER_H
#define OPENCMW_IOSERIALISER_H

#include "IoBuffer.hpp"
#include "MultiArray.hpp"
#include <list>
#include <map>
#include <queue>

#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-c-arrays"

namespace opencmw {

class ProtocolException {
    const std::string errorMsg;

public:
    explicit ProtocolException(std::string errorMessage) noexcept
        : errorMsg(std::move(errorMessage)) {}
    explicit ProtocolException(const char *errorMessage) noexcept
        : errorMsg(errorMessage) {}

    [[nodiscard]] std::string_view what() const noexcept { return errorMsg; }
};

inline std::ostream &operator<<(std::ostream &os, const ProtocolException &exception) {
    return os << "ProtocolException(\"" << exception.what() << "\")";
}

struct DeserialiserInfo {
    std::map<std::string, std::vector<bool>> setFields;
    std::list<std::tuple<std::string, int>>  additionalFields;
    std::list<ProtocolException>             exceptions;
};

template<basic_fixed_string protocol>
struct Protocol {
    constexpr static const char *protocolName() { return protocol.data_; }
};

template<typename T>
concept SerialiserProtocol = requires { T::protocolName(); }; // TODO: find neater check via is_protocol

using ClassField           = std::string_view; // as a place-holder for the reflected type info

/// generic protocol definition -> should throw exception when used in production code
template<SerialiserProtocol protocol, typename T>
struct IoSerialiser {
    constexpr static uint8_t getDataTypeId() { return 0xFF; } // default value

    constexpr static bool    serialise(IoBuffer & /*buffer*/, const ClassField &field, const T &value) noexcept {
        std::cout << fmt::format("{:<4} - serialise-generic: {} {} value: {} - constexpr?: {}\n",
                protocol::protocolName(), typeName<T>, field, value,
                std::is_constant_evaluated());
        return std::is_constant_evaluated();
    }

    constexpr static bool deserialise(IoBuffer & /*buffer*/, const ClassField &field, T &value) noexcept {
        std::cout << fmt::format("{:<4} - deserialise-generic: {} {} value: {} - constexpr?: {}\n",
                protocol::protocolName(), typeName<T>, field, value,
                std::is_constant_evaluated());
        return std::is_constant_evaluated();
    }
};

template<SerialiserProtocol protocol, typename T>
bool serialisePartial(IoBuffer &buffer, const T & /*obj*/) noexcept {
    bool state = false;
    // TODO: The check below is always true
    if (std::is_constant_evaluated() || true) {
        state |= IoSerialiser<protocol, int>::serialise(buffer, "intField", 2);
        state |= IoSerialiser<protocol, double>::serialise(buffer, "doubleField", 2.3);
        state |= IoSerialiser<protocol, short>::serialise(buffer, "shortField", 3);
        state |= IoSerialiser<protocol, std::string_view>::serialise(buffer, "stringField", "Hello World!");
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
struct YaS : Protocol<"YaS"> {};

namespace yas {
static const int              VERSION_MAGIC_NUMBER = -1;    // '-1' since CmwLight cannot have a negative number of entries
static const std::string_view PROTOCOL_NAME        = "YaS"; // Yet another Serialiser implementation
static const uint8_t          VERSION_MAJOR        = 1;
static const uint8_t          VERSION_MINOR        = 0;
static const uint8_t          VERSION_MICRO        = 0;
static const uint8_t          ARRAY_TYPE_OFFSET    = 100U;

// clang-format off
template<typename T> inline constexpr uint8_t getDataTypeId() { return 0xFF; } // default value
template<> inline constexpr uint8_t getDataTypeId<START_MARKER>() { return 0; }
template<> inline constexpr uint8_t getDataTypeId<bool>() { return 1; }
template<> inline constexpr uint8_t getDataTypeId<int8_t>() { return 2; }
template<> inline constexpr uint8_t getDataTypeId<int16_t>() { return 3; }
template<> inline constexpr uint8_t getDataTypeId<int32_t>() { return 4; }
template<> inline constexpr uint8_t getDataTypeId<int64_t>() { return 5; }
template<> inline constexpr uint8_t getDataTypeId<float>() { return 6; }
template<> inline constexpr uint8_t getDataTypeId<double>() { return 7; }
template<> inline constexpr uint8_t getDataTypeId<char>() { return 8; }
template<> inline constexpr uint8_t getDataTypeId<std::string>() { return 9; }

template<> inline constexpr uint8_t getDataTypeId<bool[]>() { return 101; }
template<> inline constexpr uint8_t getDataTypeId<std::vector<bool>>() { return 101; }
template<> inline constexpr uint8_t getDataTypeId<int8_t[]>() { return 102; }
template<> inline constexpr uint8_t getDataTypeId<std::vector<int8_t>>() { return 102; }
template<> inline constexpr uint8_t getDataTypeId<int16_t[]>() { return 103; }
template<> inline constexpr uint8_t getDataTypeId<std::vector<int16_t>>() { return 103; }
template<> inline constexpr uint8_t getDataTypeId<int32_t[]>() { return 104; }
template<> inline constexpr uint8_t getDataTypeId<std::vector<int32_t>>() { return 104; }
template<> inline constexpr uint8_t getDataTypeId<int64_t[]>() { return 105; }
template<> inline constexpr uint8_t getDataTypeId<std::vector<int64_t>>() { return 105; }
template<> inline constexpr uint8_t getDataTypeId<float[]>() { return 106; }
template<> inline constexpr uint8_t getDataTypeId<std::vector<float>>() { return 106; }
template<> inline constexpr uint8_t getDataTypeId<double[]>() { return 107; }
template<> inline constexpr uint8_t getDataTypeId<std::vector<double>>() { return 107; }
template<> inline constexpr uint8_t getDataTypeId<char[]>() { return 108; }
template<> inline constexpr uint8_t getDataTypeId<std::vector<char>>() { return 108; }

template<> inline constexpr uint8_t getDataTypeId<std::string[]>() { return 109; }
template<> inline constexpr uint8_t getDataTypeId<std::string_view[]>() { return 109; }

// template<> inline constexpr uint8_t getDataTypeId<START_MARKER>() { return 200; }
// template<> inline constexpr uint8_t getDataTypeId<enum>()          { return 201; }
// template<typename T> inline constexpr uint8_t getDataTypeId<std::list<T>>()     { return 202; }
// template<> inline constexpr uint8_t getDataTypeId<std::unsorted_map>() { return 203; }
// template<> inline constexpr uint8_t getDataTypeId<std::queue>()    { return 204; }
// template<> inline constexpr uint8_t getDataTypeId<std::set>()      { return 205; }

template<>
inline constexpr uint8_t getDataTypeId<OTHER>() { return 0xFD; }

template<>
inline constexpr uint8_t getDataTypeId<END_MARKER>() { return 0xFE; }

// clang-format on
} // namespace yas

template<typename T>
struct IoSerialiser<YaS, T> {
    inline static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); } // default value
};

template<Number T> // catches all numbers
struct IoSerialiser<YaS, T> {
    inline static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        buffer.put(value);
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) noexcept {
        value = buffer.get<T>();
        return std::is_constant_evaluated();
    }
};

template<StringLike T>
struct IoSerialiser<YaS, T> {
    inline static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        //        std::cout << fmt::format("{} - serialise-String_like: {} {} == {} - constexpr?: {} - const {}\n",
        //                YaS::protocolName(), typeName<T>(), field, value, std::is_constant_evaluated(), std::is_const_v<T>);
        buffer.put<T>(value); // N.B. ensure that the wrapped value and not the annotation itself is serialised
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
struct IoSerialiser<YaS, T> {
    inline static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        buffer.put(std::array<int32_t, 1>{ static_cast<int32_t>(value.size()) });
        buffer.put(value);
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) noexcept {
        buffer.getArray<int32_t, 1>();
        buffer.getArray(value);
        return std::is_constant_evaluated();
    }
};

template<MultiArrayType T>
struct IoSerialiser<YaS, T> {
    inline static constexpr uint8_t getDataTypeId() {
        // std::cout << fmt::format("getDataTypeID<{}>() = {}\n", typeName<typename T::value_type>(), yas::getDataTypeId<typename T::value_type>());
        return yas::ARRAY_TYPE_OFFSET + yas::getDataTypeId<typename T::value_type>();
    }
    constexpr static bool serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        std::array<int32_t, T::n_dims_> dims;
        for (uint32_t i = 0U; i < T::n_dims_; i++) {
            dims[i] = static_cast<int32_t>(value.dimensions()[i]);
        }
        buffer.put(dims);
        buffer.put(value.elements()); // todo: account for strides and offsets (possibly use iterators?)
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) noexcept {
        const std::array<int32_t, T::n_dims_> dimWire = buffer.getArray<int32_t, T::n_dims_>();
        for (auto i = 0U; i < T::n_dims_; i++) {
            value.dimensions()[i] = static_cast<typename T::size_t_>(dimWire[i]);
        }
        value.element_count()        = value.dimensions()[T::n_dims_];
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
struct IoSerialiser<YaS, START_MARKER> {
    inline static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<START_MARKER>(); }

    constexpr static bool           serialise(IoBuffer & /*buffer*/, const ClassField & /*field*/, const START_MARKER & /*value*/) noexcept {
        // do not do anything, as the start marker is of size zero and only the type byte is important
        return std::is_constant_evaluated();
    }

    constexpr static bool deserialise(IoBuffer & /*buffer*/, const ClassField & /*field*/, const START_MARKER &) {
        // do not do anything, as the start marker is of size zero and only the type byte is important
        return std::is_constant_evaluated();
    }
};

template<>
struct IoSerialiser<YaS, END_MARKER> {
    inline static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<END_MARKER>(); }
    static bool                     serialise(IoBuffer & /*buffer*/, const ClassField & /*field*/, const END_MARKER & /*value*/) noexcept {
        // do not do anything, as the end marker is of size zero and only the type byte is important
        return std::is_constant_evaluated();
    }

    constexpr static bool deserialise(IoBuffer & /*buffer*/, const ClassField & /*field*/, const END_MARKER &) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
        return std::is_constant_evaluated();
    }
};

template<SerialiserProtocol protocol, const bool writeMetaInfo, typename DataType>
std::size_t putFieldHeader(IoBuffer &buffer, const char *fieldName, const int fieldNameSize, const DataType &data) {
    using StrippedDataType         = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(data)))>;
    constexpr int32_t dataTypeSize = static_cast<int32_t>(sizeof(StrippedDataType));
    buffer.reserve_spare(((static_cast<uint64_t>(fieldNameSize) + 18) * sizeof(uint8_t)) + dataTypeSize);

    // -- offset 0 vs. field start
    const std::size_t headerStart = buffer.size();
    buffer.put(static_cast<uint8_t>(IoSerialiser<protocol, StrippedDataType>::getDataTypeId())); // data type ID
    buffer.put(opencmw::hash(fieldName, fieldNameSize));                                         // unique hashCode identifier -- TODO: choose more performant implementation instead of java default
    const std::size_t dataStartOffsetPosition = buffer.size();
    buffer.put(-1); // dataStart offset
    const int32_t     dataSize         = is_supported_number<DataType> ? dataTypeSize : -1;
    const std::size_t dataSizePosition = buffer.size();
    buffer.put(dataSize);                    // dataSize (N.B. 'headerStart' + 'dataStart + dataSize' == start of next field header
    buffer.put<std::string_view>(fieldName); // full field name

    if constexpr (is_annotated<DataType>) {
        if (writeMetaInfo) {
            buffer.put(std::string_view(data.getUnit()));
            buffer.put(std::string_view(data.getDescription()));
            buffer.put(static_cast<uint8_t>(data.getModifier()));
            // TODO: write group meta data
            //final String[] groups = fieldDescription.getFieldGroups().toArray(new String[0]); // java uses non-array string
            //buffer.putStringArray(groups, groups.length);
            //buffer.put<std::string[]>({""});
        }
    }

    const std::size_t dataStartPosition         = buffer.size();
    const std::size_t dataStartOffset           = (dataStartPosition - headerStart);     // -- offset dataStart calculations
    buffer.at<int32_t>(dataStartOffsetPosition) = static_cast<int32_t>(dataStartOffset); // write offset to dataStart

    // from hereon there are data specific structures that are written to the IoBuffer
    IoSerialiser<protocol, StrippedDataType>::serialise(buffer, fieldName, getAnnotatedMember(unwrapPointer(data)));

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
                CmwLight::protocolName(), typeName<T>, field, value,
                std::is_constant_evaluated());
        return false;
    }
};
} // namespace opencmw

// TODO: allow declaration of reflection within name-space
ENABLE_REFLECTION_FOR(opencmw::DeserialiserInfo, setFields, additionalFields, exceptions)

#pragma clang diagnostic pop
#endif //OPENCMW_IOSERIALISER_H
