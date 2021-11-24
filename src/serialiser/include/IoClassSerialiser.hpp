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
    std::size_t                     posSizePositionStart = FieldHeader<protocol>::putFieldHeader(buffer, type_name, reflectionData.name.size, START_MARKER_INST, writeMetaInfo);
    std::size_t                     posStartDataStart    = buffer.size();
    serialise<protocol, writeMetaInfo>(buffer, value, 0);
    FieldHeader<protocol>::putFieldHeader(buffer, type_name, reflectionData.name.size, END_MARKER_INST, writeMetaInfo);
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
                std::size_t posSizePositionStart = FieldHeader<protocol>::putFieldHeader(buffer, member.name.c_str(), member.name.size, START_MARKER_INST, writeMetaInfo);
                std::size_t posStartDataStart    = buffer.size();
                serialise<protocol, writeMetaInfo>(buffer, getAnnotatedMember(unwrapPointer(member(value))), hierarchyDepth + 1); // do not inspect annotation itself
                FieldHeader<protocol>::putFieldHeader(buffer, member.name.c_str(), member.name.size, END_MARKER_INST, writeMetaInfo);
                updateSize<protocol>(buffer, posSizePositionStart, posStartDataStart);
            } else { // primitive type
                FieldHeader<protocol>::putFieldHeader(buffer, member.name.c_str(), member.name.size, member(value), writeMetaInfo);
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

    while (buffer.position() < buffer.size()) {
        using str_view                = std::string_view;
        const std::size_t headerStart = buffer.position();
        const auto        intDataType = buffer.get<uint8_t>(); // data type ID

        //const auto        hashFieldName     =
        buffer.get<int32_t>(); // hashed field name -> future: faster look-up/matching of fields
        const auto        dataStartOffset   = static_cast<uint64_t>(buffer.get<int32_t>());
        const auto        dataSize          = static_cast<uint64_t>(buffer.get<int32_t>());
        const str_view    fieldName         = buffer.get<std::string_view>(); // full field name
        const std::size_t dataStartPosition = headerStart + dataStartOffset;
        const std::size_t dataEndPosition   = headerStart + dataStartOffset + dataSize;
        // the following information is optional
        // e.g. could skip to 'headerStart + dataStartOffset' and start reading the data, or
        // e.g. could skip to 'headerStart + dataStartOffset + dataSize' and start reading the next field header

        constexpr bool ignoreChecks = protocolCheckVariant == IGNORE;
        const str_view unit         = ignoreChecks || (buffer.position() == dataStartPosition) ? "" : buffer.get<str_view>();
        const str_view description  = ignoreChecks || (buffer.position() == dataStartPosition) ? "" : buffer.get<str_view>();
        //ignoreChecks || (buffer.position() == dataStartPosition) ? "" : buffer.get<str_view>();
        const ExternalModifier modifier = ignoreChecks || (buffer.position() == dataStartPosition) ? RW : get_ext_modifier(buffer.get<uint8_t>());
        // std::cout << fmt::format("parsed field {:<20} meta data: [{}] {} dir: {}\n", fieldName, unit, description, modifier);

        // skip to data start
        buffer.set_position(dataStartPosition);

        if (intDataType == IoSerialiser<protocol, END_MARKER>::getDataTypeId()) {
            // todo: assert name equals start marker
            // reached end of sub-structure
            try {
                IoSerialiser<protocol, END_MARKER>::deserialise(buffer, fieldName, END_MARKER_INST);
            } catch (ProtocolException &exception) { // protocol exception
                if constexpr (protocolCheckVariant == IGNORE) {
                    buffer.set_position(dataEndPosition);
                    continue;
                }
                const auto text = fmt::format("IoSerialiser<{}, END_MARKER>::deserialise(buffer, fieldName, END_MARKER_INST) exception for class {}: position {} vs. size {} -- exception thrown: {}",
                        protocol::protocolName(), structName, buffer.position(), buffer.size(), exception.what());
                if constexpr (protocolCheckVariant == ALWAYS) {
                    throw ProtocolException(text);
                }
                info.exceptions.emplace_back(ProtocolException(text));
                buffer.set_position(dataEndPosition);
                continue;
            } catch (...) {
                if constexpr (protocolCheckVariant == IGNORE) {
                    buffer.set_position(dataEndPosition);
                    continue;
                }
                throw ProtocolException(fmt::format("unknown exception in IoSerialiser<{}, START_MARKER>::deserialise(buffer, fieldName, START_MARKER_INST) for class {}: position {} vs. size {}",
                        protocol::protocolName(), structName, buffer.position(), buffer.size()));
            }
            return info; // step down to previous hierarchy depth
        }

        int32_t searchIndex = -1;
        if (hierarchyDepth != 0) { // do not resolve field name (== type name) for root element
            try {
                searchIndex = static_cast<int32_t>(findMemberIndex<T>(fieldName));
            } catch (std::out_of_range &e) {
                if constexpr (protocolCheckVariant == IGNORE) {
                    buffer.set_position(dataEndPosition);
                    continue;
                }
                const auto exception = fmt::format("missing field (type:{}) {}::{} at buffer[{}, size:{}]",
                        intDataType, structName, fieldName, buffer.position(), buffer.size());
                if constexpr (protocolCheckVariant == ALWAYS) {
                    throw ProtocolException(exception);
                }
                info.exceptions.emplace_back(ProtocolException(exception));
                info.additionalFields.emplace_back(std::make_tuple(fmt::format("{}::{}", structName, fieldName), intDataType));
                buffer.set_position(dataEndPosition);
                continue;
            }
        }

        if (intDataType == IoSerialiser<protocol, START_MARKER>::getDataTypeId()) {
            // reached start of sub-structure -> dive in
            try {
                IoSerialiser<protocol, START_MARKER>::deserialise(buffer, fieldName, START_MARKER_INST);
            } catch (ProtocolException &exception) { // protocol exception
                if constexpr (protocolCheckVariant == IGNORE) {
                    buffer.set_position(dataEndPosition);
                    continue;
                }
                const auto text = fmt::format("IoSerialiser<{}, START_MARKER>::deserialise(buffer, fieldName, START_MARKER_INST) exception for class {}: position {} vs. size {} -- exception thrown: {}",
                        protocol::protocolName(), structName, buffer.position(), buffer.size(), exception.what());
                if constexpr (protocolCheckVariant == ALWAYS) {
                    throw ProtocolException(text);
                }
                info.exceptions.emplace_back(ProtocolException(text));
                buffer.set_position(dataEndPosition);
                continue;
            } catch (...) {
                if constexpr (protocolCheckVariant == IGNORE) {
                    buffer.set_position(dataEndPosition);
                    continue;
                }
                throw ProtocolException(fmt::format("unknown exception in IoSerialiser<{}, START_MARKER>::deserialise(buffer, fieldName, START_MARKER_INST) for class {}: position {} vs. size {}",
                        protocol::protocolName(), structName, buffer.position(), buffer.size()));
            }

            if (searchIndex == -1 && hierarchyDepth == 0) {                       // top level element
                if (fieldName != typeName<T> && protocolCheckVariant != IGNORE) { // check if root type is matching
                    const auto text = fmt::format("IoSerialiser<{}, {}>::deserialise: data is not of excepted type but of type {}", protocol::protocolName(), typeName<T>, fieldName);
                    if constexpr (protocolCheckVariant == ALWAYS) {
                        throw ProtocolException(text);
                    }
                    info.exceptions.emplace_back(ProtocolException(text));
                }
                info = deserialise<protocol, protocolCheckVariant>(buffer, unwrapPointerCreateIfAbsent(value), info, structName, hierarchyDepth + 1);
                if constexpr (protocolCheckVariant != IGNORE) {
                    info.setFields[structName][static_cast<uint64_t>(0)] = true;
                }
            } else {
                for_each(refl::reflect<T>().members, [searchIndex, &buffer, &value, &hierarchyDepth, &info, &structName](auto member, int32_t index) {
                    using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;
                    if constexpr (isReflectableClass<MemberType>()) {
                        if (index == searchIndex) {
                            info = deserialise<protocol, protocolCheckVariant>(buffer, unwrapPointerCreateIfAbsent(member(value)), info, fmt::format("{}.{}", structName, get_display_name(member)), hierarchyDepth + 1);
                            if constexpr (protocolCheckVariant != IGNORE) {
                                info.setFields[structName][static_cast<uint64_t>(index)] = true;
                            }
                        }
                    }
                });
            }

            buffer.set_position(dataEndPosition);
            continue;
        }

#pragma clang diagnostic push
#pragma ide diagnostic   ignored "readability-function-cognitive-complexity"
        for_each(refl::reflect<T>().members, [&buffer, &value, &info, &intDataType, &fieldName, &description, &modifier, &searchIndex, &unit, &structName](auto member, int32_t index) {
            using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;

            if constexpr (!isReflectableClass<MemberType>() && is_writable(member) && !is_static(member)) {
                if (index != searchIndex) {
                    return; // fieldName does not match -- skip to next field
                }
                constexpr int requestedType = IoSerialiser<protocol, MemberType>::getDataTypeId();
                if (requestedType != intDataType) {
                    // mismatching data-type
                    if constexpr (protocolCheckVariant == IGNORE) {
                        return; // don't write -> skip to next
                    }
                    const auto error = fmt::format("mismatched field type for {}::{} - requested type: {} (typeID: {}) got: {}", member.declarator.name, member.name, typeName<MemberType>, requestedType, intDataType);
                    if constexpr (protocolCheckVariant == ALWAYS) {
                        throw ProtocolException(error);
                    }
                    info.exceptions.emplace_back(ProtocolException(error));
                    return;
                }

                constexpr bool isAnnotated = is_annotated<std::remove_reference_t<decltype(unwrapPointer(member(value)))>>;
                if constexpr (isAnnotated && protocolCheckVariant != IGNORE) {
                    // check for Annotation mismatch
                    if (is_deprecated(unwrapPointer(member(value)).getModifier())) {
                        // should not set field via external reference
                        const auto error = fmt::format("deprecated field access for {}::{} - description: {}", member.declarator.name, member.name, unwrapPointer(member(value)).getDescription());
                        if constexpr (protocolCheckVariant == ALWAYS) {
                            throw ProtocolException(error);
                        }
                        info.exceptions.emplace_back(ProtocolException(error));
                    }

                    if (is_private(unwrapPointer(member(value)).getModifier())) {
                        // should not set field via external reference
                        const auto error = fmt::format("private/internal field access for {}::{} - description: {}", member.declarator.name, member.name, unwrapPointer(member(value)).getDescription());
                        if constexpr (protocolCheckVariant == ALWAYS) {
                            throw ProtocolException(error);
                        }
                        info.exceptions.emplace_back(ProtocolException(error));
                    }

                    if (unwrapPointer(member(value)).getUnit().compare(unit)) {
                        const auto error = fmt::format("mismatched field unit for {}::{} - requested unit '{}' received '{}'", member.declarator.name, member.name, unwrapPointer(member(value)).getUnit(), unit);
                        if constexpr (protocolCheckVariant == ALWAYS) {
                            throw ProtocolException(error);
                        }
                        info.exceptions.emplace_back(ProtocolException(error));
                        return;
                    }

                    if (is_readonly((unwrapPointer(member(value)).getModifier()))) {
                        // should not set field via external reference
                        const auto error = fmt::format("mismatched field access modifier for {}::{} - requested '{}' received '{}'", member.declarator.name, member.name, (unwrapPointer(member(value)).getModifier()), modifier);
                        if constexpr (protocolCheckVariant == ALWAYS) {
                            throw ProtocolException(error);
                        }
                        info.exceptions.emplace_back(ProtocolException(error));
                        return;
                    }
                }

                IoSerialiser<protocol, MemberType>::deserialise(buffer, fieldName, getAnnotatedMember(unwrapPointerCreateIfAbsent(member(value))));
                if constexpr (protocolCheckVariant != IGNORE) {
                    info.setFields[structName][static_cast<uint64_t>(index)] = true;
                }
            }
        });
#pragma clang diagnostic pop
        // skip to data end
        buffer.set_position(dataEndPosition);
    }
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
