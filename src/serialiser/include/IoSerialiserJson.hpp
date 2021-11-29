#ifndef OPENCMW_JSONSERIALISER_H
#define OPENCMW_JSONSERIALISER_H

#include "IoSerialiser.hpp"
#include <list>
#include <map>
#include <queue>

#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-c-arrays"

namespace opencmw {

struct Json : Protocol<"Json"> {};

namespace json {
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

/**
 * Read a json string from a buffer.
 * @param buffer An IoBuffer with the position at the start of a JSON string.
 * @return a string view of the string starting at the buffer position
 */
inline std::string_view readJsonString(IoBuffer &buffer) {
    buffer.set_position(buffer.position() + 1); // skip leading quote
    auto start = buffer.position();
    while (buffer.get<uint8_t>() != '"') {
        if (buffer.at<uint8_t>(buffer.position() - 1) == '\\') { // escape sequence detected
            switch (buffer.get<uint8_t>()) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                buffer.set_position(buffer.position() + 1); // skip escaped char
                break;
            case 'u':
                buffer.set_position(buffer.position() + 5); // skip escaped unicode char
                break;
            default:
                throw ProtocolException(fmt::format("illegal escape character: \\{}", buffer.at<uint8_t>(buffer.position() - 1)));
                // todo: lenient handling methods
            }
            // todo: correctly treat escape sequences instead of just skipping (breaks string_view usage)
        }
    }
    auto end = buffer.position() - 1;
    return { reinterpret_cast<char *>(buffer.data() + start), end - start };
}

/**
 * @param c the character to test
 * @return true if the character is a whitespace character as specified by the json specification
 */
inline bool isJsonWhitespace(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/**
 * Consume whitespace as defined by the JSON specification.
 * After calling this function the position of the passed buffer will point to the first non-whitespace character at/after the initial position.
 * @param buffer
 */
inline void consumeJsonWhitespace(IoBuffer &buffer) {
    while (isJsonWhitespace(buffer.at<uint8_t>(buffer.position()))) {
        buffer.set_position(buffer.position() + 1);
    }
}

} // namespace json

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
struct IoSerialiser<Json, OTHER> { // because json does not explicitly provide the datatype, all types except nested classes provide data type OTHER
    inline static constexpr uint8_t getDataTypeId() { return 0; }
};

template<>
struct IoSerialiser<Json, END_MARKER> {
    inline static constexpr uint8_t getDataTypeId() { return 1; }
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
    inline static constexpr uint8_t getDataTypeId() { return 2; }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const START_MARKER & /*value*/) noexcept {
        buffer.putRaw("{");
        return false;
    }
    constexpr static bool deserialise(IoBuffer & /*buffer*/, const ClassField & /*field*/, const START_MARKER & /*value*/) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
        return std::is_constant_evaluated();
    }
};

template<Number T>
struct IoSerialiser<Json, T> {
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
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
        auto start = buffer.position();
        while (json::isJsonNumberChar(buffer.get<uint8_t>())) {
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
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        buffer.put('"');
        buffer.putRaw(value);
        buffer.put('"');
        return false;
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) {
        value = json::readJsonString(buffer);
        return std::is_constant_evaluated();
    }
};

template<ArrayOrVector T>
struct IoSerialiser<Json, T> {
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
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
        while (json::isJsonNumberChar(buffer.get<uint8_t>())) {
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
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
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
        auto start = buffer.position();
        while (json::isJsonNumberChar(buffer.get<uint8_t>())) {
        }
        auto end = buffer.position();
        // value = stringTo
        std::ignore = start;
        std::ignore = end;
        std::ignore = value;
        return std::is_constant_evaluated();
    }
};

template<>
inline FieldDescription readFieldHeader<Json>(IoBuffer &buffer, DeserialiserInfo &info, const ProtocolCheck &protocolCheckVariant) {
    FieldDescription result;
    result.headerStart = buffer.position();
    json::consumeJsonWhitespace(buffer);
    if (buffer.at<char8_t>(buffer.position()) == ',') { // move to next field
        buffer.set_position(buffer.position() + 1);
        json::consumeJsonWhitespace(buffer);
        result.headerStart = buffer.position();
    }
    if (buffer.at<char8_t>(buffer.position()) == '{') { // start marker
        result.intDataType       = IoSerialiser<Json, START_MARKER>::getDataTypeId();
        result.dataStartPosition = buffer.position() + 1;
        // set rest of fields
        return result;
    }
    if (buffer.at<char8_t>(buffer.position()) == '}') { // end marker
        result.intDataType       = IoSerialiser<Json, END_MARKER>::getDataTypeId();
        result.dataStartPosition = buffer.position() + 1;
        // set rest of fields
        return result;
    }
    if (buffer.at<char8_t>(buffer.position()) == '"') { // string
        result.fieldName = json::readJsonString(buffer);
        if (result.fieldName.size() == 0) {
            //handleError<protocolCheckVariant>(info, "Cannot read field name for field at buffer position {}", buffer.position());
            const auto text = fmt::format("Cannot read field name for field at buffer position {}", buffer.position());
            if (protocolCheckVariant == ALWAYS) {
                throw ProtocolException(text);
            }
            info.exceptions.emplace_back(ProtocolException(text));
        }
        json::consumeJsonWhitespace(buffer);
        if (buffer.get<int8_t>() != ':') {
            std::cerr << "json malformed, no colon between key/value";
            // exception
        }
        json::consumeJsonWhitespace(buffer);
        // read value and set type ?
        if (buffer.at<char8_t>(buffer.position()) == '{') { // nested object
            result.intDataType = IoSerialiser<Json, START_MARKER>::getDataTypeId();
            buffer.set_position(buffer.position() + 1);
        } else {
            result.intDataType = IoSerialiser<Json, OTHER>::getDataTypeId(); // value is ignored anyway
        }
        result.dataStartPosition = buffer.position();
        result.dataStartOffset   = result.dataStartPosition - result.headerStart;
        result.dataSize          = std::numeric_limits<size_t>::max(); // not defined for non-skipable data
        result.dataEndPosition   = std::numeric_limits<size_t>::max(); // not defined for non-skipable data
        // there is also no way to get unit, description and modifiers
        return result;
    }
    return result;
}

} // namespace opencmw

#pragma clang diagnostic pop
#endif //OPENCMW_JSONSERIALISER_H
