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

struct START_MARKER {};
struct END_MARKER {};
struct OTHER {};

constexpr static START_MARKER START_MARKER_INST;
constexpr static END_MARKER   END_MARKER_INST;
constexpr static OTHER        OTHER_INST;

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

// clang-format off
inline std::string what(const std::exception_ptr &eptr = std::current_exception()) {
    if (!eptr) { throw std::bad_exception(); }
    try { std::rethrow_exception(eptr);
    } catch (const ProtocolException &e) { return std::string{ e.what() };
    } catch (const std::exception &e) { return e.what();
    } catch (const std::string &e) { return e;
    } catch (const char *e) { return e;
    } catch (...) { return "unknown exception"; }
}
// clang-format on

inline std::ostream &operator<<(std::ostream &os, const ProtocolException &exception) {
    return os << "ProtocolException(\"" << exception.what() << "\")";
}

struct DeserialiserInfo {
    std::map<std::string_view, std::vector<bool>> setFields; // N.B. non-owning string_view references since sources are compile-time strings (refl-cpp)
    std::list<std::tuple<std::string, int>>       additionalFields;
    std::list<ProtocolException>                  exceptions;
};
} // namespace opencmw
ENABLE_REFLECTION_FOR(opencmw::DeserialiserInfo, setFields, additionalFields, exceptions) // TODO: allow declaration of reflection within name-space

namespace opencmw {

template<basic_fixed_string protocol>
struct Protocol {
    constexpr static const char *protocolName() { return protocol.data_; }
};

template<typename T>
concept SerialiserProtocol = requires { T::protocolName(); }; // TODO: find neater check via is_protocol

struct FieldDescriptionShort {
    uint64_t         headerStart       = 0U;
    std::size_t      dataStartPosition = 0U;
    std::size_t      dataEndPosition   = 0U;
    std::string_view fieldName;
    uint8_t          intDataType    = 0U;
    uint8_t          hierarchyDepth = 0;
};

struct FieldDescriptionLong {
    uint64_t         headerStart;
    std::size_t      dataStartPosition;
    std::size_t      dataEndPosition;
    std::string_view fieldName;
    std::string_view unit;
    std::string_view description;
    ExternalModifier modifier;
    uint8_t          intDataType;
    uint8_t          hierarchyDepth;
};

template<typename T>
concept FieldDescription = requires(T v) {
    v.headerStart;
    v.dataStartPosition;
    v.dataEndPosition;
    v.fieldName;
    v.intDataType;
    v.hierarchyDepth;
};

// using ClassField           = std::string_view; // as a place-holder for the reflected type info

/// generic protocol definition -> should throw exception when used in production code
template<SerialiserProtocol protocol>
inline void updateSize(IoBuffer & /*buffer*/, const size_t /*posSizePositionStart*/, const size_t /*posStartDataStart*/) {}

template<SerialiserProtocol protocol>
inline void putHeaderInfo(IoBuffer & /*buffer*/) {}

template<SerialiserProtocol protocol>
struct FieldHeaderWriter { // todo: remove struct and rename to putField
    template<const bool writeMetaInfo, typename DataType>
    constexpr std::size_t static put(IoBuffer & /*buffer*/, FieldDescription auto && /*field*/, const DataType & /*data*/) { return 0; }
};

template<SerialiserProtocol protocol>
inline DeserialiserInfo checkHeaderInfo(IoBuffer & /*buffer*/, DeserialiserInfo info, const ProtocolCheck /*protocolCheckVariant*/) { return info; }

template<SerialiserProtocol protocol, typename T>
struct IoSerialiser {
    constexpr static uint8_t getDataTypeId() { return 0xFF; } // default value

    constexpr static void    serialise(IoBuffer    &/*buffer*/, FieldDescription auto const &field, const T &value) noexcept {
        std::cout << fmt::format("{:<4} - serialise-generic: {} {} value: {} - constexpr?: {}\n",
                   protocol::protocolName(), typeName<T>, field.fieldName, value,
                   std::is_constant_evaluated());
    }

    constexpr static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const &field, T &value) noexcept {
        std::cout << fmt::format("{:<4} - deserialise-generic: {} {} value: {} - constexpr?: {}\n",
                protocol::protocolName(), typeName<T>, field.fieldName, value,
                std::is_constant_evaluated());
    }
};

template<ReflectableClass T>
forceinline int32_t findMemberIndex(const std::string_view &fieldName) noexcept {
    static constexpr auto m = ConstExprMap{ refl::util::map_to_array<std::pair<std::string_view, int32_t>>(refl::reflect<T>().members, [](auto field, auto index) {
        return std::pair<std::string_view, int32_t>(field.name.c_str(), index);
    }) };
    return m.at(fieldName, -1);
}

namespace detail {

template<SerialiserProtocol protocol, const bool writeMetaInfo = true, typename DataType>
constexpr auto newFieldHeader(const IoBuffer &buffer, const char *fieldName, const int hierarchyDepth = 0, const DataType &value = 0) {
    constexpr int typeID = IoSerialiser<protocol, std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(value)))>>::getDataTypeId();
    const auto    pos    = buffer.size();
    if constexpr (writeMetaInfo && is_annotated<DataType>) {
        return FieldDescriptionLong{ .headerStart = pos, .dataStartPosition = pos, .dataEndPosition = pos, .fieldName = fieldName, .unit = value.getUnit(), .description = value.getDescription(), .modifier = value.getModifier(), .intDataType = typeID, .hierarchyDepth = static_cast<uint8_t>(hierarchyDepth) };
    } else if constexpr (writeMetaInfo) {
        return FieldDescriptionLong{ .headerStart = pos, .dataStartPosition = pos, .dataEndPosition = pos, .fieldName = fieldName, .unit = "", .description = "", .modifier = RW, .intDataType = typeID, .hierarchyDepth = static_cast<uint8_t>(hierarchyDepth) };
    } else {
        return FieldDescriptionShort{ .headerStart = pos, .dataStartPosition = pos, .dataEndPosition = pos, .fieldName = fieldName, .intDataType = typeID, .hierarchyDepth = static_cast<uint8_t>(hierarchyDepth) };
    }
}

template<SerialiserProtocol protocol, const bool writeMetaInfo = true>
constexpr void serialise(IoBuffer &buffer, ReflectableClass auto const &value, FieldDescription auto const parent) {
    for_each(refl::reflect(value).members, [&](const auto member, [[maybe_unused]] const auto index) {
        if constexpr (is_field(member) && !is_static(member)) {
            auto &&fieldValue = member(value);
            if constexpr (is_smart_pointer<std::remove_reference_t<decltype(fieldValue)>>) {
                if (!fieldValue) {
                    return; // skip empty smart pointer
                }
            }

            using UnwrappedMemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(fieldValue)))>;
            if constexpr (isReflectableClass<UnwrappedMemberType>()) { // nested data-structure
                FieldDescription auto field                = newFieldHeader<protocol, writeMetaInfo>(buffer, member.name.c_str(), parent.hierarchyDepth + 1, fieldValue);
                const std::size_t     posSizePositionStart = FieldHeaderWriter<protocol>::template put<writeMetaInfo>(buffer, field, START_MARKER_INST);
                const std::size_t     posStartDataStart    = buffer.size();
                serialise<protocol, writeMetaInfo>(buffer, getAnnotatedMember(unwrapPointer(fieldValue)), field); // do not inspect annotation itself
                FieldHeaderWriter<protocol>::template put<writeMetaInfo>(buffer, field, END_MARKER_INST);
                updateSize<protocol>(buffer, posSizePositionStart, posStartDataStart);
                return;
            } else { // field is a (possibly annotated) primitive type
                FieldDescription auto field = newFieldHeader<protocol, writeMetaInfo>(buffer, member.name.c_str(), parent.hierarchyDepth + 1, fieldValue);
                FieldHeaderWriter<protocol>::template put<writeMetaInfo>(buffer, field, fieldValue);
            }
        }
    });
}
} // namespace detail

template<SerialiserProtocol protocol, const bool writeMetaInfo = true>
constexpr void serialise(IoBuffer &buffer, ReflectableClass auto const &value) {
    putHeaderInfo<protocol>(buffer);
    const auto       &reflectionData       = refl::reflect(value);
    auto              field                = detail::newFieldHeader<protocol, writeMetaInfo>(buffer, reflectionData.name.c_str(), 0, reflectionData);
    const std::size_t posSizePositionStart = FieldHeaderWriter<protocol>::template put<writeMetaInfo>(buffer, field, START_MARKER_INST);
    const std::size_t posStartDataStart    = buffer.size();
    detail::serialise<protocol, writeMetaInfo>(buffer, value, field);
    FieldHeaderWriter<protocol>::template put<writeMetaInfo>(buffer, field, END_MARKER_INST);
    updateSize<protocol>(buffer, posSizePositionStart, posStartDataStart);
}

template<SerialiserProtocol protocol>
struct FieldHeaderReader {
    template<ProtocolCheck protocolCheckVariant>
    inline static void get(IoBuffer & /*buffer*/, DeserialiserInfo & /*info*/, const ProtocolCheck & /*protocolCheckVariant*/, FieldDescriptionLong & /*result*/) {}
};

namespace detail {

template<ProtocolCheck protocolCheckVariant>
neverinline constexpr void handleDeserialisationError(DeserialiserInfo &info, const char *errorFormat, const auto &...errorArgs) noexcept(protocolCheckVariant != ALWAYS) {
    const auto text = fmt::format(errorFormat, errorArgs...);
    if constexpr (protocolCheckVariant == ALWAYS) {
        throw ProtocolException(text);
    }
    info.exceptions.emplace_back(ProtocolException(text));
}

template<ProtocolCheck protocolCheckVariant>
neverinline constexpr bool handleDeserialisationErrorAndSkipToNextField(IoBuffer &buffer, const FieldDescriptionLong &field, DeserialiserInfo &info, const char *errorFormat, const auto &...errorArgs) noexcept(protocolCheckVariant != ALWAYS) {
    if constexpr (protocolCheckVariant == IGNORE) {
        if (field.dataEndPosition != std::numeric_limits<size_t>::max()) {
            buffer.set_position(field.dataEndPosition);
        }
        return true; // should return into outer context
    }
    handleDeserialisationError<protocolCheckVariant>(info, errorFormat, errorArgs...);
    if (field.dataEndPosition != std::numeric_limits<size_t>::max()) {
        buffer.set_position(field.dataEndPosition);
    }
    return false; // may continue
}

template<SerialiserProtocol protocol, const ProtocolCheck protocolCheckVariant>
constexpr void deserialise(IoBuffer &buffer, ReflectableClass auto &value, DeserialiserInfo &info, const FieldDescriptionLong parent) {
    using ValueType = std::remove_reference_t<decltype(value)>;
    // todo: replace structName string by const_string
    // initialize bitfield indicating which fields have been set
    if constexpr (protocolCheckVariant != IGNORE) {
        if (info.setFields.contains(parent.fieldName)) {
            std::fill(info.setFields[parent.fieldName].begin(), info.setFields[parent.fieldName].end(), false);
        } else {
            info.setFields[parent.fieldName] = std::vector<bool>(refl::reflect<ValueType>().members.size);
        }
    }

    // read initial field header
    auto field = newFieldHeader<protocol, true>(buffer, "", parent.hierarchyDepth, value);
    FieldHeaderReader<protocol>::template get<protocolCheckVariant>(buffer, info, field);
    if (field.hierarchyDepth == 0 && field.fieldName != typeName<ValueType> && !field.fieldName.empty() && protocolCheckVariant != IGNORE) { // check if root type is matching
        handleDeserialisationError<protocolCheckVariant>(info, "IoSerialiser<{}, {}>::deserialise: data is not of excepted type but of type {}", protocol::protocolName(), typeName<ValueType>, field.fieldName);
    }
    try {
        IoSerialiser<protocol, START_MARKER>::deserialise(buffer, field, START_MARKER_INST);
    } catch (...) {
        if (handleDeserialisationErrorAndSkipToNextField<protocolCheckVariant>(buffer, field, info,
                    "IoSerialiser<{}, START_MARKER>::deserialise(buffer, {}::{}, START_MARKER_INST): position {} vs. size {} -- exception: {}",
                    protocol::protocolName(), parent.fieldName, field.fieldName, buffer.position(), buffer.size(), what())) {
            return;
        }
    }
    buffer.set_position(field.dataStartPosition); // skip to data start

    while (buffer.position() < buffer.size()) {
        FieldHeaderReader<protocol>::template get<protocolCheckVariant>(buffer, info, field);
        buffer.set_position(field.dataStartPosition); // skip to data start

        if (field.intDataType == IoSerialiser<protocol, END_MARKER>::getDataTypeId()) { // reached end of sub-structure
            try {
                IoSerialiser<protocol, END_MARKER>::deserialise(buffer, field, END_MARKER_INST);
            } catch (...) {
                if (handleDeserialisationErrorAndSkipToNextField<protocolCheckVariant>(buffer, field, info, "IoSerialiser<{}, END_MARKER>::deserialise(buffer, {}::{}, END_MARKER_INST): position {} vs. size {} -- exception: {}",
                            protocol::protocolName(), parent.fieldName, field.fieldName, buffer.position(), buffer.size(), what())) {
                    continue;
                }
            }
            return; // step down to previous hierarchy depth
        }

        const int fieldIndex = findMemberIndex<ValueType>(field.fieldName); // indexed position within struct
        if (fieldIndex < 0) {
            if constexpr (protocolCheckVariant != IGNORE) {
                handleDeserialisationError<protocolCheckVariant>(info, "missing field (type:{}) {}::{} at buffer[{}, size:{}]", field.intDataType, parent.fieldName, field.fieldName, buffer.position(), buffer.size());
                info.additionalFields.emplace_back(std::make_tuple(fmt::format("{}::{}", parent.fieldName, field.fieldName), field.intDataType));
            }
            if (field.dataEndPosition != std::numeric_limits<size_t>::max()) {
                buffer.set_position(field.dataEndPosition);
            } else { // use deserialise OTHER to continue parsing the data just to skip it for formats which do not include the field size in the header
                if constexpr (requires { IoSerialiser<protocol, OTHER>::deserialise(buffer, field, OTHER_INST); }) {
                    if (field.intDataType == IoSerialiser<protocol, START_MARKER>::getDataTypeId()) {
                        buffer.set_position(field.dataStartPosition - 1); // reset buffer position for the nested deserialiser to read again
                    }
                    IoSerialiser<protocol, OTHER>::deserialise(buffer, field, OTHER_INST);
                }
            }
            continue;
        }

        if (field.intDataType == IoSerialiser<protocol, START_MARKER>::getDataTypeId()) {
            buffer.set_position(field.headerStart); // reset buffer position for the nested deserialiser to read again
            // reached start of sub-structure -> dive in
            for_each(refl::reflect<ValueType>().members, [&fieldIndex, &buffer, &value, &info, &parent, &field](auto member, int32_t index) {
                if (index != fieldIndex) {
                    return; // fieldName does not match -- skip to next field
                }
                using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;
                if constexpr (isReflectableClass<MemberType>()) {
                    field.hierarchyDepth++;
                    field.fieldName = member.name.c_str(); // N.B. needed since member.name is referring to compile-time const string
                    deserialise<protocol, protocolCheckVariant>(buffer, unwrapPointerCreateIfAbsent(member(value)), info, field);
                    field.hierarchyDepth--;
                    if constexpr (protocolCheckVariant != IGNORE) {
                        info.setFields[parent.fieldName][static_cast<uint64_t>(fieldIndex)] = true;
                    }
                }
            });
        }

        for_each(refl::reflect<ValueType>().members, [&fieldIndex, &buffer, &value, &parent, &field, &info](auto member, int32_t memberIndex) {
            if constexpr (!is_field(member) || is_static(member)) return;
            if (memberIndex != fieldIndex) return; // fieldName does not match -- skip to next field

            using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;
            if constexpr (isReflectableClass<MemberType>() || !is_writable(member) || is_static(member)) {
                if (field.dataEndPosition != std::numeric_limits<size_t>::max()) {
                    buffer.set_position(field.dataEndPosition);
                }
                return;
            } else {
                constexpr int requestedType = IoSerialiser<protocol, MemberType>::getDataTypeId();
                if (requestedType != field.intDataType) { // mismatching data-type
                    if (field.dataEndPosition != std::numeric_limits<size_t>::max()) {
                        buffer.set_position(field.dataEndPosition);
                    }
                    if constexpr (protocolCheckVariant == IGNORE) {
                        return; // don't write -> skip to next
                    }
                    handleDeserialisationError<protocolCheckVariant>(info, "mismatched field type for {}::{} - requested type: {} (typeID: {}) got: {}", member.declarator.name, member.name, typeName<MemberType>, requestedType, field.intDataType);
                    return;
                }
                constexpr bool isAnnotated = is_annotated<std::remove_reference_t<decltype(unwrapPointer(member(value)))>>;
                if constexpr (isAnnotated && protocolCheckVariant != IGNORE) {       // check for Annotation mismatch
                    if (is_deprecated(unwrapPointer(member(value)).getModifier())) { // warn for deprecated access
                        handleDeserialisationError<protocolCheckVariant>(info, "deprecated field access for {}::{} - description: {}", member.declarator.name, member.name, unwrapPointer(member(value)).getDescription());
                    }
                    if (is_private(unwrapPointer(member(value)).getModifier())) { // warn for private access
                        handleDeserialisationError<protocolCheckVariant>(info, "private/internal field access for {}::{} - description: {}", member.declarator.name, member.name, unwrapPointer(member(value)).getDescription());
                    }
                    if (unwrapPointer(member(value)).getUnit().compare(field.unit)) { // error on unit mismatch
                        handleDeserialisationError<protocolCheckVariant>(info, "mismatched field unit for {}::{} - requested unit '{}' received '{}'", member.declarator.name, member.name, unwrapPointer(member(value)).getUnit(), field.unit);
                        if (field.dataEndPosition != std::numeric_limits<size_t>::max()) {
                            buffer.set_position(field.dataEndPosition);
                        }
                        return;
                    }
                    if (is_readonly((unwrapPointer(member(value)).getModifier()))) { // should not set field via external reference
                        handleDeserialisationError<protocolCheckVariant>(info, "mismatched field access modifier for {}::{} - requested '{}' received '{}'", member.declarator.name, member.name, (unwrapPointer(member(value)).getModifier()), field.modifier);
                        if (field.dataEndPosition != std::numeric_limits<size_t>::max()) {
                            buffer.set_position(field.dataEndPosition);
                        }
                        return;
                    }
                }
                IoSerialiser<protocol, MemberType>::deserialise(buffer, field, getAnnotatedMember(unwrapPointerCreateIfAbsent(member(value))));
                if constexpr (protocolCheckVariant != IGNORE) {
                    info.setFields[parent.fieldName][static_cast<uint64_t>(fieldIndex)] = true;
                }
            }
        });

        // skip to data end if field header defines the end
        if (field.dataEndPosition != std::numeric_limits<size_t>::max() && field.dataEndPosition != buffer.position()) {
            if (protocolCheckVariant != IGNORE) {
                handleDeserialisationError<protocolCheckVariant>(info, "field reader for field {} did not consume until the end ({}) of the field, buffer position: {}", field.fieldName, field.dataEndPosition, buffer.position());
            }
            buffer.set_position(field.dataEndPosition);
        }
    }
    // check that full buffer is read. // todo: this check should be removed to allow consecutive storage of multiple objects in one buffer
    if (field.hierarchyDepth == 0 && buffer.position() != buffer.size()) {
        if constexpr (protocolCheckVariant == IGNORE) {
            return;
        }
        handleDeserialisationError<protocolCheckVariant>(info, "protocol exception for class type {}({}): position {} vs. size {}", typeName<ValueType>, field.hierarchyDepth, buffer.position(), buffer.size());
    }
}
constexpr const char *ROOT_NAME = "root";
} // namespace detail

template<SerialiserProtocol protocol, const ProtocolCheck protocolCheckVariant>
constexpr DeserialiserInfo deserialise(IoBuffer &buffer, ReflectableClass auto &value, DeserialiserInfo info = DeserialiserInfo()) {
    // check data header for protocol version match
    info = checkHeaderInfo<protocol>(buffer, info, protocolCheckVariant);
    if (protocolCheckVariant == LENIENT && !info.exceptions.empty()) {
        return info; // do not attempt to deserialise data with wrong header
    }
    FieldDescription auto fieldDescription = detail::newFieldHeader<protocol, true>(buffer, detail::ROOT_NAME, 0, value);
    detail::deserialise<protocol, protocolCheckVariant>(buffer, value, info, fieldDescription);
    return info;
}

} // namespace opencmw

#pragma clang diagnostic pop
#endif // OPENCMW_IOSERIALISER_H
