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
// TODO: allow declaration of reflection within name-space
ENABLE_REFLECTION_FOR(opencmw::DeserialiserInfo, setFields, additionalFields, exceptions)

#pragma clang diagnostic pop
#endif //OPENCMW_IOSERIALISER_H
