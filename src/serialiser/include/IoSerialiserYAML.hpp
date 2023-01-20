#ifndef OPENCMW_CPP_IOSERIALISERYAML_HPP
#define OPENCMW_CPP_IOSERIALISERYAML_HPP

#include "fast_float.h"
#include "IoSerialiser.hpp"
#include <charconv>
#include <ranges>
#include <set>

#ifndef IOSERIALISER_YAML_INDENTATION
#define IOSERIALISER_YAML_INDENTATION 2 // NOSONAR -- allow users to redefine if other preference are requested
#endif

#ifndef IOSERIALISER_YAML_META_POSITION
#define IOSERIALISER_YAML_META_POSITION 80U // NOSONAR -- allow users to redefine if other preference are requested
#endif

namespace opencmw {

struct YAML : Protocol<"YAML"> {}; // as specified in https://yaml.org/spec/1.2.2/

namespace yaml::detail {

constexpr int  getIndentation(const uint8_t hierarchyDepth, const int additional = 0U) { return hierarchyDepth * IOSERIALISER_YAML_INDENTATION + additional; }
constexpr int  getMetaPosition() { return IOSERIALISER_YAML_META_POSITION; }

constexpr void addSpace(IoBuffer &buffer, const uint8_t hierarchyDepth, const uint8_t additional = 0) {
    for (auto i = 0; i < getIndentation(hierarchyDepth, additional); i++) {
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(' ');
    }
}

[[nodiscard]] constexpr int consumeWhitespace(IoBuffer &buffer) noexcept {
    int            count        = 0;
    constexpr auto isWhiteSpace = [](uint8_t c) noexcept { return c == ' ' || c == '\t' || c == '\r'; };
    while (buffer.position() < buffer.size() && isWhiteSpace(buffer.at<uint8_t>(buffer.position()))) {
        buffer.skip<false>(1);
        count++;
    }
    return count;
}

[[nodiscard]] constexpr int moveToEndOfLine(IoBuffer &buffer) {
    if (buffer.position() == buffer.size()) {
        return 0;
    }
    const auto start = buffer.position();
    while (buffer.position() < buffer.size() && buffer.at<char>(buffer.position()) != '\n') {
        buffer.skip<false>(1);
    }
    if (buffer.at<char>(buffer.position()) == '\n') {
        buffer.skip<false>(1);
    }
    return static_cast<int>(buffer.position() - start);
}

[[nodiscard]] inline std::string_view peekLine(IoBuffer &buffer) {
    const auto start = buffer.position();
    const auto end   = moveToEndOfLine(buffer);
    buffer.set_position(start);
    return buffer.asString<false>(start, end - 1);
}

[[nodiscard]] constexpr std::string_view parseNonWhiteSpaceData(IoBuffer &buffer) {
    constexpr auto isWhiteSpace = [](uint8_t c) noexcept { return c == ' ' || c == '\t' || c == '\r'; };
    auto           start        = buffer.position();
    while (start < buffer.size() && isWhiteSpace(buffer.at<uint8_t>(start))) {
        start++;
    } // swallow leading white-spaces
    auto end                          = start;
    auto firstWhiteCharacterAfterData = start;
    while (end < buffer.size() && buffer.at<uint8_t>(end) != '#' && buffer.at<uint8_t>(end) != '\n') { // move to comment '#' or line-break '\n'
        if (!isWhiteSpace(buffer.at<uint8_t>(end))) {
            firstWhiteCharacterAfterData = end + 1;
        }
        end++;
    }

    return buffer.asString(start, static_cast<int>(firstWhiteCharacterAfterData - start));
}

template<ProtocolCheck protocolCheckVariant, typename... ErrorArgs>
neverinline constexpr void handleDeserialisationError(DeserialiserInfo &info, fmt::format_string<ErrorArgs...> errorFormat, ErrorArgs &&...errorArgs) noexcept(protocolCheckVariant != ProtocolCheck::ALWAYS) {
    if constexpr (protocolCheckVariant == ProtocolCheck::ALWAYS) {
        throw ProtocolException(errorFormat, errorArgs...);
    }
    info.exceptions.emplace_back(ProtocolException(errorFormat, std::forward<ErrorArgs>(errorArgs)...));
}

inline std::string_view readKey(IoBuffer &buffer) {
    const auto start = buffer.position();
    auto       i     = start;
    while (i < (buffer.size() - 1) && buffer.at<uint8_t>(i) != ':') {
        if (buffer.at<uint8_t>(i) == '\n') {
            throw ProtocolException("encountered pre-mature line-break while reading key at {} - string: '{}'", i, buffer.asString(start, static_cast<int32_t>(i - start)));
        }
        i++;
        if (i < (buffer.size() - 2) && buffer.at<uint8_t>(i) == ':' && buffer.at<uint8_t>(i + 1) == ':') { // encountered a '::' skip ahead
            i += 2;
        }
    }
    if (buffer.at<uint8_t>(i) == '\n' && buffer.at<uint8_t>(i - 1) == '.' && buffer.at<uint8_t>(i - 2) == '.' && buffer.at<uint8_t>(i - 3) == '.') {
        buffer.set_position(i);
        return buffer.asString(start, static_cast<int32_t>(i - start));
    }
    if (buffer.at<uint8_t>(i) != ':') {
        throw ProtocolException("missing colon (':') while reading key at {} '{}' - string: '{}'", i, buffer.at<uint8_t>(i), buffer.asString(start, static_cast<int32_t>(i - start)));
    }
    if (buffer.at<uint8_t>(i + 1) != ' ' && buffer.at<uint8_t>(i + 1) != '\n') {
        throw ProtocolException("missing space or line-break after colon while reading key at {} - string: '{}'", i, buffer.asString(start, static_cast<int32_t>(i - start)));
    }
    buffer.set_position(i + 1);

    return buffer.asString(start, static_cast<int32_t>(i - start));
}

constexpr std::string_view stringTokenizer(std::string_view &input, const char delimiter) noexcept {
    if (input.empty()) {
        return {};
    }
    input.remove_prefix(std::min(input.find_first_not_of(' '), input.size())); // trimming leading spaces
    if (input.empty()) {
        return {};
    }
    if (const auto second = std::min(input.find_first_of(delimiter), input.size() + 1); second != 0) {
        const auto end    = second < input.size() ? input.find_last_not_of(' ', second + 1) : second; // trimming trailing spaces
        auto       result = input.substr(0, end);
        input.remove_prefix(std::min(second + 1, input.size()));
        return result;
    }
    return {};
}

template<typename T>
inline std::string fieldFormatter(const T &value, const int nIndentation) noexcept {
    return nIndentation == 0 ? fmt::format("\"{}\"", value) : fmt::format("{:<{}}\"{}\"\n", ' ', nIndentation, value);
}

template<bool SingleLineParameter>
inline void fieldParser(const std::string_view &data, auto &value) {
    if constexpr (SingleLineParameter) {
        throw ProtocolException("YAML - single-line parameter parsing for type {} not supported, data : '{}'", typeName<std::remove_reference_t<decltype(value)>>, data);
    }
    throw ProtocolException("YAML - multi-line parameter parsing for type {} not supported, data : '{}'", typeName<std::remove_reference_t<decltype(value)>>, data);
}

inline std::string fieldFormatter(ArithmeticType auto const &value, const int nIndentation = 0) noexcept {
    if constexpr (std::is_same_v<decltype(value), bool>) {
        return value ? "true" : "false";
    }
    return nIndentation == 0 ? fmt::format("{}", value) : fmt::format("{:<{}}{}\n", ' ', nIndentation, value);
}

template<bool SingleLineParameter>
constexpr void fieldParser(const std::string_view &data, std::floating_point auto &value) {
    if (const auto result = fast_float::from_chars(data.cbegin(), data.cend(), value); result.ec != std::errc()) {
        throw ProtocolException("parsing ArithmeticType (float) from string '{}'", data);
    }
}

template<bool SingleLineParameter>
constexpr void fieldParser(const std::string_view &data, bool &value) {
    using namespace std::string_view_literals;
    if (SingleLineParameter) {
        if (data == "false"sv || data == "0"sv) {
            value = false;
            return;
        }
        if (data == "true"sv || data == "1"sv) {
            value = true;
            return;
        }
        throw ProtocolException("parsing ArithmeticType (bool) from string '{}'", data);
    }
    // is multi-line parameter, strip meta-info and leading spaces
    const std::string_view parse = data.substr(data.find_first_not_of(' ', data.find_first_of('\n') + 1));
    fieldParser<true>(parse, FWD(value));
}

template<bool SingleLineParameter, std::integral DestDataType>
constexpr void fieldParser(const std::string_view &data, DestDataType &value) {
    if (SingleLineParameter) {
        if constexpr (std::is_same_v<DestDataType, char>) {
            if (data.size() != 1) {
                throw ProtocolException("parsing ArithmeticType (char) from string '{}'", data);
            }
            value = data[0];
            return;
        } else {
            const auto result = std::from_chars(data.data(), data.data() + data.size(), value); // fall-back
            if (result.ec != std::errc()) {
                throw ProtocolException("parsing ArithmeticType (integral) from string '{}'", data);
            }
        }
        return;
    }
    // is multi-line parameter, strip meta-info and leading spaces
    const std::string_view parse = data.substr(data.find_first_not_of(' ', data.find_first_of('\n') + 1));
    fieldParser<true, DestDataType>(parse, FWD(value));
}

inline std::string fieldFormatter(StringLike auto const &value, const int nIndentation = 0) noexcept {
    return nIndentation == 0 ? fmt::format("\"{}\"", value) : fmt::format("{:<{}}\"{}\"\n", ' ', nIndentation, value);
}

template<bool SingleLineParameter>
inline void fieldParser(const std::string_view &data, StringLike auto &value) {
    if constexpr (SingleLineParameter) {
        if (data.size() > 2 && data.starts_with('\"') && data.ends_with('\"')) {
            value = data.substr(1, data.size() - 2);
            return;
        }
        value = std::string{ FWD(data) };
        return;
    }
    throw ProtocolException("reading multi-line strings is not yet supported"); // TODO: read multi-line string
}

inline std::string fieldFormatter(ArrayOrVector auto const &value, const int nIndentation = 0) noexcept {
    if (nIndentation == 0) {
        return fmt::format("[{}]", fmt::join(std::cbegin(value), std::cend(value), ", "));
    }
    const auto joinDelimiter = fmt::format("\n{:<{}}- ", ' ', nIndentation);
    return fmt::format("{:<{}}- {}\n", ' ', nIndentation, fmt::join(std::cbegin(value), std::cend(value), joinDelimiter));
}

template<bool SingleLineParameter, ArrayOrVector ContainerType>
inline void fieldParser(const std::string_view &data, ContainerType &value) {
    if constexpr (requires { value.clear(); }) {
        value.clear();
    }
    if constexpr (SingleLineParameter) { // single-line JSON-style array
        if (!data.starts_with('[') || !data.ends_with(']')) {
            throw ProtocolException("error parsing array from string '{}' -- missing brackets '[]' for single-line representation", data);
        }
        std::string_view parse     = data.substr(1, data.size() - 2);
        unsigned         tokenSize = 0;
        std::string_view token;
        while (!(token = stringTokenizer(parse, ',')).empty()) {
            if constexpr (requires { value.resize(2); }) {
                value.resize(value.size() + 1);
            }
            fieldParser<true>(token, value[tokenSize++]);
        }
        if (value.size() != tokenSize) {
            throw ProtocolException("error parsing array from string '{}' -- size mismatch have {} need {} ", data, value.size(), tokenSize);
        }
        return;
    }
    std::string_view parse     = data.substr(data.find_first_of('\n') + 1);
    unsigned         tokenSize = 0;
    std::string_view token;
    while (!(token = stringTokenizer(parse, '\n')).empty()) {
        if (token.starts_with('#') || token.starts_with('\n')) {
            continue; // skip commented or empty lines
        }
        if constexpr (requires { value.resize(2); }) {
            value.resize(value.size() + 1);
        }
        fieldParser<true>(token.substr(2), value[tokenSize++]);
    }
    if (value.size() != tokenSize) {
        throw ProtocolException("error parsing array from string '{}' -- size mismatch have {} need {} ", data, value.size(), tokenSize);
    }
}

template<typename V>
inline std::string fieldFormatter(std::set<V> const &value, const int nIndentation = 0) noexcept {
    if (nIndentation == 0) {
        if constexpr (is_stringlike<V>) {
#if __cpp_lib_ranges >= 202106L
            return fmt::format("[{}]", fmt::join(std::ranges::views::transform(value, [](const auto &v) { return "\"" + v + "\""; }), ", "));
#else
        } else if (is_stringlike<V>) { // version for clang without ranges
            std::vector<V> vals{ value.size() };
            std::transform(value.begin(), value.end(), vals.begin(), [](const auto &v) { return "\"" + v + "\""; });
            return fmt::format("[{}]", fmt::join(vals.begin(), vals.end(), ", "));
#endif
        } else {
            return fmt::format("[{}]", fmt::join(value, ", "));
        }
    }
    const auto joinDelimiter = fmt::format("\n{:<{}}- ", ' ', nIndentation);
    if constexpr (is_stringlike<V>) {
#if __cpp_lib_ranges >= 202106L
        return fmt::format("{:<{}}- {}\n", ' ', nIndentation, fmt::join(std::ranges::views::transform(value, [](const auto &v) { return "\"" + v + "\""; }), joinDelimiter));
#else
        std::vector<V> vals{ value.size() };
        std::transform(value.begin(), value.end(), vals.begin(), [](const auto &v) { return "\"" + v + "\""; });
        return fmt::format("{:<{}}- {}\n", ' ', nIndentation, fmt::join(vals.begin(), vals.end(), joinDelimiter));
#endif
    } else {
        return fmt::format("{:<{}}- {}\n", ' ', nIndentation, fmt::join(value, joinDelimiter));
    }
}

template<bool SingleLineParameter, typename V>
inline void fieldParser(const std::string_view &data, std::set<V> &value) {
    if constexpr (requires { value.clear(); }) {
        value.clear();
    }
    if constexpr (SingleLineParameter) { // single-line JSON-style array
        if (!data.starts_with('[') || !data.ends_with(']')) {
            throw ProtocolException("error parsing array from string '{}' -- missing brackets '[]' for single-line representation", data);
        }
        std::string_view parse     = data.substr(1, data.size() - 2);
        unsigned         tokenSize = 0;
        std::string_view token;
        while (!(token = stringTokenizer(parse, ',')).empty()) {
            ++tokenSize;
            V newval;
            fieldParser<true>(token, newval);
            value.insert(newval);
        }
        if (value.size() != tokenSize) {
            throw ProtocolException("error parsing array from string '{}' -- size mismatch have {} need {} ", data, value.size(), tokenSize);
        }
        return;
    }
    std::string_view parse     = data.substr(data.find_first_of('\n') + 1);
    unsigned         tokenSize = 0;
    std::string_view token;
    while (!(token = stringTokenizer(parse, '\n')).empty()) {
        if (token.starts_with('#') || token.starts_with('\n')) {
            continue; // skip commented or empty lines
        }
        V newval;
        fieldParser<true>(token.substr(2), newval);
        value.insert(newval);
    }
    if (value.size() != tokenSize) {
        throw ProtocolException("error parsing array from string '{}' -- size mismatch have {} need {} ", data, value.size(), tokenSize);
    }
}

inline std::string fieldFormatter(MapLike auto const &value, const int nSpaces = 0) noexcept {
    std::stringstream ss;
    if (nSpaces == 0) {
        ss << '{';
        bool first = true;
        for (auto &[k, v] : value) {
            if (first) {
                ss << fmt::format("{}: {}", k, fieldFormatter(FWD(v), 0));
                first = false;
            } else {
                ss << fmt::format(", {}: {}", k, fieldFormatter(FWD(v), 0));
            }
        }
        ss << '}';
        return ss.str();
    }
    for (auto &[k, v] : value) {
        ss << fmt::format("{:<{}}{}: {}\n", ' ', nSpaces, k, fieldFormatter(FWD(v), 0));
    }
    return ss.str();
}

template<bool SingleLineParameter>
inline void fieldParser(const std::string_view &data, MapLike auto &map) {
    map.clear();
    if constexpr (SingleLineParameter) { // single-line JSON-style map
        if (!data.starts_with('{') || !data.ends_with('}')) {
            throw ProtocolException("error parsing map from string '{}' -- missing brackets '{{}}' for single-line representation", data);
        }
        std::string_view parse = data.substr(1, data.size() - 2);
        std::string_view element;
        while (!(element = stringTokenizer(parse, ',')).empty()) {
            const auto colonPosition = element.find_first_of(':');
            if (colonPosition == std::string_view::npos) {
                throw ProtocolException("error parsing map element {} from string '{}' -- missing colon ':?", element, data);
            }
            if (colonPosition < element.size() - 1 && element[colonPosition + 1] != ' ') {
                throw ProtocolException("error parsing map element {} from string '{}' -- missing space after colon ':?", element, data);
            }
            const auto key   = element.substr(0, colonPosition);
            const auto value = element.substr(colonPosition + 2);
            fieldParser<true>(value, FWD(map[std::string{ key }]));
        }
        return;
    }
    std::string_view parse = data.substr(data.find_first_of('\n') + 1);
    std::string_view element;
    while (!(element = stringTokenizer(parse, '\n')).empty()) {
        if (element.starts_with('#') || element.starts_with('\n')) {
            break; // skip commented or empty lines
        }

        const auto colonPosition = element.find_first_of(':');
        if (colonPosition == std::string_view::npos) {
            throw ProtocolException("error parsing map element {} from string '{}' -- missing colon ':?", element, data);
        }
        if (colonPosition < element.size() - 1 && element[colonPosition + 1] != ' ') {
            throw ProtocolException("error parsing map element {} from string '{}' -- missing space after colon ':?", element, data);
        }
        const auto key   = element.substr(0, colonPosition);
        const auto value = element.substr(colonPosition + 2);
        fieldParser<true>(value, FWD(map[std::string{ key }]));
    }
}

} // namespace yaml::detail

template<>
inline void putHeaderInfo<YAML>(IoBuffer &buffer) {
    using namespace std::string_view_literals;
    constexpr auto WITHOUT = opencmw::IoBuffer::MetaInfo::WITHOUT;
    buffer.put<WITHOUT>("---\n"sv);
}

template<>
inline DeserialiserInfo checkHeaderInfo<YAML>(IoBuffer &buffer, DeserialiserInfo info, const ProtocolCheck check) {
    const auto position = buffer.position();
    const auto line     = yaml::detail::peekLine(buffer);
    if ((line.size() < 3) || buffer.at<char>(position) != '-' || buffer.at<char>(position + 1) != '-' || buffer.at<char>(position + 2) != '-') {
        if (check == ProtocolCheck::LENIENT) {
            // info.exceptions.emplace_back(fmt::format_string<std::size_t, std::size_t, std::size_t>("YAML: buffer too small or missing (`---') line: '{}' pos: {}  size: {}"), line, position, buffer.size());
            info.exceptions.push_back(ProtocolException{ "YAML: buffer too small or missing (`---') line: '{}' pos: {}  size: {}", line, position, buffer.size() });
        }
        if (check == ProtocolCheck::ALWAYS) {
            throw ProtocolException("YAML: buffer too small or missing (`---') line: '{}' pos: {}  size: {}", line, position, buffer.size());
        }
    }
    buffer.skip<false>(3);
    [[maybe_unused]] const auto nWhitespaces = static_cast<std::size_t>(yaml::detail::consumeWhitespace(buffer));
    [[maybe_unused]] const auto nTillEnd     = static_cast<std::size_t>(yaml::detail::moveToEndOfLine(buffer));
    if (nTillEnd > 1 || buffer.at<uint8_t>(buffer.position() - 1) != '\n') {
        if (check == ProtocolCheck::LENIENT) {
            // info.exceptions.emplace_back("YAML: non-white-space characters after (`---') line: '{}' pos: {}  size: {}", line, position, buffer.size());
            info.exceptions.push_back(ProtocolException{ "YAML: non-white-space characters after (`---') line: '{}' pos: {}  size: {}", line, position, buffer.size() });
        }
        if (check == ProtocolCheck::ALWAYS) {
            throw ProtocolException("YAML: non-white-space characters after (`---') line: '{}' pos: {}  size: {}", line, position, buffer.size());
        }
    }
    return info;
}

template<>
struct FieldHeaderWriter<YAML> {
    template<const bool writeMetaInfo, typename DataType>
    constexpr std::size_t static put(IoBuffer &buffer, FieldDescription auto &field, const DataType &data) noexcept {
        using namespace std::string_view_literals;
        using namespace yaml::detail;
        constexpr auto WITHOUT = opencmw::IoBuffer::MetaInfo::WITHOUT;

        if constexpr (std::is_same_v<DataType, START_MARKER>) {
            addSpace(buffer, field.hierarchyDepth);
            if (field.fieldName.size() > 0) {
                buffer.put<WITHOUT>(field.fieldName);
                buffer.put<WITHOUT>(":\n"sv);
            }
            return 0; // dataSizePosition
        }
        if constexpr (std::is_same_v<DataType, END_MARKER>) {
            addSpace(buffer, field.hierarchyDepth);
            buffer.put<WITHOUT>(fmt::format("# end - {}\n", field.fieldName));
            if (field.hierarchyDepth == 0) {
                buffer.put<WITHOUT>("...\n"sv);
            }
            return 0; // dataSizePosition
        }

        addSpace(buffer, field.hierarchyDepth);
        buffer.put<WITHOUT>(field.fieldName);
        buffer.put<WITHOUT>(": "sv);

        IoSerialiser<YAML, DataType>::serialise(buffer, field, data);
        return 0; // dataSizePosition
    }
};

template<typename DataType>
struct IoSerialiser<YAML, DataType> { // catch all template -> dispatched to fieldFormatter() and fieldParser()
    static constexpr uint8_t getDataTypeId() { return 0; }

    constexpr static void    serialise(IoBuffer &buffer, FieldDescription auto &&field, const DataType &rawValue) noexcept {
        using StrippedDataType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(rawValue)))>;
        using yaml::detail::getMetaPosition;

        constexpr auto unwrap        = [](auto &&raw) noexcept { return FWD(getAnnotatedMember(unwrapPointer(FWD(raw)))); };
        const auto     rawFormat     = yaml::detail::fieldFormatter(unwrap(FWD(rawValue)), 0);
        const auto     startPosition = yaml::detail::getIndentation(field.hierarchyDepth, static_cast<int32_t>(field.fieldName.size() + sizeof(": ")));
        if (const int spaceUntilMeta = getMetaPosition() - (startPosition + static_cast<int32_t>(rawFormat.size())); spaceUntilMeta > 0) {
            if constexpr (is_annotated<DataType>) {
                const auto unit        = rawValue.getUnit().empty() ? "" : fmt::format(" - [{}]", rawValue.getUnit());
                const auto description = rawValue.getDescription().empty() ? "" : fmt::format(" - {}", rawValue.getDescription());
                buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(fmt::format("{}{:<{}}# {}\t{}{}\n", rawFormat, ' ', spaceUntilMeta, typeName<StrippedDataType>, unit, description));
                return;
            }
            buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(fmt::format("{}{:<{}}# {}\n", rawFormat, ' ', spaceUntilMeta, typeName<StrippedDataType>));
            return;
        }
        // need line-break to not collide with the meta information
        const auto minMetaSpacing   = std::max(getMetaPosition() - startPosition, 5);
        const auto lineBrokenFormat = yaml::detail::fieldFormatter(unwrap(rawValue), yaml::detail::getIndentation(field.hierarchyDepth + 1));
        if constexpr (is_annotated<DataType>) {
            const auto unit        = rawValue.getUnit().empty() ? "" : fmt::format(" - [{}]", rawValue.getUnit());
            const auto description = rawValue.getDescription().empty() ? "" : fmt::format(" - {}", rawValue.getDescription());
            buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(fmt::format("{:<{}}# {}\t{}{}\n{}", ' ', minMetaSpacing, typeName<StrippedDataType>, unit, description, lineBrokenFormat));
            return;
        }
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(fmt::format("{:<{}}# {}\n{}", ' ', minMetaSpacing, typeName<StrippedDataType>, lineBrokenFormat));
    }
    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const &field, DataType &value) {
        if (const auto shortData = yaml::detail::parseNonWhiteSpaceData(buffer); !shortData.empty()) {
            try {
                yaml::detail::fieldParser<true>(shortData, FWD(value));
            } catch (const ProtocolException &exception) {
                throw ProtocolException("YAML: could not deserialise single-line field {} {} - reason: {}", typeName<DataType>, field.fieldName, shortData, exception.what());
            }
            buffer.set_position(field.dataEndPosition);
            return;
        }
        const auto startData = field.dataStartPosition;
        const auto size      = field.dataEndPosition - field.dataStartPosition;
        try {
            yaml::detail::fieldParser<false>(buffer.asString(startData, static_cast<int32_t>(size - 1)), FWD(value));
        } catch (const ProtocolException &exception) {
            throw ProtocolException("YAML: could not deserialise multi-line field {} {} - reason: {}", typeName<DataType>, field.fieldName, buffer.asString(startData, static_cast<int32_t>(size - 1)), exception.what());
        }
        buffer.set_position(field.dataEndPosition);
    }
};

template<>
struct IoSerialiser<YAML, START_MARKER> {
    static constexpr uint8_t getDataTypeId() { return 1; }
    constexpr static void    serialise(const IoBuffer    &/*buffer*/, FieldDescription auto const    &/*field*/, const START_MARKER &) noexcept { /* handled in field header parser */
    }
    constexpr static void deserialise(const IoBuffer & /*buffer*/, FieldDescription auto & /*field*/, const START_MARKER & /*value*/) noexcept { /* handled in field header parser */
    }
};

template<>
struct IoSerialiser<YAML, END_MARKER> {
    static constexpr uint8_t getDataTypeId() { return 2; }
    constexpr static void    serialise(const IoBuffer    &/*buffer*/, FieldDescription auto const    &/*field*/, const END_MARKER &) noexcept { /* handled in field header parser */
    }
    constexpr static void deserialise(const IoBuffer & /*buffer*/, FieldDescription auto & /*field*/, const END_MARKER &) noexcept { /* handled in field header parser */
    }
};

template<>
struct FieldHeaderReader<YAML> {
    template<ProtocolCheck check>
    constexpr static void get(IoBuffer &buffer, const DeserialiserInfo & /*info*/, FieldDescription auto &result) {
        using namespace yaml::detail;
        while (true) {
            const auto line = peekLine(buffer);
            if (const auto leftStrippedLine = line.substr(line.find_first_not_of(' ')); !leftStrippedLine.starts_with('#') && !leftStrippedLine.starts_with('\n')) {
                break;
            }
            [[maybe_unused]] const auto n = moveToEndOfLine(buffer); // skip commented or empty lines
        }
        result.headerStart     = buffer.position();
        const auto indentation = consumeWhitespace(buffer);
        if (indentation % IOSERIALISER_YAML_INDENTATION) {
            throw ProtocolException("YAML: non canonical indentation depth {} at buffer position {}", indentation, buffer.position());
        }
        const auto newHierarchyDepth = static_cast<uint8_t>(indentation / IOSERIALISER_YAML_INDENTATION);

        result.fieldName             = readKey(buffer);
        if (buffer.position() == buffer.size() || result.fieldName == "...") { // reached end-of-file
            result.intDataType = IoSerialiser<YAML, END_MARKER>::getDataTypeId();
            return;
        }
        result.intDataType       = 0;
        result.dataStartPosition = buffer.position();
        if (result.fieldName.size() != 0 && buffer.at<char8_t>(buffer.position()) == '\n') { // start marker
            result.intDataType       = IoSerialiser<YAML, START_MARKER>::getDataTypeId();
            result.dataStartPosition = buffer.position() + static_cast<std::size_t>(moveToEndOfLine(buffer));
            result.hierarchyDepth    = newHierarchyDepth;
        }

        if (result.fieldName.size() != 0 && newHierarchyDepth < result.hierarchyDepth) { // end marker
            result.intDataType       = IoSerialiser<YAML, END_MARKER>::getDataTypeId();
            result.dataStartPosition = result.headerStart;
            result.dataEndPosition   = result.headerStart;
            return;
        }
        result.hierarchyDepth = newHierarchyDepth;

        // check for next key at the same or higher hierarchy level
        do {
            [[maybe_unused]] const auto n = moveToEndOfLine(buffer);
            result.dataEndPosition        = buffer.position();
        } while (consumeWhitespace(buffer) > indentation);
        buffer.set_position(result.dataEndPosition);
    }
};

} // namespace opencmw

#endif // OPENCMW_CPP_IOSERIALISERYAML_HPP
