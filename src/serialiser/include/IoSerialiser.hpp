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

enum ProtocolCheck {
    IGNORE,  // null return type
    LENIENT, // via return type
    ALWAYS   // via ProtocolException
};

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
template<SerialiserProtocol protocol>
inline void updateSize(IoBuffer & /*buffer*/, const size_t /*posSizePositionStart*/, const size_t /*posStartDataStart*/) {}

template<SerialiserProtocol protocol>
inline void putHeaderInfo(IoBuffer & /*buffer*/) {}

template<SerialiserProtocol protocol>
struct FieldHeader { // todo: remove struct and rename to putField
    template<const bool writeMetaInfo, typename DataType>
    constexpr std::size_t static putFieldHeader(IoBuffer & /*buffer*/, const char * /*fieldName*/, const int /*fieldNameSize*/, const DataType & /*data*/) { return 0; }
};

template<SerialiserProtocol protocol>
inline DeserialiserInfo checkHeaderInfo(IoBuffer & /*buffer*/, DeserialiserInfo info, const ProtocolCheck /*protocolCheckVariant*/) { return info; }

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

} // namespace opencmw

/* #################################################################################### */
/* ### Yet-Another-Serialiser - YaS Definitions ####################################### */
/* #################################################################################### */
struct START_MARKER {};
template<>
struct fmt::formatter<START_MARKER> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        auto it = ctx.begin(), end = ctx.end();
        // Check if reached the end of the range:
        if (it != end && *it != '}')
            throw format_error("invalid format");
        // Return an iterator past the end of the parsed range:
        return it;
    }

    template<typename FormatContext>
    auto format(START_MARKER const &startMarker, FormatContext &ctx) {
        std::ignore = startMarker;
        return fmt::format_to(ctx.out(), "START_MARKER\n");
    }
};

struct END_MARKER {};
template<>
struct fmt::formatter<END_MARKER> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        auto it = ctx.begin(), end = ctx.end();
        // Check if reached the end of the range:
        if (it != end && *it != '}')
            throw format_error("invalid format");
        // Return an iterator past the end of the parsed range:
        return it;
    }
    template<typename FormatContext>
    auto format(END_MARKER const &endMarker, FormatContext &ctx) {
        std::ignore = endMarker;
        return fmt::format_to(ctx.out(), "END_MARKER\n");
    }
};

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

template<>
struct FieldHeader<YaS> {
    template<const bool writeMetaInfo, typename DataType>
    constexpr std::size_t static putFieldHeader(IoBuffer &buffer, const char *fieldName, const int fieldNameSize, const DataType &data) { // todo fieldName -> string_view
        using StrippedDataType         = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(data)))>;
        constexpr int32_t dataTypeSize = static_cast<int32_t>(sizeof(StrippedDataType));
        buffer.reserve_spare(((static_cast<uint64_t>(fieldNameSize) + 18) * sizeof(uint8_t)) + dataTypeSize);

        // -- offset 0 vs. field start
        const std::size_t headerStart = buffer.size();
        buffer.put(static_cast<uint8_t>(IoSerialiser<YaS, StrippedDataType>::getDataTypeId())); // data type ID
        buffer.put(opencmw::hash(fieldName, fieldNameSize));                                    // unique hashCode identifier -- TODO: choose more performant implementation instead of java default
        const std::size_t dataStartOffsetPosition = buffer.size();
        buffer.put(-1); // dataStart offset
        const int32_t     dataSize         = is_supported_number<StrippedDataType> ? dataTypeSize : -1;
        const std::size_t dataSizePosition = buffer.size();
        buffer.put(dataSize);                    // dataSize (N.B. 'headerStart' + 'dataStart + dataSize' == start of next field header
        buffer.put<std::string_view>(fieldName); // full field name

        if constexpr (is_annotated<DataType> && writeMetaInfo) {
            //if (writeMetaInfo) {
            buffer.put(std::string_view(data.getUnit()));
            buffer.put(std::string_view(data.getDescription()));
            buffer.put(static_cast<uint8_t>(data.getModifier()));
            // TODO: write group meta data
            // final String[] groups = fieldDescription.getFieldGroups().toArray(new String[0]); // java uses non-array string
            // buffer.putStringArray(groups, groups.length);
            // buffer.put<std::string[]>({""});
            //}
        }

        const std::size_t dataStartPosition         = buffer.size();
        const std::size_t dataStartOffset           = (dataStartPosition - headerStart);     // -- offset dataStart calculations
        buffer.at<int32_t>(dataStartOffsetPosition) = static_cast<int32_t>(dataStartOffset); // write offset to dataStart

        // from hereon there are data specific structures that are written to the IoBuffer
        IoSerialiser<YaS, StrippedDataType>::serialise(buffer, fieldName, getAnnotatedMember(unwrapPointer(data)));

        // add just data-end position
        buffer.at<int32_t>(dataSizePosition) = static_cast<int32_t>(buffer.size() - dataStartPosition); // write data size

        return dataSizePosition; // N.B. exported for adjusting START_MARKER -> END_MARKER data size to be adjustable and thus skippable
    }
};

template<>
inline void updateSize<YaS>(IoBuffer &buffer, const size_t posSizePositionStart, const size_t posStartDataStart) {
    buffer.at<int32_t>(posSizePositionStart) = static_cast<int32_t>(buffer.size() - posStartDataStart); // write data size
}

template<>
inline void putHeaderInfo<YaS>(IoBuffer &buffer) {
    buffer.reserve_spare(2 * sizeof(int) + 7); // magic int + string length int + 4 byte 'YAS\0` string + 3 version bytes
    buffer.put(yas::VERSION_MAGIC_NUMBER);
    buffer.put(yas::PROTOCOL_NAME);
    buffer.put(yas::VERSION_MAJOR);
    buffer.put(yas::VERSION_MINOR);
    buffer.put(yas::VERSION_MICRO);
}

template<>
inline DeserialiserInfo checkHeaderInfo<YaS>(IoBuffer &buffer, DeserialiserInfo info, const ProtocolCheck protocolCheckVariant) {
    auto magic      = buffer.get<int>();
    auto proto_name = buffer.get<std::string>();
    auto ver_major  = buffer.get<int8_t>();
    auto ver_minor  = buffer.get<int8_t>();
    auto ver_micro  = buffer.get<int8_t>();
    if (yas::VERSION_MAGIC_NUMBER != magic) {
        if (protocolCheckVariant == LENIENT) {
            info.exceptions.template emplace_back(ProtocolException(fmt::format("Wrong serialiser magic number: {} != -1", magic)));
        }
        if (protocolCheckVariant == ALWAYS) {
            throw ProtocolException(fmt::format("Wrong serialiser magic number: {} != -1", magic));
        }
    }
    if (yas::PROTOCOL_NAME != proto_name) {
        if (protocolCheckVariant == LENIENT) {
            info.exceptions.template emplace_back(ProtocolException(fmt::format("Wrong serialiser identification string: {} != YaS", proto_name)));
        }
        if (protocolCheckVariant == ALWAYS) {
            throw ProtocolException(fmt::format("Wrong serialiser identification string: {} != YaS", proto_name));
        }
    }
    if (yas::VERSION_MAJOR != ver_major) {
        if (protocolCheckVariant == LENIENT) {
            info.exceptions.template emplace_back(ProtocolException(fmt::format("Major versions do not match, received {}.{}.{}", ver_major, ver_minor, ver_micro)));
        }
        if (protocolCheckVariant == ALWAYS) {
            throw ProtocolException(fmt::format("Major versions do not match, received {}.{}.{}", ver_major, ver_minor, ver_micro));
        }
    }
    return info;
}

} // namespace opencmw

/* #################################################################################### */
/* ### Json Definitions ########################################################### */
/* #################################################################################### */
namespace opencmw {

struct Json : Protocol<"Json"> {}; // todo: move to more appropriate place, but has to be declared before usage

namespace json {
}

template<>
struct FieldHeader<Json> {
    template<const bool writeMetaInfo, typename DataType>
    constexpr std::size_t static putFieldHeader(IoBuffer &buffer, const char *fieldName, const int fieldNameSize, const DataType &data) { // todo fieldName -> string_view
        if constexpr (std::is_same_v<DataType, START_MARKER>) {
            if (fieldNameSize > 0) {
                buffer.putRaw(fieldName);
                buffer.putRaw(":");
            }
            buffer.putRaw("{\n"); // use string put instead of other put // call serialise with the start marker instead?
            return 0;
        }
        if constexpr (std::is_same_v<DataType, END_MARKER>) {
            buffer.put('}');
            return 0;
        }
        buffer.putRaw(std::basic_string_view(fieldName, static_cast<size_t>(fieldNameSize)));
        buffer.put(':');
        using StrippedDataType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(data)))>;
        IoSerialiser<Json, StrippedDataType>::serialise(buffer, fieldName, getAnnotatedMember(unwrapPointer(data)));
        buffer.putRaw(",\n");
        return 0; // return value is irrelevant for Json
    }
};

template<>
struct IoSerialiser<Json, END_MARKER> { // catch all template
    inline static constexpr uint8_t getDataTypeId() { return 0; }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const END_MARKER & /*value*/) noexcept {
        buffer.putRaw("}");
        return false;
    }
    constexpr static bool deserialise(IoBuffer & /*buffer*/, const ClassField & /*field*/, const END_MARKER &) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
        return std::is_constant_evaluated();
    }
};

template<>
struct IoSerialiser<Json, START_MARKER> { // catch all template
    inline static constexpr uint8_t getDataTypeId() { return 0; }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const START_MARKER & /*value*/) noexcept {
        buffer.putRaw("{");
        return false;
    }
    constexpr static bool deserialise(IoBuffer & /*buffer*/, const ClassField & /*field*/, const START_MARKER & /*value*/) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
        return std::is_constant_evaluated();
    }
};

inline bool isJsonNumberChar(uint8_t c) {
    switch (c) {
    case '-':
    case '+':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '0':
    case '.':
    case 'e':
    case 'E':
        return true;
    default:
        return false;
    }
}

template<Number T>
struct IoSerialiser<Json, T> { // catch all template
    inline static constexpr uint8_t getDataTypeId() { return 0; }
    /*constexpr*/ static bool       serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        // todo: constexpr not possible because of fmt
        if constexpr (std::is_integral_v<T>) {
            // buffer.putRaw(std::to_string(value));
            buffer.putRaw(fmt::format("{}", value));
        } else {
            buffer.putRaw(fmt::format("{}", value));
        }
        return false;
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
        auto start = buffer.position();
        while (isJsonNumberChar(buffer.get<uint8_t>())) {
        }
        auto end = buffer.position();
        // value = stringTo
        std::ignore = start;
        std::ignore = end;
        std::ignore = value;
        return std::is_constant_evaluated();
    }
};
template<StringLike T>
struct IoSerialiser<Json, T> { // catch all template
    inline static constexpr uint8_t getDataTypeId() { return 0; }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        buffer.put('"');
        buffer.putRaw(value);
        buffer.put('"');
        return false;
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
        auto start = buffer.position();
        while (isJsonNumberChar(buffer.get<uint8_t>())) {
        }
        auto end = buffer.position();
        // value = stringTo
        std::ignore = start;
        std::ignore = end;
        std::ignore = value;
        return std::is_constant_evaluated();
    }
};

template<ArrayOrVector T>
struct IoSerialiser<Json, T> {
    inline static constexpr uint8_t getDataTypeId() { return 0; }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &values) noexcept {
        using MemberType = typename T::value_type;
        buffer.put('[');
        for (auto value : values) {
            // todo: use same formatting as for non array elements
            IoSerialiser<Json, MemberType>::serialise(buffer, "", value);
            buffer.putRaw(", ");
        }
        buffer.resize(buffer.size() - 2); // remove trailing comma
        buffer.put(']');
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
        auto start = buffer.position();
        while (isJsonNumberChar(buffer.get<uint8_t>())) {
        }
        auto end = buffer.position();
        // value = stringTo
        std::ignore = start;
        std::ignore = end;
        std::ignore = value;
        return std::is_constant_evaluated();
    }
};

template<MultiArrayType T>
struct IoSerialiser<Json, T> {
    inline static constexpr uint8_t getDataTypeId() { return 0; }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        buffer.putRaw("{\n");
        std::array<int32_t, T::n_dims_> dims;
        for (uint32_t i = 0U; i < T::n_dims_; i++) {
            dims[i] = static_cast<int32_t>(value.dimensions()[i]);
        }
        FieldHeader<Json>::template putFieldHeader<false>(buffer, "dims", 4, dims);
        FieldHeader<Json>::template putFieldHeader<false>(buffer, "values", 6, value.elements());
        buffer.putRaw("}");
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
        auto start = buffer.position();
        while (isJsonNumberChar(buffer.get<uint8_t>())) {
        }
        auto end = buffer.position();
        // value = stringTo
        std::ignore = start;
        std::ignore = end;
        std::ignore = value;
        return std::is_constant_evaluated();
    }
};

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
