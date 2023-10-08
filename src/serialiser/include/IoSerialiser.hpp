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

enum class ProtocolCheck {
    IGNORE,  // null return type
    LENIENT, // via return type
    ALWAYS   // via ProtocolException
};

class ProtocolException : private std::exception {
    const std::string errorMsg;

public:
    explicit ProtocolException(std::string errorMessage) noexcept
        : errorMsg(std::move(errorMessage)) {}
    explicit ProtocolException(const char *errorMessage) noexcept
        : errorMsg(errorMessage) {}
    template<typename... ErrorArgs>
    explicit ProtocolException(fmt::format_string<ErrorArgs...> fmt, ErrorArgs &&...errorArgs) noexcept
        : errorMsg(fmt::format(fmt, std::forward<ErrorArgs>(errorArgs)...)) {}

    [[nodiscard]] const char *what() const noexcept override { return errorMsg.data(); }
};

// clang-format off
inline std::string what(const std::exception_ptr &eptr = std::current_exception()) {
    if (!eptr) { throw std::bad_exception(); }
    try { std::rethrow_exception(eptr);
    } catch (const ProtocolException &e) { return std::string{ e.what() };
    } catch (const std::exception &e) { return e.what();
    } catch (const std::string &e) { return e;
    } catch (const char *e) { return e;
    } catch (...) { return "unknown exception"; } // NOLINT
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
    int16_t          subfields         = 0;
    std::string_view fieldName;
    uint8_t          intDataType    = 0U;
    uint8_t          hierarchyDepth = 0;
};

struct FieldDescriptionLong {
    uint64_t         headerStart;
    std::size_t      dataStartPosition;
    std::size_t      dataEndPosition;
    int16_t          subfields = 0;
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
    v.subfields;
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
struct FieldHeaderWriter { // struct needed due to partial specialisation
    template<const bool writeMetaInfo, typename DataType>
    constexpr std::size_t static put(IoBuffer & /*buffer*/, FieldDescription auto && /*field*/, const DataType & /*data*/) { return 0; }
};

template<SerialiserProtocol protocol>
inline DeserialiserInfo checkHeaderInfo(IoBuffer & /*buffer*/, DeserialiserInfo info, const ProtocolCheck /*protocolCheckVariant*/) { return info; }

template<SerialiserProtocol protocol, typename T>
struct IoSerialiser {
    constexpr static uint8_t getDataTypeId() { return 0xFF; } // default value

    constexpr static void    serialise(IoBuffer    &/*buffer*/, FieldDescription auto const &field, const T &value) {
        throw ProtocolException("not implemented IoSerialiser<{}>::serialise(IoBuffer&, field: '{}', type '{}' value: '{}')", protocol::protocolName(), field.fieldName, typeName<T>, value);
    }

    constexpr static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const &field, T &value) {
        throw ProtocolException("not implemented IoSerialiser<{}>::deserialise(IoBuffer&, field: '{}', type '{}' value: '{}')", protocol::protocolName(), field.fieldName, typeName<T>, value);
    }
};

template<ReflectableClass T>
forceinline int32_t findMemberIndex(const std::string_view &fieldName) noexcept {
    static constexpr ConstExprMap<std::string_view, int32_t, refl::reflect<T>().members.size> m{ refl::util::map_to_array<std::pair<std::string_view, int32_t>>(refl::reflect<T>().members, [](auto field, auto index) {
        return std::pair<std::string_view, int32_t>(field.name.c_str(), index);
    }) };
    return m.at(fieldName, -1);
}

namespace detail {

template<ReflectableClass T>
constexpr int16_t getNumberOfNonNullSubfields(const T &value) {
    int16_t fields = 0;
    for_each(refl::reflect(value).members, [&](const auto member, [[maybe_unused]] const auto index) {
        if constexpr (is_field(member) && !is_static(member)) {
            if constexpr (is_smart_pointer<std::remove_reference_t<decltype(member(value))>>) {
                if (!member(value)) {
                    return; // skip empty smart pointers
                }
            }
            fields++;
        }
    });
    return fields;
}

template<SerialiserProtocol protocol, const bool writeMetaInfo = true, typename DataType>
constexpr auto newFieldHeader(const IoBuffer &buffer, const char *fieldName, const int hierarchyDepth, const DataType &value, const int16_t subfields) {
    constexpr int typeID = IoSerialiser<protocol, std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(value)))>>::getDataTypeId();
    const auto    pos    = buffer.size();
    if constexpr (writeMetaInfo && is_annotated<DataType>) {
        return FieldDescriptionLong{ .headerStart = pos, .dataStartPosition = pos, .dataEndPosition = pos, .subfields = subfields, .fieldName = fieldName, .unit = value.getUnit(), .description = value.getDescription(), .modifier = value.getModifier(), .intDataType = typeID, .hierarchyDepth = static_cast<uint8_t>(hierarchyDepth) };
    } else if constexpr (writeMetaInfo) {
        return FieldDescriptionLong{ .headerStart = pos, .dataStartPosition = pos, .dataEndPosition = pos, .subfields = subfields, .fieldName = fieldName, .unit = "", .description = "", .modifier = RW, .intDataType = typeID, .hierarchyDepth = static_cast<uint8_t>(hierarchyDepth) };
    } else {
        return FieldDescriptionShort{ .headerStart = pos, .dataStartPosition = pos, .dataEndPosition = pos, .subfields = subfields, .fieldName = fieldName, .intDataType = typeID, .hierarchyDepth = static_cast<uint8_t>(hierarchyDepth) };
    }
}

template<SerialiserProtocol protocol, bool writeMetaInfo = true>
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
                const auto            subfields            = getNumberOfNonNullSubfields(getAnnotatedMember(unwrapPointer(fieldValue)));
                FieldDescription auto field                = newFieldHeader<protocol, writeMetaInfo>(buffer, member.name.c_str(), parent.hierarchyDepth + 1, FWD(fieldValue), subfields);
                const std::size_t     posSizePositionStart = FieldHeaderWriter<protocol>::template put<writeMetaInfo>(buffer, field, START_MARKER_INST);
                const std::size_t     posStartDataStart    = buffer.size();
                serialise<protocol, writeMetaInfo>(buffer, getAnnotatedMember(unwrapPointer(fieldValue)), field); // do not inspect annotation itself
                FieldHeaderWriter<protocol>::template put<writeMetaInfo>(buffer, field, END_MARKER_INST);
                updateSize<protocol>(buffer, posSizePositionStart, posStartDataStart);
                return;
            } else { // field is a (possibly annotated) primitive type
                FieldDescription auto field = newFieldHeader<protocol, writeMetaInfo>(buffer, member.name.c_str(), parent.hierarchyDepth + 1, fieldValue, 0);
                FieldHeaderWriter<protocol>::template put<writeMetaInfo>(buffer, field, fieldValue);
            }
        }
    });
}
} // namespace detail

template<SerialiserProtocol protocol, const bool writeMetaInfo = true>
constexpr void serialise(IoBuffer &buffer, ReflectableClass auto const &value) {
    putHeaderInfo<protocol>(buffer);
    const auto        subfields            = detail::getNumberOfNonNullSubfields(value);
    auto              field                = detail::newFieldHeader<protocol, writeMetaInfo>(buffer, refl::reflect(value).name.c_str(), 0, value, subfields);
    const std::size_t posSizePositionStart = FieldHeaderWriter<protocol>::template put<writeMetaInfo>(buffer, field, START_MARKER_INST);
    const std::size_t posStartDataStart    = buffer.size();
    detail::serialise<protocol, writeMetaInfo>(buffer, value, field);
    FieldHeaderWriter<protocol>::template put<writeMetaInfo>(buffer, field, END_MARKER_INST);
    updateSize<protocol>(buffer, posSizePositionStart, posStartDataStart);
}

template<SerialiserProtocol protocol>
struct FieldHeaderReader {
    template<ProtocolCheck check>
    inline static void get(IoBuffer & /*buffer*/, DeserialiserInfo & /*info*/, FieldDescriptionLong & /*result*/) {}
};

namespace detail {

forceinline void moveToFieldEndBufferPosition(IoBuffer &buffer, const FieldDescriptionLong &field) {
    if (field.dataEndPosition != std::numeric_limits<size_t>::max()) {
        buffer.set_position(field.dataEndPosition);
    }
}

template<ProtocolCheck check, typename... ErrorArgs>
neverinline constexpr void handleDeserialisationError(DeserialiserInfo &info, fmt::format_string<ErrorArgs...> errorFormat, ErrorArgs &&...errorArgs) noexcept(check != ProtocolCheck::ALWAYS) {
    const auto text = fmt::format(errorFormat, std::forward<ErrorArgs>(errorArgs)...);
    if constexpr (check == ProtocolCheck::ALWAYS) {
        throw ProtocolException(text);
    }
    info.exceptions.emplace_back(ProtocolException(text));
}

template<ProtocolCheck check, typename... ErrorArgs>
neverinline constexpr bool handleDeserialisationErrorAndSkipToNextField(IoBuffer &buffer, const FieldDescriptionLong &field, DeserialiserInfo &info, fmt::format_string<ErrorArgs...> errorFormat, ErrorArgs &&...errorArgs) noexcept(check != ProtocolCheck::ALWAYS) {
    moveToFieldEndBufferPosition(buffer, field);
    if constexpr (check == ProtocolCheck::IGNORE) {
        return true; // should return into outer context
    }
    handleDeserialisationError<check>(info, errorFormat, std::forward<ErrorArgs>(errorArgs)...);
    return false; // may continue
}

template<SerialiserProtocol protocol, const ProtocolCheck check>
constexpr void deserialise(IoBuffer &buffer, ReflectableClass auto &value, DeserialiserInfo &info, const FieldDescriptionLong parent) {
    using ValueType = std::remove_reference_t<decltype(value)>;
    // initialize bitfield indicating which fields have been set
    if constexpr (check != ProtocolCheck::IGNORE) {
        if (info.setFields.contains(parent.fieldName)) {
            std::fill(info.setFields[parent.fieldName].begin(), info.setFields[parent.fieldName].end(), false);
        } else {
            info.setFields[parent.fieldName] = std::vector<bool>(refl::reflect<ValueType>().members.size);
        }
    }

    // read initial field header
    auto field = newFieldHeader<protocol, true>(buffer, "", parent.hierarchyDepth, value, -1);
    FieldHeaderReader<protocol>::template get<check>(buffer, info, field);
    if (field.hierarchyDepth == 0 && field.fieldName != typeName<ValueType> && !field.fieldName.empty() && check != ProtocolCheck::IGNORE) { // check if root type is matching
        handleDeserialisationError<check>(info, "IoSerialiser<{}, {}>::deserialise: data is not of excepted type but of type {}", protocol::protocolName(), typeName<ValueType>, field.fieldName);
    }
    try {
        IoSerialiser<protocol, START_MARKER>::deserialise(buffer, field, START_MARKER_INST);
    } catch (...) {
        if (handleDeserialisationErrorAndSkipToNextField<check>(buffer, field, info,
                    "IoSerialiser<{}, START_MARKER>::deserialise(buffer, {}::{}, START_MARKER_INST): position {} vs. size {} -- exception: {}",
                    protocol::protocolName(), parent.fieldName, field.fieldName, buffer.position(), buffer.size(), what())) {
            return;
        }
    }
    buffer.set_position(field.dataStartPosition); // skip to data start

    while (buffer.position() < buffer.size()) {
        auto previousSubFields = field.subfields;
        FieldHeaderReader<protocol>::template get<check>(buffer, info, field);
        buffer.set_position(field.dataStartPosition); // skip to data start

        if (field.intDataType == IoSerialiser<protocol, END_MARKER>::getDataTypeId()) { // reached end of sub-structure
            try {
                IoSerialiser<protocol, END_MARKER>::deserialise(buffer, field, END_MARKER_INST);
            } catch (...) {
                if (handleDeserialisationErrorAndSkipToNextField<check>(buffer, field, info, "IoSerialiser<{}, END_MARKER>::deserialise(buffer, {}::{}, END_MARKER_INST): position {} vs. size {} -- exception: {}",
                            protocol::protocolName(), parent.fieldName, field.fieldName, buffer.position(), buffer.size(), what())) {
                    continue;
                }
            }
            return; // step down to previous hierarchy depth
        }

        const int fieldIndex = findMemberIndex<ValueType>(field.fieldName); // indexed position within struct
        if (fieldIndex < 0) {
            if constexpr (check != ProtocolCheck::IGNORE) {
                handleDeserialisationError<check>(info, "missing field (type:{}) {}::{} at buffer[{}, size:{}]", field.intDataType, parent.fieldName, field.fieldName, buffer.position(), buffer.size());
                info.additionalFields.emplace_back(std::make_tuple(fmt::format("{}::{}", parent.fieldName, field.fieldName), field.intDataType));
            }
            if (field.dataEndPosition != std::numeric_limits<size_t>::max()) {
                moveToFieldEndBufferPosition(buffer, field);
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

        for_each(refl::reflect<ValueType>().members, [&](auto member, int32_t memberIndex) {
            if constexpr (!is_field(member) || is_static(member)) return;
            if (memberIndex != fieldIndex) return; // fieldName does not match -- skip to next field

            using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;
            if constexpr (!is_writable(member) || is_static(member)) {
                moveToFieldEndBufferPosition(buffer, field);
                return;
            } else if constexpr (isReflectableClass<MemberType>()) {
                field.intDataType = IoSerialiser<protocol, START_MARKER>::getDataTypeId();
            } else {
                constexpr int requestedType = IoSerialiser<protocol, MemberType>::getDataTypeId();
                if (requestedType != field.intDataType) { // mismatching data-type
                    moveToFieldEndBufferPosition(buffer, field);
                    if constexpr (check == ProtocolCheck::IGNORE) {
                        return; // don't write -> skip to next
                    }
                    handleDeserialisationError<check>(info, "mismatched field type for {}::{} - requested type: {} (typeID: {}) got: {}", member.declarator.name, member.name, typeName<MemberType>, requestedType, field.intDataType);
                    return;
                }
                constexpr bool isAnnotated = is_annotated<std::remove_reference_t<decltype(unwrapPointer(member(value)))>>;
                if constexpr (isAnnotated && check != ProtocolCheck::IGNORE) {       // check for Annotation mismatch
                    if (is_deprecated(unwrapPointer(member(value)).getModifier())) { // warn for deprecated access
                        handleDeserialisationError<check>(info, "deprecated field access for {}::{} - description: {}", member.declarator.name, member.name, unwrapPointer(member(value)).getDescription());
                    }
                    if (is_private(unwrapPointer(member(value)).getModifier())) { // warn for private access
                        handleDeserialisationError<check>(info, "private/internal field access for {}::{} - description: {}", member.declarator.name, member.name, unwrapPointer(member(value)).getDescription());
                    }
                    if (std::string_view(unwrapPointer(member(value)).getUnit()).compare(field.unit)) { // error on unit mismatch
                        handleDeserialisationError<check>(info, "mismatched field unit for {}::{} - requested unit '{}' received '{}'", member.declarator.name, member.name, unwrapPointer(member(value)).getUnit(), field.unit);
                        moveToFieldEndBufferPosition(buffer, field);
                        return;
                    }
                    if (is_readonly(unwrapPointer(member(value)).getModifier())) { // should not set field via external reference
                        handleDeserialisationError<check>(info, "mismatched field access modifier for {}::{} - requested '{}' received '{}'", member.declarator.name, member.name, (unwrapPointer(member(value)).getModifier()), field.modifier);
                        moveToFieldEndBufferPosition(buffer, field);
                        return;
                    }
                }
                IoSerialiser<protocol, MemberType>::deserialise(buffer, field, getAnnotatedMember(unwrapPointerCreateIfAbsent(member(value))));
                if constexpr (check != ProtocolCheck::IGNORE) {
                    info.setFields[parent.fieldName][static_cast<uint64_t>(fieldIndex)] = true;
                }
            }
        });

        if (field.intDataType == IoSerialiser<protocol, START_MARKER>::getDataTypeId()) {
            // reached start of sub-structure -> dive in
            for_each(refl::reflect<ValueType>().members, [&](auto member, int32_t index) {
                if (index != fieldIndex) {
                    return; // fieldName does not match -- skip to next field
                }
                using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;
                if constexpr (isReflectableClass<MemberType>()) {
                    buffer.set_position(field.headerStart); // reset buffer position for the nested deserialiser to read again
                    field.hierarchyDepth++;
                    field.fieldName = member.name.c_str(); // N.B. needed since member.name is referring to compile-time const string
                    deserialise<protocol, check>(buffer, unwrapPointerCreateIfAbsent(member(value)), info, field);
                    field.hierarchyDepth--;
                    field.subfields = previousSubFields - 1;
                    if constexpr (check != ProtocolCheck::IGNORE) {
                        info.setFields[parent.fieldName][static_cast<uint64_t>(fieldIndex)] = true;
                    }
                }
            });
        }

        // skip to data end if field header defines the end
        if (field.dataEndPosition != std::numeric_limits<size_t>::max() && field.dataEndPosition != buffer.position()) {
            if (check != ProtocolCheck::IGNORE) {
                handleDeserialisationError<check>(info, "field reader for field {} did not consume until the end ({}) of the field, buffer position: {}", field.fieldName, field.dataEndPosition, buffer.position());
            }
            moveToFieldEndBufferPosition(buffer, field);
        }
    }
    // check that full buffer is read. // todo: this check should be removed to allow consecutive storage of multiple objects in one buffer
    if (field.hierarchyDepth == 0 && buffer.position() != buffer.size()) {
        if constexpr (check == ProtocolCheck::IGNORE) {
            return;
        }
        handleDeserialisationError<check>(info, "protocol exception for class type {}({}): position {} vs. size {}", typeName<ValueType>, field.hierarchyDepth, buffer.position(), buffer.size());
    }
}
constexpr const char *ROOT_NAME = "root";
} // namespace detail

template<SerialiserProtocol protocol, const ProtocolCheck check>
DeserialiserInfo deserialise(IoBuffer &buffer, ReflectableClass auto &value, DeserialiserInfo info = DeserialiserInfo()) {
    // check data header for protocol version match
    info = checkHeaderInfo<protocol>(buffer, info, check);
    if (check == ProtocolCheck::LENIENT && !info.exceptions.empty()) {
        return info; // do not attempt to deserialise data with wrong header
    }
    FieldDescription auto fieldDescription = detail::newFieldHeader<protocol, true>(buffer, detail::ROOT_NAME, 0, value, -1);
    detail::deserialise<protocol, check>(buffer, value, info, fieldDescription);
    return info;
}

inline std::ostream &operator<<(std::ostream &os, const DeserialiserInfo &info) {
    os << typeName<DeserialiserInfo> << "\nset fields:\n";
    if (!info.setFields.empty()) {
        for (auto fieldMask : info.setFields) {
            os << "   class '" << fieldMask.first << "' bit field: " << fieldMask.second << '\n';
        }
    }
    if (!info.additionalFields.empty()) {
        os << "additional fields:\n";
        for (auto e : info.additionalFields) {
            os << "    field name: " << std::get<0>(e) << " typeID: " << std::get<1>(e) << '\n';
        }
    }
    if (!info.exceptions.empty()) {
        os << "thrown exceptions:\n";
        int count = 0;
        for (auto e : info.exceptions) {
            os << "    " << (count++) << ": " << e << '\n';
        }
    }
    return os;
}

} // namespace opencmw

template<>
struct fmt::formatter<opencmw::ProtocolCheck> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin(); // not (yet) implemented
    }

    template<typename FormatContext>
    auto format(opencmw::ProtocolCheck const &v, FormatContext &ctx) const {
        using enum opencmw::ProtocolCheck;
        switch (v) {
        case IGNORE:
            return fmt::format_to(ctx.out(), "IGNORE");
        case LENIENT:
            return fmt::format_to(ctx.out(), "LENIENT");
        case ALWAYS:
            return fmt::format_to(ctx.out(), "ALWAYS");
        default:
            return ctx.out();
        }
    }
};
#pragma clang diagnostic pop
#endif // OPENCMW_IOSERIALISER_H
