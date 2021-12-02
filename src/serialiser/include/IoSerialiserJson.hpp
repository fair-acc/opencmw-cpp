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
inline bool isNumberChar(uint8_t c) {
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
inline std::string readString(IoBuffer &buffer) {
    if (buffer.get<uint8_t>() != '"') {
        throw ProtocolException("error: expected leading quote");
    }
    std::string result;
    for (auto currentChar = buffer.get<uint8_t>(); currentChar != '"'; currentChar = buffer.get<uint8_t>()) {
        if (buffer.at<uint8_t>(buffer.position() - 1) == '\\') { // escape sequence detected
            switch (buffer.get<uint8_t>()) {
            case '"':
            case '\\':
            case '/':
                result += buffer.at<char>(buffer.position() - 1);
                break;
            case 'b':
                result += '\b';
                break;
            case 'f':
                result += '\f';
                break;
            case 'n':
                result += '\n';
                break;
            case 'r':
                result += '\r';
                break;
            case 't':
                result += '\t';
                break;
            case 'u':
                buffer.set_position(buffer.position() + 4); // skip escaped unicode char
                result += '_';                              // todo: implement parsing the actual unicode characters
                break;
            default:
                throw ProtocolException(fmt::format("illegal escape character: \\{}", buffer.at<uint8_t>(buffer.position() - 1)));
                // todo: lenient error handling methods
            }
        } else {
            result += static_cast<char>(currentChar);
        }
    }
    return result;
}

/**
 * @param c the character to test
 * @return true if the character is a whitespace character as specified by the json specification
 */
inline bool isWhitespace(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/**
 * Consume whitespace as defined by the JSON specification.
 * After calling this function the position of the passed buffer will point to the first non-whitespace character at/after the initial position.
 * @param buffer
 */
inline void consumeWhitespace(IoBuffer &buffer) {
    while (buffer.position() < buffer.size() && isWhitespace(buffer.at<uint8_t>(buffer.position()))) {
        buffer.set_position(buffer.position() + 1);
    }
}

template<Number T>
inline T parseNumber(const std::string_view & /*num*/) {
    throw ProtocolException(fmt::format("json parser for number type {} not implemented!\n", typeName<T>));
};

template<>
inline double parseNumber<double>(const std::string_view &num) {
    return std::stod(std::string{ num });
}

template<>
inline float parseNumber<float>(const std::string_view &num) {
    return std::stof(std::string{ num });
}

template<>
inline long parseNumber<long>(const std::string_view &num) {
    return std::stol(std::string{ num });
}

template<>
inline int parseNumber<int>(const std::string_view &num) {
    return std::stoi(std::string{ num });
}

template<>
inline char parseNumber<char>(const std::string_view &num) {
    return static_cast<char>(std::stoul(std::string{ num }));
}

template<>
inline int8_t parseNumber<int8_t>(const std::string_view &num) { // byte
    return static_cast<int8_t>(parseNumber<int>(num));
}

template<>
inline int16_t parseNumber<int16_t>(const std::string_view &num) { // short
    return static_cast<int16_t>(parseNumber<int>(num));
}

template<typename T>
inline void assignArray(std::vector<T> &value, const std::vector<T> &result) {
    value = result;
}

template<typename T, size_t N>
inline void assignArray(std::array<T, N> &value, const std::vector<T> &result) {
    // todo: discuss if this is the right thing to do? throw error on size mismatch? zero out additional array entries?
    for (size_t i = 0; i < N && i < result.size(); ++i) {
        value[i] = result[i];
    }
}

inline void skipValue(IoBuffer &buffer);

inline void skipField(IoBuffer &buffer) {
    consumeWhitespace(buffer);
    std::ignore = readString(buffer);
    consumeWhitespace(buffer);
    if (buffer.get<int8_t>() != ':') {
        throw ProtocolException("json malformed, no colon between key/value");
    }
    consumeWhitespace(buffer);
    skipValue(buffer);
    consumeWhitespace(buffer);
}

inline void skipObject(IoBuffer &buffer) {
    if (buffer.get<uint8_t>() != '{') {
        throw ProtocolException("error: expected {");
    }
    consumeWhitespace(buffer);
    while (buffer.at<uint8_t>(buffer.position()) != '}') {
        skipField(buffer);
        if (buffer.at<uint8_t>(buffer.position()) == '}') break;
        if (buffer.get<uint8_t>() != ',') {
            throw ProtocolException("Expected comma to separate object fields");
        }
        consumeWhitespace(buffer);
    }
    buffer.set_position(buffer.position() + 1); // skip }
    consumeWhitespace(buffer);
}

inline void skipNumber(IoBuffer &buffer) {
    while (isNumberChar(buffer.get<uint8_t>())) {
    }
    buffer.set_position(buffer.position() - 1);
}

inline void skipArray(IoBuffer &buffer) {
    if (buffer.get<uint8_t>() != '[') {
        throw ProtocolException("error: expected [");
    }
    consumeWhitespace(buffer);
    while (buffer.at<uint8_t>(buffer.position()) != ']') {
        skipValue(buffer);
        if (buffer.at<uint8_t>(buffer.position()) == ']') break;
        if (buffer.get<uint8_t>() != ',') {
            throw ProtocolException("Expected comma to separate array entries");
        }
        consumeWhitespace(buffer);
    }
    buffer.set_position(buffer.position() + 1); // skip ]
    consumeWhitespace(buffer);
}

inline void skipValue(IoBuffer &buffer) {
    consumeWhitespace(buffer);
    const auto firstChar = buffer.at<int8_t>(buffer.position());
    if (firstChar == '{') {
        json::skipObject(buffer);
    } else if (firstChar == '[') {
        json::skipArray(buffer);
    } else if (firstChar == '"') {
        std::ignore = readString(buffer);
    } else { // skip number
        json::skipNumber(buffer);
    }
}
} // namespace json

template<>
struct FieldHeaderWriter<Json> {
    template<const bool writeMetaInfo, typename DataType>
    constexpr std::size_t static put(IoBuffer &buffer, const char *fieldName, const int fieldNameSize, const DataType &data) { // todo fieldName -> string_view
        if constexpr (std::is_same_v<DataType, START_MARKER>) {
            if (fieldNameSize > 0) {
                buffer.putRaw("\"");
                buffer.putRaw(fieldName);
                buffer.putRaw("\": ");
            }
            buffer.putRaw("{\n"); // use string put instead of other put // call serialise with the start marker instead?
            return 0;
        }
        if constexpr (std::is_same_v<DataType, END_MARKER>) {
            buffer.put('}');
            return 0;
        }
        buffer.putRaw("\"");
        buffer.putRaw(std::basic_string_view(fieldName, static_cast<size_t>(fieldNameSize)));
        buffer.putRaw("\": ");
        using StrippedDataType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(data)))>;
        IoSerialiser<Json, StrippedDataType>::serialise(buffer, fieldName, getAnnotatedMember(unwrapPointer(data)));
        buffer.putRaw(",\n");
        return 0; // return value is irrelevant for Json
    }
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

template<>
struct IoSerialiser<Json, OTHER> { // because json does not explicitly provide the datatype, all types except nested classes provide data type OTHER
    inline static constexpr uint8_t getDataTypeId() { return 0; }
    static bool                     deserialise(IoBuffer &buffer, const ClassField & /*field*/, const OTHER &) {
        json::skipValue(buffer);
        return false;
    }
};

template<>
struct IoSerialiser<Json, bool> {
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const bool &value) noexcept {
        buffer.putRaw(value ? "true" : "false");
        return std::is_constant_evaluated();
    }
    static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, bool &value) {
        if (buffer.size() - buffer.position() > 5 && std::string_view(reinterpret_cast<const char *>(buffer.data() + buffer.position()), 5) == "false") {
            buffer.set_position(buffer.position() + 5);
            value = false;
            return std::is_constant_evaluated();
        }
        if (buffer.size() - buffer.position() > 4 && std::string_view(reinterpret_cast<const char *>(buffer.data() + buffer.position()), 4) == "true") {
            buffer.set_position(buffer.position() + 4);
            value = false;
            value = true;
            return std::is_constant_evaluated();
        }
        throw ProtocolException("unsupported boolean value");
    }
};
template<Number T>
struct IoSerialiser<Json, T> {
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    /*constexpr*/ static bool       serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &value) noexcept {
        // todo: constexpr not possible because of fmt
        if constexpr (std::is_same_v<T, char>) {
            buffer.putRaw(fmt::format("{}", static_cast<uint8_t>(value)));
        } else if constexpr (std::is_integral_v<T>) {
            buffer.putRaw(fmt::format("{}", value));
        } else {
            buffer.putRaw(fmt::format("{}", value));
        }
        return false;
    }
    static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) {
        std::string string;
        for (size_t i = buffer.position(); json::isNumberChar(buffer.at<uint8_t>(i)); ++i) {
            string += buffer.at<char>(i);
            buffer.set_position(i + 1);
        }
        value = json::parseNumber<T>(string);
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
        value = json::readString(buffer);
        return std::is_constant_evaluated();
    }
};

template<ArrayOrVector T>
struct IoSerialiser<Json, T> {
    // todo: arrays of objects
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField & /*field*/, const T &values) noexcept {
        using MemberType = typename T::value_type;
        buffer.put('[');
        bool first = true;
        for (auto value : values) {
            if (!first) {
                buffer.putRaw(", ");
            }
            IoSerialiser<Json, MemberType>::serialise(buffer, "", value);
            first = false;
        }
        buffer.put(']');
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField &field, T &value) {
        using MemberType = typename T::value_type;
        if (buffer.get<uint8_t>() != '[') {
            throw ProtocolException("expected [");
        }
        std::vector<MemberType> result;
        json::consumeWhitespace(buffer);
        if (buffer.at<uint8_t>(buffer.position()) != ']') { // empty array
            while (true) {
                MemberType entry;
                IoSerialiser<Json, MemberType>::deserialise(buffer, field, entry);
                result.push_back(entry);
                json::consumeWhitespace(buffer);
                const auto next = buffer.template get<uint8_t>();
                if (next == ']') {
                    break;
                }
                if (next != ',') {
                    throw ProtocolException("expected comma or end of array");
                }
                json::consumeWhitespace(buffer);
            }
        }
        json::assignArray(value, result);
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
        FieldHeaderWriter<Json>::template put<false>(buffer, "dims", 4, dims);
        FieldHeaderWriter<Json>::template put<false>(buffer, "values", 6, value.elements());
        buffer.putRaw("}");
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) {
        // todo: implement
        std::ignore = value;
        std::ignore = buffer;
        return std::is_constant_evaluated();
    }
};

template<>
struct FieldHeaderReader<Json> {
    template<ProtocolCheck protocolCheckVariant>
    inline static FieldDescription get(IoBuffer &buffer, DeserialiserInfo &info) {
        FieldDescription result;
        result.headerStart = buffer.position();
        json::consumeWhitespace(buffer);
        if (buffer.at<char8_t>(buffer.position()) == ',') { // move to next field
            buffer.set_position(buffer.position() + 1);
            json::consumeWhitespace(buffer);
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
            result.fieldName = json::readString(buffer);
            if (result.fieldName.size() == 0) {
                handleError<protocolCheckVariant>(info, "Cannot read field name for field at buffer position {}", buffer.position());
            }
            json::consumeWhitespace(buffer);
            if (buffer.get<int8_t>() != ':') {
                handleError<protocolCheckVariant>(info, "json malformed, no colon between key/value at buffer position {}", buffer.position());
            }
            json::consumeWhitespace(buffer);
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
            return result;
        }
        return result;
    }
};
} // namespace opencmw

#pragma clang diagnostic pop
#endif //OPENCMW_JSONSERIALISER_H
