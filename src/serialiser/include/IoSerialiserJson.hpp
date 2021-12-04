#ifndef OPENCMW_JSONSERIALISER_H
#define OPENCMW_JSONSERIALISER_H

#include "IoSerialiser.hpp"
#include "fast_float.h"
#include <charconv>
#include <cmath>
#include <list>
#include <map>
#include <queue>

namespace opencmw {

struct Json : Protocol<"Json"> {}; // as specified in https://www.json.org/json-en.html

namespace json {

constexpr inline bool isNumberChar(uint8_t c) { return (c >= '+' && c <= '9' && c != '/' && c != ',') || c == 'e' || c == 'E'; }
constexpr inline bool isWhitespace(uint8_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
constexpr inline bool isHexNumberChar(int8_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
constexpr inline bool isHexNumber(const char *start, const size_t size) noexcept {
    for (size_t i = 0; i < size; i++) {
        if (!isHexNumberChar(start[i])) {
            return false;
        }
    }
    return true;
}

inline std::string readString(IoBuffer &buffer) {
    if (buffer.position() >= buffer.size()) {
        return "";
    }
    if (buffer.get<uint8_t>() != '"') {
        throw ProtocolException("readString(IoBuffer) error: expected leading quote");
    }

    std::string result;
    result.reserve(20);
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
            case 'u': {
                char *data = reinterpret_cast<char *>(buffer.data() + buffer.position());
                if (!isHexNumber(data, 4) || (buffer.position() + 4) >= buffer.size()) {
                    throw ProtocolException(fmt::format("illegal hex number {} at position {} of {}", std::string{ data, 4 }, buffer.position(), buffer.size()));
                }
                buffer.set_position(buffer.position() + 4); // skip escaped unicode char
                result += '_';                              // todo: implement parsing the actual unicode characters
                break;
            }
            default:
                throw ProtocolException(fmt::format("illegal escape character: \\{} at position {} of {}", buffer.at<uint8_t>(buffer.position() - 1), buffer.position(), buffer.size()));
                // todo: lenient error handling methods
            }
        } else {
            result += static_cast<char>(currentChar);
        }
    }
    return result;
}

inline std::string_view readKey(IoBuffer &buffer) {
    if (buffer.get<uint8_t>() != '"') {
        throw ProtocolException("readKey(IoBuffer) error: expected leading quote");
    }
    const auto start = buffer.position();
    auto       i     = start;
    while (buffer.at<uint8_t>(i) != '"') {
        if (buffer.at<uint8_t>(i) == '\\') { // escape sequence detected
            throw ProtocolException(fmt::format("Escape characters not allowed in key: \\{}", buffer.at<uint8_t>(buffer.position() - 1)));
        }
        i++;
    }
    buffer.set_position(i + 1);
    return std::string_view(reinterpret_cast<const char *>(buffer.data() + start), i - start);
}

inline constexpr void consumeWhitespace(IoBuffer &buffer) {
    while (buffer.position() < buffer.size() && isWhitespace(buffer.at<uint8_t>(buffer.position()))) {
        buffer.set_position(buffer.position() + 1);
    }
}

template<typename T>
inline constexpr void assignArray(std::vector<T> &value, const std::vector<T> &result) {
    value = result;
}

template<typename T, size_t N>
inline constexpr void assignArray(std::array<T, N> &value, const std::vector<T> &result) {
    if (value.size() != result.size()) {
        throw ProtocolException(fmt::format("vector -> array size mismatch: source<{}>[{}]={} vs. destination std::array<T,{}>", //
                typeName<T>, result.size(), result, typeName<T>, N));
    }
    for (size_t i = 0; i < N && i < result.size(); ++i) {
        value[i] = result[i];
    }
}

inline constexpr void skipValue(IoBuffer &buffer);

inline void           skipField(IoBuffer &buffer) {
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

inline constexpr void skipObject(IoBuffer &buffer) {
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

inline constexpr void skipNumber(IoBuffer &buffer) {
    while (isNumberChar(buffer.get<uint8_t>())) {
    }
    buffer.set_position(buffer.position() - 1);
}

inline constexpr void skipArray(IoBuffer &buffer) {
    if (buffer.get<uint8_t>() != '[') {
        throw ProtocolException("error: expected [");
    }
    consumeWhitespace(buffer);
    while (buffer.at<uint8_t>(buffer.position()) != ']') {
        skipValue(buffer);
        consumeWhitespace(buffer);
        if (buffer.at<uint8_t>(buffer.position()) == ']') break;
        if (buffer.get<uint8_t>() != ',') {
            throw ProtocolException("Expected comma to separate array entries");
        }
        consumeWhitespace(buffer);
    }
    buffer.set_position(buffer.position() + 1); // skip ]
    consumeWhitespace(buffer);
}

inline constexpr void skipValue(IoBuffer &buffer) {
    consumeWhitespace(buffer);
    const auto firstChar = buffer.at<int8_t>(buffer.position());
    if (firstChar == '{') {
        json::skipObject(buffer);
    } else if (firstChar == '[') {
        json::skipArray(buffer);
    } else if (firstChar == '"') {
        std::ignore = readString(buffer);
    } else if (buffer.size() - buffer.position() > 5 && std::string_view(reinterpret_cast<const char *>(buffer.data() + buffer.position()), 5) == "false") {
        buffer.set_position(buffer.position() + 5);
    } else if (buffer.size() - buffer.position() > 4 && std::string_view(reinterpret_cast<const char *>(buffer.data() + buffer.position()), 4) == "true") {
        buffer.set_position(buffer.position() + 4);
    } else if (buffer.size() - buffer.position() > 4 && std::string_view(reinterpret_cast<const char *>(buffer.data() + buffer.position()), 4) == "null") {
        buffer.set_position(buffer.position() + 4);
    } else { // skip number
        json::skipNumber(buffer);
    }
}
} // namespace json

template<>
struct FieldHeaderWriter<Json> {
    template<const bool writeMetaInfo, typename DataType>
    constexpr std::size_t static put(IoBuffer &buffer, const std::string_view &fieldName, const DataType &data) {
        using namespace std::string_view_literals;
        if constexpr (std::is_same_v<DataType, START_MARKER>) {
            if (fieldName.size() > 0) {
                buffer.putRaw("\""sv);
                buffer.putRaw(fieldName);
                buffer.putRaw("\": "sv);
            }
            buffer.putRaw("{\n"); // use string put instead of other put // call serialise with the start marker instead?
            return 0;
        }
        if constexpr (std::is_same_v<DataType, END_MARKER>) {
            buffer.put('}');
            return 0;
        }
        buffer.putRaw("\""sv);
        buffer.putRaw(fieldName);
        buffer.putRaw("\": "sv);
        using StrippedDataType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(data)))>;
        IoSerialiser<Json, StrippedDataType>::serialise(buffer, fieldName, getAnnotatedMember(unwrapPointer(data)));
        buffer.putRaw(",\n");
        return 0; // return value is irrelevant for Json
    }
};

template<>
struct IoSerialiser<Json, END_MARKER> {
    inline static constexpr uint8_t getDataTypeId() { return 1; }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField           &/*field*/, const END_MARKER           &/*value*/) noexcept {
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
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField           &/*field*/, const START_MARKER           &/*value*/) noexcept {
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
    static bool                     deserialise(IoBuffer &buffer, const ClassField                     &/*field*/, const OTHER &) {
        json::skipValue(buffer);
        return false;
    }
};

template<>
struct IoSerialiser<Json, bool> {
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField           &/*field*/, const bool &value) noexcept {
        using namespace std::string_view_literals;
        buffer.putRaw(value ? "true"sv : "false"sv);
        return std::is_constant_evaluated();
    }
    static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, bool &value) {
        using namespace std::string_view_literals;
        if (buffer.size() - buffer.position() > 5 && std::string_view(reinterpret_cast<const char *>(buffer.data() + buffer.position()), 5) == "false"sv) {
            buffer.set_position(buffer.position() + 5);
            value = false;
            return std::is_constant_evaluated();
        }
        if (buffer.size() - buffer.position() > 4 && std::string_view(reinterpret_cast<const char *>(buffer.data() + buffer.position()), 4) == "true"sv) {
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
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField           &/*field*/, const T &value) {
        buffer.reserve_spare(30); // just reserve some spare capacity and expect that all numbers are shorter
        const auto           start = buffer.size();
        auto                 size  = buffer.capacity();
        auto                 data  = reinterpret_cast<char *>(buffer.data());
        std::to_chars_result result;
        if constexpr (std::is_floating_point_v<T>) {
            result = std::to_chars(data + start, data + size, value, std::chars_format::scientific);
        } else { // fall-back
            result = std::to_chars(data + start, data + size, value);
        }
        if (result.ec != std::errc()) {
            throw ProtocolException(fmt::format("error({}) serialising number at buffer position: {}", result.ec, start));
        }
        buffer.resize(static_cast<size_t>(result.ptr - data)); // new position
        return std::is_constant_evaluated();
    }
    static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) {
        const auto start = buffer.position();
        const auto data  = reinterpret_cast<const char *>(buffer.data());
        // N.B. assumes that any reasonable number representation has a maximum of 30 characters
        const auto stop = start + 30;

        //        auto     stop  = 0;
        //        for (size_t i = start; i < buffer.size(); i++) {
        //            if (!json::isNumberChar(buffer.data()[i])) {
        //                stop = i;
        //                break;
        //            }
        //        }
        if constexpr (std::is_floating_point_v<T>) {
            const auto result = fast_float::from_chars(data + start, data + stop, value);
            if (result.ec != std::errc()) {
                throw ProtocolException(fmt::format("error({}) parsing number at buffer position: {}", result.ec, start));
            }
            buffer.set_position(static_cast<size_t>(result.ptr - data)); // new position
            return std::is_constant_evaluated();
        }
        // fall-back
        const auto result = std::from_chars(data + start, data + stop, value);
        if (result.ec != std::errc()) {
            throw ProtocolException(fmt::format("error({}) parsing number at buffer position: {}", result.ec, start));
        }
        buffer.set_position(static_cast<size_t>(result.ptr - data)); // new position
        return std::is_constant_evaluated();
    }
};
template<StringLike T>
struct IoSerialiser<Json, T> { // catch all template
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField           &/*field*/, const T &value) noexcept {
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
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField &field, const T &values) noexcept {
        using MemberType = typename T::value_type;
        buffer.put('[');
        bool first = true;
        for (auto value : values) {
            if (!first) {
                buffer.put(',');
                buffer.put(' ');
            }
            IoSerialiser<Json, MemberType>::serialise(buffer, field, value);
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
        if (buffer.get<uint8_t>() != ']') { // empty array
            buffer.set_position(buffer.position() - 1);
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
    constexpr static bool           serialise(IoBuffer &buffer, const ClassField           &/*field*/, const T &value) noexcept {
        using namespace std::string_view_literals;
        buffer.putRaw("{\n"sv);
        std::array<int32_t, T::n_dims_> dims;
        for (uint32_t i = 0U; i < T::n_dims_; i++) {
            dims[i] = static_cast<int32_t>(value.dimensions()[i]);
        }
        FieldHeaderWriter<Json>::template put<false>(buffer, "dims"sv, 4, dims);
        FieldHeaderWriter<Json>::template put<false>(buffer, "values"sv, 6, value.elements());
        buffer.putRaw("}"sv);
        return std::is_constant_evaluated();
    }
    constexpr static bool deserialise(IoBuffer &buffer, const ClassField & /*field*/, T &value) {
        // todo: implement
        json::skipValue(buffer);
        std::ignore = value;
        std::ignore = buffer;
        return std::is_constant_evaluated();
    }
};

template<>
struct FieldHeaderReader<Json> {
    template<ProtocolCheck protocolCheckVariant>
    inline constexpr static FieldDescription get(IoBuffer &buffer, DeserialiserInfo &info) {
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
            result.fieldName = json::readKey(buffer);
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
            result.dataSize          = std::numeric_limits<size_t>::max(); // not defined for non-skippable data
            result.dataEndPosition   = std::numeric_limits<size_t>::max(); // not defined for non-skippable data
            return result;
        }
        return result;
    }
};
} // namespace opencmw

#endif // OPENCMW_JSONSERIALISER_H
