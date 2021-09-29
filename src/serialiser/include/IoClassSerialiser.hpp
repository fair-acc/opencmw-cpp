#ifndef OPENCMW_IOCLASSSERIALISER_H
#define OPENCMW_IOCLASSSERIALISER_H
#include "IoSerialiser.hpp"

namespace opencmw {

enum ProtocolCheck {
    IGNORE,  // null return type
    LENIENT, // via return type
    ALWAYS   // via ProtocolException
};

template<SerialiserProtocol protocol, const bool writeMetaInfo = true, ReflectableClass T>
constexpr void serialise(IoBuffer &buffer, const T &value, const uint8_t hierarchyDepth = 0) {
    for_each(refl::reflect(value).members, [&](const auto member, [[maybe_unused]] const auto index) {
        if constexpr (is_field(member) && !is_static(member)) {
            using UnwrappedMemberType = std::remove_reference_t<decltype(member(value))>;
            using MemberType          = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;
            if constexpr (isReflectableClass<MemberType>()) {
                if constexpr (is_smart_pointer<std::remove_reference_t<UnwrappedMemberType>>) {
                    if (!member(value)) {
                        return;
                    } // return from lambda
                }
                std::size_t posSizePositionStart = opencmw::putFieldHeader<protocol, writeMetaInfo>(buffer, member.name.str(), START_MARKER_INST);
                std::size_t posStartDataStart    = buffer.size() - sizeof(uint8_t);                                               // '-1 because we wrote one byte as marker payload
                serialise<protocol, writeMetaInfo>(buffer, getAnnotatedMember(unwrapPointer(member(value))), hierarchyDepth + 1); // do not inspect annotation itself

                opencmw::putFieldHeader<protocol, writeMetaInfo>(buffer, member.name.str(), END_MARKER_INST);
                buffer.at<int32_t>(posSizePositionStart) = static_cast<int32_t>(buffer.size() - posStartDataStart); // write data size
            } else {
                if constexpr (is_smart_pointer<std::remove_reference_t<UnwrappedMemberType>>) {
                    if (member(value)) {
                        opencmw::putFieldHeader<protocol, writeMetaInfo>(buffer, member.name.str(), member(value));
                        return;
                    }
                    // else -- skip empty smart pointer
                    return;
                }
                opencmw::putFieldHeader<protocol, writeMetaInfo>(buffer, member.name.str(), member(value));
            }
        }
    });
}

template<typename T>
std::unordered_map<std::string_view, int32_t> createMemberMap() {
    std::unordered_map<std::string_view, int32_t> m;
    refl::util::for_each(refl::reflect<T>().members, [&m](auto field, auto index) {
        m.insert({ field.name.c_str(), index });
    });
    return m;
}

template<typename T>
constexpr auto createMemberMap2() noexcept {
    constexpr size_t                                        size = refl::reflect<T>().members.size;
    constexpr ConstExprMap<std::string_view, int32_t, size> m    = { refl::util::map_to_array<std::pair<std::string_view, int32_t>>(refl::reflect<T>().members, [](auto field, auto index) {
        return std::pair<std::string_view, int32_t>(field.name.c_str(), index);
    }) };
    return m;
}

template<typename T>
int32_t findMemberIndex(const std::string_view fieldName) {
    //static const std::unordered_map<std::string_view, int32_t> m = createMemberMap<T>(); // TODO: consider replacing this by ConstExprMap (array list-based)
    static constexpr auto m = createMemberMap2<T>(); //alt: array-based implementation
    return m.at(fieldName);
}

template<SerialiserProtocol protocol, const ProtocolCheck protocolCheckVariant, ReflectableClass T>
constexpr DeserialiserInfo deserialise(IoBuffer &buffer, T &value, DeserialiserInfo info = DeserialiserInfo(), const std::string &structName = "root", const uint8_t hierarchyDepth = 0) {
    // todo: replace structName string by const_string
    if constexpr (protocolCheckVariant != IGNORE) {
        if (info.setFields.contains(structName)) {
            std::fill(info.setFields[structName].begin(), info.setFields[structName].end(), false);
        } else {
            info.setFields[structName] = std::vector<bool>(refl::reflect<T>().members.size);
        }
    }

    while (buffer.position() < buffer.size()) {
        using String                  = std::string_view;
        const std::size_t headerStart = buffer.position();
        const auto        intDataType = buffer.get<uint8_t>(); // data type ID

        //const auto        hashFieldName     =
        buffer.get<int32_t>(); // hashed field name -> future: faster look-up/matching of fields
        const auto        dataStartOffset   = static_cast<uint64_t>(buffer.get<int32_t>());
        const auto        dataSize          = static_cast<uint64_t>(buffer.get<int32_t>());
        const String      fieldName         = buffer.get<std::string_view>(); // full field name
        const std::size_t dataStartPosition = headerStart + dataStartOffset;
        const std::size_t dataEndPosition   = headerStart + dataStartOffset + dataSize;
        // the following information is optional
        // e.g. could skip to 'headerStart + dataStartOffset' and start reading the data, or
        // e.g. could skip to 'headerStart + dataStartOffset + dataSize' and start reading the next field header

        constexpr bool ignoreChecks = protocolCheckVariant == IGNORE;
        const String   unit         = ignoreChecks || (buffer.position() == dataStartPosition) ? "" : buffer.get<String>();
        const String   description  = ignoreChecks || (buffer.position() == dataStartPosition) ? "" : buffer.get<String>();
        //ignoreChecks || (buffer.position() == dataStartPosition) ? "" : buffer.get<String>();
        const ExternalModifier modifier = ignoreChecks || (buffer.position() == dataStartPosition) ? RW : get_ext_modifier(buffer.get<uint8_t>());
        // std::cout << fmt::format("parsed field {:<20} meta data: [{}] {} dir: {}\n", fieldName, unit, description, modifier);

        // skip to data start
        buffer.set_position(dataStartPosition);

        if (intDataType == IoSerialiser<protocol, END_MARKER>::getDataTypeId()) {
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
        }

        int32_t searchIndex = -1;
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
