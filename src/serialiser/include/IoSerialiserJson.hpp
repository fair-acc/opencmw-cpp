#ifndef OPENCMW_JSONSERIALISER_H
#define OPENCMW_JSONSERIALISER_H

#include "fast_float.h"
#include "IoSerialiser.hpp"
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
                if ((buffer.position() + 4) >= buffer.size() || !isHexNumber(data, 4)) {
                    throw ProtocolException("illegal hex number {} at position {} of {}", buffer.asString(buffer.position(), 4), buffer.position(), buffer.size());
                }
                buffer.skip<false>(4); // skip escaped unicode char
                result += '_';         // todo: implement parsing the actual unicode characters
                break;
            }
            default:
                throw ProtocolException("illegal escape character: \\{} at position {} of {}", buffer.at<uint8_t>(buffer.position() - 1), buffer.position(), buffer.size());
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
            throw ProtocolException("Escape characters not allowed in key: \\{}", buffer.at<uint8_t>(buffer.position() - 1));
        }
        i++;
    }
    buffer.set_position(i + 1);
    return buffer.asString(start, static_cast<int>(i - start));
}

inline constexpr void consumeWhitespace(IoBuffer &buffer) {
    while (buffer.position() < buffer.size() && isWhitespace(buffer.at<uint8_t>(buffer.position()))) {
        buffer.skip<false>(1);
    }
}

template<typename T>
inline constexpr void assignArray(std::vector<T> &value, const std::vector<T> &result) {
    value = result;
}

template<typename T, size_t N>
inline constexpr void assignArray(std::array<T, N> &value, const std::vector<T> &result) {
    if (N != result.size()) {
        throw ProtocolException("vector -> array size mismatch: source<{}>[{}]={} vs. destination std::array<T,{}>", typeName<T>, result.size(), result, typeName<T>, N);
    }
    for (size_t i = 0; i < N; ++i) {
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
    buffer.skip(1);
    consumeWhitespace(buffer);
}

inline constexpr void skipNumber(IoBuffer &buffer) {
    while (isNumberChar(buffer.get<uint8_t>())) {
    }
    buffer.skip<false>(-1);
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
    buffer.skip<false>(1);
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
    } else if (buffer.size() - buffer.position() > 5 && buffer.asString(buffer.position(), 5) == "false") {
        buffer.skip<false>(5);
    } else if (buffer.size() - buffer.position() > 4 && buffer.asString(buffer.position(), 4) == "true") {
        buffer.skip<false>(4);
    } else if (buffer.size() - buffer.position() > 4 && buffer.asString(buffer.position(), 4) == "null") {
        buffer.skip<false>(4);
    } else { // skip number
        json::skipNumber(buffer);
    }
}
} // namespace json

template<>
struct FieldHeaderWriter<Json> {
    template<const bool writeMetaInfo, typename DataType>
    constexpr std::size_t static put(IoBuffer &buffer, FieldDescription auto &&field, const DataType &data) {
        using namespace std::string_view_literals;
        constexpr auto WITHOUT = opencmw::IoBuffer::MetaInfo::WITHOUT;
        if constexpr (std::is_same_v<DataType, START_MARKER>) {
            if (field.fieldName.size() > 0) {
                buffer.put<WITHOUT>("\""sv);
                buffer.put<WITHOUT>(field.fieldName);
                buffer.put<WITHOUT>("\": "sv);
            }
            buffer.put<WITHOUT>("{\n"sv); // use string put instead of other put // call serialise with the start marker instead?
            return 0;
        }
        if constexpr (std::is_same_v<DataType, END_MARKER>) {
            if (buffer.template at<uint8_t>(buffer.size() - 2) == ',') {
                // proceeded by value, remove trailing comma
                buffer.resize(buffer.size() - 2);
                buffer.put<WITHOUT>("\n}"sv);
            } else {
                buffer.put('}');
            }
            return 0;
        }
        buffer.put<WITHOUT>("\""sv);
        buffer.put<WITHOUT>(field.fieldName);
        buffer.put<WITHOUT>("\": "sv);
        using StrippedDataType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(data)))>;
        IoSerialiser<Json, StrippedDataType>::serialise(buffer, field, getAnnotatedMember(unwrapPointer(data)));
        buffer.put<WITHOUT>(",\n"sv);
        return 0; // return value is irrelevant for Json
    }
};

template<>
struct IoSerialiser<Json, END_MARKER> {
    inline static constexpr uint8_t getDataTypeId() { return 1; }
    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const           &/*field*/, const END_MARKER           &/*value*/) noexcept {
        using namespace std::string_view_literals;
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>("}"sv);
    }
    constexpr static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, const END_MARKER &) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
    }
};

template<>
struct IoSerialiser<Json, START_MARKER> { // catch all template
    inline static constexpr uint8_t getDataTypeId() { return 2; }
    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const           &/*field*/, const START_MARKER           &/*value*/) noexcept {
        using namespace std::string_view_literals;
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>("{"sv);
    }
    constexpr static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, const START_MARKER & /*value*/) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
    }
};

template<>
struct IoSerialiser<Json, OTHER> { // because json does not explicitly provide the datatype, all types except nested classes provide data type OTHER
    inline static constexpr uint8_t getDataTypeId() { return 0; }
    constexpr static void           deserialise(IoBuffer &buffer, FieldDescription auto const           &/*field*/, const OTHER &) {
        json::skipValue(buffer);
    }
};

template<>
struct IoSerialiser<Json, bool> {
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const           &/*field*/, const bool &value) noexcept {
        using namespace std::string_view_literals;
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(value ? "true"sv : "false"sv);
    }
    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, bool &value) {
        using namespace std::string_view_literals;
        if (buffer.size() - buffer.position() > 5 && buffer.asString(buffer.position(), 5) == "false"sv) {
            buffer.skip<false>(5);
            value = false;
            return;
        }
        if (buffer.size() - buffer.position() > 4 && buffer.asString(buffer.position(), 4) == "true"sv) {
            buffer.skip<false>(4);
            value = false;
            value = true;
            return;
        }
        throw ProtocolException("unsupported boolean value");
    }
};

template<Number T>
struct IoSerialiser<Json, T> {
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const           &/*field*/, const T &value) {
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
            throw ProtocolException("error({}) serialising number at buffer position: {}", result.ec, start);
        }
        buffer.resize(static_cast<size_t>(result.ptr - data)); // new position
    }
    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) {
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
                throw ProtocolException("error({}) parsing number at buffer position: {}", result.ec, start);
            }
            buffer.set_position(static_cast<size_t>(result.ptr - data)); // new position
            return;
        }
        // fall-back
        const auto result = std::from_chars(data + start, data + stop, value);
        if (result.ec != std::errc()) {
            throw ProtocolException("error({}) parsing number at buffer position: {}", result.ec, start);
        }
        buffer.set_position(static_cast<size_t>(result.ptr - data)); // new position
    }
};
template<StringLike T>
struct IoSerialiser<Json, T> { // catch all template
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const           &/*field*/, const T &value) noexcept {
        buffer.put('"');
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(value);
        buffer.put('"');
    }
    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) {
        value = json::readString(buffer);
    }
};

template<ArrayOrVector T>
struct IoSerialiser<Json, T> {
    // todo: arrays of objects
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    inline constexpr static void    serialise(IoBuffer &buffer, FieldDescription auto const &field, const T &values) noexcept {
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
    }
    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const &field, T &value) {
        using MemberType = typename T::value_type;
        if (buffer.get<uint8_t>() != '[') {
            throw ProtocolException("expected [");
        }
        std::vector<MemberType> result;
        json::consumeWhitespace(buffer);
        if (buffer.get<uint8_t>() != ']') { // empty array
            buffer.skip<false>(-1);
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
    }
};

template<MultiArrayType T>
struct IoSerialiser<Json, T> {
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, OTHER>::getDataTypeId(); }
    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const           &/*field*/, const T &value) noexcept {
        using namespace std::string_view_literals;
        constexpr auto WITHOUT = opencmw::IoBuffer::MetaInfo::WITHOUT;
        buffer.put<WITHOUT>("{\n"sv);
        std::array<int32_t, T::n_dims_> dims;
        for (uint32_t i = 0U; i < T::n_dims_; i++) {
            dims[i] = static_cast<int32_t>(value.dimensions()[i]);
        }
        FieldHeaderWriter<Json>::template put<WITHOUT>(buffer, "dims"sv, dims);
        FieldHeaderWriter<Json>::template put<WITHOUT>(buffer, "values"sv, value.elements());
        buffer.put<WITHOUT>("}"sv);
    }
    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) {
        // todo: implement
        json::skipValue(buffer);
        std::ignore = value;
        std::ignore = buffer;
    }
};

template<>
struct FieldHeaderReader<Json> {
    template<ProtocolCheck protocolCheckVariant>
    constexpr static void get(IoBuffer &buffer, DeserialiserInfo &info, FieldDescription auto &result) {
        result.headerStart = buffer.position();
        json::consumeWhitespace(buffer);
        if (buffer.at<char8_t>(buffer.position()) == ',') { // move to next field
            buffer.skip<false>(1);
            json::consumeWhitespace(buffer);
            result.headerStart = buffer.position();
        }
        if (buffer.at<char8_t>(buffer.position()) == '{') { // start marker
            result.intDataType       = IoSerialiser<Json, START_MARKER>::getDataTypeId();
            result.dataStartPosition = buffer.position() + 1;
            // set rest of fields
            return;
        }
        if (buffer.at<char8_t>(buffer.position()) == '}') { // end marker
            result.intDataType       = IoSerialiser<Json, END_MARKER>::getDataTypeId();
            result.dataStartPosition = buffer.position() + 1;
            // set rest of fields
            return;
        }
        if (buffer.at<char8_t>(buffer.position()) == '"') { // string
            result.fieldName = json::readKey(buffer);
            if (result.fieldName.size() == 0) {
                detail::handleDeserialisationError<protocolCheckVariant>(info, "Cannot read field name for field at buffer position {}", buffer.position());
            }
            json::consumeWhitespace(buffer);
            if (buffer.get<int8_t>() != ':') {
                detail::handleDeserialisationError<protocolCheckVariant>(info, "json malformed, no colon between key/value at buffer position {}", buffer.position());
            }
            json::consumeWhitespace(buffer);
            // read value and set type ?
            if (buffer.at<char8_t>(buffer.position()) == '{') { // nested object
                result.intDataType = IoSerialiser<Json, START_MARKER>::getDataTypeId();
                buffer.skip<false>(1);
            } else {
                result.intDataType = IoSerialiser<Json, OTHER>::getDataTypeId(); // value is ignored anyway
            }
            result.dataStartPosition = buffer.position();
            result.dataEndPosition   = std::numeric_limits<size_t>::max(); // not defined for non-skippable data
            return;
        }

        detail::handleDeserialisationError<protocolCheckVariant>(info, "json malformed, unexpected '{}' at buffer position {}", buffer.at<char>(buffer.position()), buffer.position());
    }
};
} // namespace opencmw

#endif // OPENCMW_JSONSERIALISER_H
