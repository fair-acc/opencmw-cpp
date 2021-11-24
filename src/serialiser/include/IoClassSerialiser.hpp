#ifndef OPENCMW_IOCLASSSERIALISER_H
#define OPENCMW_IOCLASSSERIALISER_H
#include "IoSerialiser.hpp"

namespace opencmw {

struct FieldDescription {
    uint64_t         headerStart;
    uint8_t          intDataType;
    uint64_t         hash;
    uint64_t         dataStartOffset;
    uint64_t         dataSize;
    std::string_view fieldName;
    std::size_t      dataStartPosition;
    std::size_t      dataEndPosition;
    std::string_view unit;
    std::string_view description;
    ExternalModifier modifier;
};

/**
 * Reflection helper, calls the given lambda expresion
 * @tparam T Type parameter for the object
 * @tparam Callable Type parameter for the callable
 * @param fieldName The name of the field to call the callable with
 * @param callable A lambda expression refl::member, index -> void which will be called for field with the given name
 */
// TODO move to refl, change function name corresponding to ranges api
template<ReflectableClass T, typename Callable>
void call_on_field(const std::string_view fieldName, const Callable &callable) {
    const auto searchIndex = static_cast<int32_t>(findMemberIndex<T>(fieldName));
    for_each(refl::reflect<T>().members, [&callable, &searchIndex](auto member, int32_t index) {
        if (index != searchIndex) {
            return; // fieldName does not match -- skip to next field
        }
        callable(std::forward<decltype(member) &>(member), searchIndex);
    });
}

/**
 * Helper function which handles an error depending on the selected type of error handling
 * @tparam protocolCheckVariant Type of error handling
 * @param info object which will be updated with error information
 * @param formatString the format string for the error message
 * @param arguments arguments for use in the format string
 */
inline constexpr void handleError(const ProtocolCheck protocolCheckVariant, DeserialiserInfo &info, const char *formatString, const auto &...arguments) {
    const auto text = fmt::format(formatString, arguments...);
    if (protocolCheckVariant == ALWAYS) {
        throw ProtocolException(text);
    }
    info.exceptions.emplace_back(ProtocolException(text));
}

template<ReflectableClass T>
std::unordered_map<std::string_view, int32_t> createMemberMap() {
    std::unordered_map<std::string_view, int32_t> m;
    refl::util::for_each(refl::reflect<T>().members, [&m](auto field, auto index) {
        m.insert({ field.name.c_str(), index });
    });
    return m;
}

template<ReflectableClass T>
constexpr auto createMemberMap2() noexcept {
    constexpr size_t                                        size = refl::reflect<T>().members.size;
    constexpr ConstExprMap<std::string_view, int32_t, size> m    = { refl::util::map_to_array<std::pair<std::string_view, int32_t>>(refl::reflect<T>().members, [](auto field, auto index) {
        return std::pair<std::string_view, int32_t>(field.name.c_str(), index);
    }) };
    return m;
}

template<ReflectableClass T>
int32_t findMemberIndex(const std::string_view fieldName) {
    //static const std::unordered_map<std::string_view, int32_t> m = createMemberMap<T>(); // TODO: consider replacing this by ConstExprMap (array list-based)
    static constexpr auto m = createMemberMap2<T>(); //alt: array-based implementation
    return m.at(fieldName);
}

/**
 * Serialises an object into an IoBuffer
 *
 * @tparam protocol The serialisation Type to use: YaS, Json, Yaml, CMW...
 * @tparam writeMetaInfo whether to write metadata(unit, description) if the chosen serialiser supports it
 * @tparam T The type of the serialised object
 * @param buffer An IoBuffer instance
 * @param value The object to be serialised
 */
template<SerialiserProtocol protocol, const bool writeMetaInfo = true, ReflectableClass T>
constexpr void serialise(IoBuffer &buffer, const T &value) {
    putHeaderInfo<protocol>(buffer);
    const refl::type_descriptor<T> &reflectionData       = refl::reflect(value);
    const auto                      type_name            = reflectionData.name.c_str();
    std::size_t                     posSizePositionStart = FieldHeader<protocol>::template putFieldHeader<writeMetaInfo>(buffer, type_name, reflectionData.name.size, START_MARKER_INST);
    std::size_t                     posStartDataStart    = buffer.size();
    serialise<protocol, writeMetaInfo>(buffer, value, 0);
    FieldHeader<protocol>::template putFieldHeader<writeMetaInfo>(buffer, type_name, reflectionData.name.size, END_MARKER_INST);
    updateSize<protocol>(buffer, posSizePositionStart, posStartDataStart);
}

/**
 * Helper function which serialises a sub-object at a given hierarchy depth
 *
 * @tparam protocol The serialisation Type to use: YaS, Json, Yaml, CMW...
 * @tparam writeMetaInfo whether to write metadata(unit, description) if the chosen serialiser supports it
 * @tparam T The type of the serialised object
 * @param buffer An IoBuffer instance
 * @param value The object to be serialised
 * @param hierarchyDepth The level of nesting, zero is the root object
 */
template<SerialiserProtocol protocol, const bool writeMetaInfo = true, ReflectableClass T>
constexpr void serialise(IoBuffer &buffer, const T &value, const uint8_t hierarchyDepth) {
    for_each(refl::reflect(value).members, [&](const auto member, [[maybe_unused]] const auto index) {
        if constexpr (is_field(member) && !is_static(member)) {
            using UnwrappedMemberType = std::remove_reference_t<decltype(member(value))>;
            using MemberType          = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;
            if constexpr (is_smart_pointer<std::remove_reference_t<UnwrappedMemberType>>) {
                if (!member(value)) {
                    return; // skip empty smart pointer
                }
            }
            if constexpr (isReflectableClass<MemberType>()) { // nested data-structure
                std::size_t posSizePositionStart = FieldHeader<protocol>::template putFieldHeader<writeMetaInfo>(buffer, member.name.c_str(), member.name.size, START_MARKER_INST);
                std::size_t posStartDataStart    = buffer.size();
                serialise<protocol, writeMetaInfo>(buffer, getAnnotatedMember(unwrapPointer(member(value))), hierarchyDepth + 1); // do not inspect annotation itself
                FieldHeader<protocol>::template putFieldHeader<writeMetaInfo>(buffer, member.name.c_str(), member.name.size, END_MARKER_INST);
                updateSize<protocol>(buffer, posSizePositionStart, posStartDataStart);
            } else { // primitive type
                FieldHeader<protocol>::template putFieldHeader<writeMetaInfo>(buffer, member.name.c_str(), member.name.size, member(value));
            }
        }
    });
}

template<SerialiserProtocol protocol>
inline constexpr FieldDescription readFieldHeader(IoBuffer & /*buffer*/, DeserialiserInfo & /*info*/, const ProtocolCheck /*protocolCheckVariant*/) { return FieldDescription{}; }

template<>
inline FieldDescription readFieldHeader<YaS>(IoBuffer &buffer, DeserialiserInfo & /*info*/, const ProtocolCheck protocolCheckVariant) {
    using str_view = std::string_view;

    FieldDescription result;
    result.headerStart = buffer.position();
    result.intDataType = buffer.get<uint8_t>(); // data type ID
    //const auto        hashFieldName     =
    buffer.get<int32_t>(); // hashed field name -> future: faster look-up/matching of fields
    result.dataStartOffset   = static_cast<uint64_t>(buffer.get<int32_t>());
    result.dataSize          = static_cast<uint64_t>(buffer.get<int32_t>());
    result.fieldName         = buffer.get<std::string_view>(); // full field name
    result.dataStartPosition = result.headerStart + result.dataStartOffset;
    result.dataEndPosition   = result.headerStart + result.dataStartOffset + result.dataSize;
    // the following information is optional
    // e.g. could skip to 'headerStart + dataStartOffset' and start reading the data, or
    // e.g. could skip to 'headerStart + dataStartOffset + dataSize' and start reading the next field header

    bool ignoreChecks  = protocolCheckVariant == IGNORE; // not constexpr because protocolCheckVariant is not NTTP
    result.unit        = ignoreChecks || (buffer.position() == result.dataStartPosition) ? "" : buffer.get<str_view>();
    result.description = ignoreChecks || (buffer.position() == result.dataStartPosition) ? "" : buffer.get<str_view>();
    //ignoreChecks || (buffer.position() == dataStartPosition) ? "" : buffer.get<str_view>();
    result.modifier = ignoreChecks || (buffer.position() == result.dataStartPosition) ? RW : get_ext_modifier(buffer.get<uint8_t>());
    // std::cout << fmt::format("parsed field {:<20} meta data: [{}] {} dir: {}\n", fieldName, unit, description, modifier);
    return result;
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
inline bool isJsonWhitespace(char8_t &c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/**
 * Consume whitespace as defined by the JSON specification.
 * After calling this function the position of the passed buffer will point to the first non-whitespace character at/after the initial position.
 * @param buffer
 */
inline void consumeJsonWhitespace(IoBuffer &buffer) {
    while (isJsonWhitespace(buffer.at<char8_t>(buffer.position()))) {
        buffer.set_position(buffer.position() + 1);
    }
}

template<>
inline FieldDescription readFieldHeader<Json>(IoBuffer &buffer, DeserialiserInfo &info, const ProtocolCheck protocolCheckVariant) {
    FieldDescription result;
    result.headerStart = buffer.position();
    consumeJsonWhitespace(buffer);
    if (buffer.at<char8_t>(buffer.position()) == '{') { // start marker
        result.intDataType = yas::getDataTypeId<START_MARKER>();
        // set rest of fields
        return result;
    }
    if (buffer.at<char8_t>(buffer.position()) == '}') { // end marker
        result.intDataType = yas::getDataTypeId<END_MARKER>();
        // set rest of fields
        return result;
    }
    if (buffer.at<char8_t>(buffer.position()) == '"') { // string
        result.fieldName = readJsonString(buffer);
        if (result.fieldName.size() == 0) {
            handleError(protocolCheckVariant, info, "Cannot read field name for field at buffer position {}", buffer.position());
        }
        consumeJsonWhitespace(buffer);
        if (buffer.get<int8_t>() != ':') {
            std::cerr << "json malformed, no colon between key/value";
            // exception
        }
        consumeJsonWhitespace(buffer);
        // read value and set type ?
        result.intDataType       = yas::getDataTypeId<OTHER>(); // value is ignored anyway
        result.dataStartPosition = buffer.position();
        result.dataStartOffset   = result.dataStartPosition - result.headerStart;
        result.dataSize          = std::numeric_limits<size_t>::max(); // not defined for non-skipable data
        result.dataEndPosition   = std::numeric_limits<size_t>::max(); // not defined for non-skipable data
        // there is also no way to get unit, description and modifiers
        return result;
    }
    return result;
}

/**
 * Deserialise the contents of an IoBuffer into a given object
 * @tparam protocol The deserialisation Type to use: YaS, Json, Yaml, CMW...
 * @tparam protocolCheckVariant determines the error and logging behaviour of the deserialiser
 * @tparam T The type of the deserialised object
 * @param buffer An IoBuffer instance
 * @param value The object to be serialised
 * @param info Object which will get populated with information about the deserialisation
 * @return object containing info like read fields, additional fields and errors. Depends on the protocolCheck level
 */
template<SerialiserProtocol protocol, const ProtocolCheck protocolCheckVariant, ReflectableClass T>
constexpr DeserialiserInfo deserialise(IoBuffer &buffer, T &value, DeserialiserInfo info = DeserialiserInfo()) {
    // check data header for protocol version match
    info = checkHeaderInfo<protocol>(buffer, info, protocolCheckVariant);
    if (protocolCheckVariant == LENIENT && !info.exceptions.empty()) {
        return info; // do not attempt to deserialise data with wrong header
    }
    return deserialise<protocol, protocolCheckVariant>(buffer, value, info, "root", 0);
}

/**
 * Deserialise the object inside of an IoBuffer into an object.
 * Internal helper function for handling nested objects.
 * @tparam protocol The deserialisation Type to use: YaS, Json, Yaml, CMW...
 * @tparam protocolCheckVariant determines the error and logging behaviour of the deserialiser
 * @tparam T The type of the deserialised object
 * @param buffer An IoBuffer instance
 * @param value The object to be serialised
 * @param info Object which will get populated with information about the deserialisation
 * @param structName Name of the (sub) object
 * @param hierarchyDepth Hierarchy level of the (sub)object. Zero means the root object.
 * @return object containing info like read fields, additional fields and errors. Depends on the protocolCheck level
 */
template<SerialiserProtocol protocol, const ProtocolCheck protocolCheckVariant, ReflectableClass T>
constexpr DeserialiserInfo deserialise(IoBuffer &buffer, T &value, DeserialiserInfo info, const std::string &structName, const uint8_t hierarchyDepth) {
    // todo: replace structName string by const_string
    // initialize bitfield indicating which fields have been set
    if constexpr (protocolCheckVariant != IGNORE) {
        if (info.setFields.contains(structName)) {
            std::fill(info.setFields[structName].begin(), info.setFields[structName].end(), false);
        } else {
            info.setFields[structName] = std::vector<bool>(refl::reflect<T>().members.size);
        }
    }

    // read initial field header
    const FieldDescription startMarker = readFieldHeader<protocol>(buffer, info, protocolCheckVariant);
    if (hierarchyDepth == 0 && startMarker.fieldName != typeName<T> && protocolCheckVariant != IGNORE) { // check if root type is matching
        handleError(protocolCheckVariant, info, "IoSerialiser<{}, {}>::deserialise: data is not of excepted type but of type {}", protocol::protocolName(), typeName<T>, startMarker.fieldName);
    }
    try {
        IoSerialiser<protocol, START_MARKER>::deserialise(buffer, startMarker.fieldName, START_MARKER_INST);
    } catch (ProtocolException &exception) { // protocol exception
        if constexpr (protocolCheckVariant == IGNORE) {
            buffer.set_position(startMarker.dataEndPosition);
            return info;
        }
        handleError(protocolCheckVariant, info, "IoSerialiser<{}, START_MARKER>::deserialise(buffer, fieldName, START_MARKER_INST) exception for class {}: position {} vs. size {} -- exception thrown: {}",
                protocol::protocolName(), structName, buffer.position(), buffer.size(), exception.what());
        buffer.set_position(startMarker.dataEndPosition);
        return info;
    } catch (...) {
        if constexpr (protocolCheckVariant == IGNORE) {
            buffer.set_position(startMarker.dataEndPosition);
            return info;
        }
        throw ProtocolException(fmt::format("unknown exception in IoSerialiser<{}, START_MARKER>::deserialise(buffer, fieldName, START_MARKER_INST) for class {}: position {} vs. size {}",
                protocol::protocolName(), structName, buffer.position(), buffer.size()));
    }
    buffer.set_position(startMarker.dataStartPosition); // skip to data start

    while (buffer.position() < buffer.size()) {
        const FieldDescription field = readFieldHeader<protocol>(buffer, info, protocolCheckVariant);
        // skip to data start
        buffer.set_position(field.dataStartPosition);

        if (field.intDataType == IoSerialiser<protocol, END_MARKER>::getDataTypeId()) {
            // todo: assert name equals start marker
            // reached end of sub-structure
            try {
                IoSerialiser<protocol, END_MARKER>::deserialise(buffer, field.fieldName, END_MARKER_INST);
            } catch (ProtocolException &exception) { // protocol exception
                if constexpr (protocolCheckVariant == IGNORE) {
                    buffer.set_position(field.dataEndPosition);
                    continue;
                }
                const auto text = fmt::format("IoSerialiser<{}, END_MARKER>::deserialise(buffer, fieldName, END_MARKER_INST) exception for class {}: position {} vs. size {} -- exception thrown: {}",
                        protocol::protocolName(), structName, buffer.position(), buffer.size(), exception.what());
                if constexpr (protocolCheckVariant == ALWAYS) {
                    throw ProtocolException(text);
                }
                info.exceptions.emplace_back(ProtocolException(text));
                buffer.set_position(field.dataEndPosition);
                continue;
            } catch (...) {
                if constexpr (protocolCheckVariant == IGNORE) {
                    buffer.set_position(field.dataEndPosition);
                    continue;
                }
                throw ProtocolException(fmt::format("unknown exception in IoSerialiser<{}, START_MARKER>::deserialise(buffer, fieldName, START_MARKER_INST) for class {}: position {} vs. size {}",
                        protocol::protocolName(), structName, buffer.position(), buffer.size()));
            }
            return info; // step down to previous hierarchy depth
        }


        int32_t searchIndex = -1;
        try {
            searchIndex = static_cast<int32_t>(findMemberIndex<T>(field.fieldName));
        } catch (std::out_of_range &e) {
            if constexpr (protocolCheckVariant == IGNORE) {
                buffer.set_position(field.dataEndPosition);
                continue;
            }
            const auto exception = fmt::format("missing field (type:{}) {}::{} at buffer[{}, size:{}]",
                                               field.intDataType, structName, field.fieldName, buffer.position(), buffer.size());
            if constexpr (protocolCheckVariant == ALWAYS) {
                throw ProtocolException(exception);
            }
            info.exceptions.emplace_back(ProtocolException(exception));
            info.additionalFields.emplace_back(std::make_tuple(fmt::format("{}::{}", structName, field.fieldName), field.intDataType));
            buffer.set_position(field.dataEndPosition);
            continue;
        }
        if (field.intDataType == IoSerialiser<protocol, START_MARKER>::getDataTypeId()) {
            buffer.set_position(field.headerStart); // reset buffer position for the nested deserialiser to read again
            // reached start of sub-structure -> dive in
            call_on_field<T>(field.fieldName, [&buffer, &value, &hierarchyDepth, &info, &structName](auto member, int32_t index) {
                using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;
                if constexpr (isReflectableClass<MemberType>()) {
                    info = deserialise<protocol, protocolCheckVariant>(buffer, unwrapPointerCreateIfAbsent(member(value)), info, fmt::format("{}.{}", structName, get_display_name(member)), hierarchyDepth + 1);
                    if constexpr (protocolCheckVariant != IGNORE) {
                        info.setFields[structName][static_cast<uint64_t>(index)] = true;
                    }
                }
            });

            buffer.set_position(field.dataEndPosition);
            continue;
        }

        call_on_field<T>(field.fieldName, [&buffer, &value, &info, &field, &searchIndex, &structName](auto member, int32_t index) {
            using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;
            if constexpr (isReflectableClass<MemberType>() || !is_writable(member) || is_static(member)) {
                handleError(protocolCheckVariant, info, "field is not writeable or non-primitive: {}", member.name);
                return;
            } else {
                constexpr int requestedType = IoSerialiser<protocol, MemberType>::getDataTypeId();
                if (requestedType != field.intDataType) { // mismatching data-type
                    if constexpr (protocolCheckVariant == IGNORE) {
                        return; // don't write -> skip to next
                    }
                    handleError(protocolCheckVariant, info, "mismatched field type for {}::{} - requested type: {} (typeID: {}) got: {}", member.declarator.name, member.name, typeName<MemberType>, requestedType, field.intDataType);
                    return;
                }
                constexpr bool isAnnotated = is_annotated<std::remove_reference_t<decltype(unwrapPointer(member(value)))>>;
                if constexpr (isAnnotated && protocolCheckVariant != IGNORE) {       // check for Annotation mismatch
                    if (is_deprecated(unwrapPointer(member(value)).getModifier())) { // warn for deprecated access
                        handleError(protocolCheckVariant, info, "deprecated field access for {}::{} - description: {}", member.declarator.name, member.name, unwrapPointer(member(value)).getDescription());
                    }
                    if (is_private(unwrapPointer(member(value)).getModifier())) { // warn for private access
                        handleError(protocolCheckVariant, info, "private/internal field access for {}::{} - description: {}", member.declarator.name, member.name, unwrapPointer(member(value)).getDescription());
                    }
                    if (unwrapPointer(member(value)).getUnit().compare(field.unit)) { // error on unit mismatch
                        handleError(protocolCheckVariant, info, "mismatched field unit for {}::{} - requested unit '{}' received '{}'", member.declarator.name, member.name, unwrapPointer(member(value)).getUnit(), field.unit);
                        return;
                    }
                    if (is_readonly((unwrapPointer(member(value)).getModifier()))) { // should not set field via external reference
                        handleError(protocolCheckVariant, info, "mismatched field access modifier for {}::{} - requested '{}' received '{}'", member.declarator.name, member.name, (unwrapPointer(member(value)).getModifier()), field.modifier);
                        return;
                    }
                }
                IoSerialiser<protocol, MemberType>::deserialise(buffer, field.fieldName, getAnnotatedMember(unwrapPointerCreateIfAbsent(member(value))));
                if constexpr (protocolCheckVariant != IGNORE) {
                    info.setFields[structName][static_cast<uint64_t>(index)] = true;
                }
            }
        });
        // skip to data end
        buffer.set_position(field.dataEndPosition);
    }
    // check that full buffer is read. // todo: this check should be removed to allow consecutive storage of multiple objects in one buffer
    if (hierarchyDepth == 0 && buffer.position() != buffer.size()) {
        if constexpr (protocolCheckVariant == IGNORE) {
            return info;
        }
        std::cerr << "serialise class type " << typeName<T> << " hierarchyDepth = " << static_cast<int>(hierarchyDepth) << '\n';
        const auto exception = fmt::format("protocol exception for class type {}({}): position {} vs. size {}",
                typeName<T>, static_cast<int>(hierarchyDepth), buffer.position(), buffer.size());
        if constexpr (protocolCheckVariant == ALWAYS) {
            throw ProtocolException(exception);
        }
        std::cout << exception << std::endl; //TODO: replace std::cerr/cout by logger?
        info.exceptions.emplace_back(ProtocolException(exception));
        return info;
    }

    return info;
}

} // namespace opencmw

#endif //OPENCMW_IOCLASSSERIALISER_H
