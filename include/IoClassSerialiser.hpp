#ifndef OPENCMW_IOCLASSSERIALISER_H
#define OPENCMW_IOCLASSSERIALISER_H
#include <IoSerialiser.hpp>

namespace opencmw {

template<SerialiserProtocol protocol, ReflectableClass T>
constexpr void serialise(IoBuffer &buffer, const T &value, const bool writeMetaInfo = true, const uint8_t hierarchyDepth = 0) {
    for_each(refl::reflect(value).members, [&](const auto member, [[maybe_unused]] const auto index) {
        using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(member(value)))>;
        if constexpr (is_field(member) && !is_static(member)) {
            if constexpr (isReflectableClass<MemberType>()) {
                std::size_t posSizePositionStart = opencmw::putFieldHeader<protocol>(buffer, member.name.str(), START_MARKER_INST);
                std::size_t posStartDataStart    = buffer.size() - sizeof(uint8_t);                                // '-1 because we wrote one byte as marker payload
                serialise<protocol>(buffer, getAnnotatedMember(member(value)), writeMetaInfo, hierarchyDepth + 1); // do not inspect annotation itself

                opencmw::putFieldHeader<protocol>(buffer, member.name.str(), END_MARKER_INST);
                buffer.at<int32_t>(posSizePositionStart) = static_cast<int32_t>(buffer.size() - posStartDataStart); // write data size
            } else {
                opencmw::putFieldHeader<protocol>(buffer, member.name.str(), member(value));
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
    constexpr size_t size = refl::reflect<T>().members.size;
    constexpr ConstExprMap<std::string_view, int32_t, size> m = {refl::util::map_to_array<std::pair<std::string_view, int32_t>>(refl::reflect<T>().members, [](auto field, auto index) {
        return std::pair<std::string_view, int32_t>(field.name.c_str(), index);
    })};
    return m;
}

template<typename T>
int32_t findMemberIndex(const std::string_view fieldName) {
    //static const std::unordered_map<std::string_view, int32_t> m = createMemberMap<T>(); // TODO: consider replacing this by ConstExprMap (array list-based)
    static constexpr auto m = createMemberMap2<T>(); //alt: array-based implementation
    return m.at(fieldName);
}

template<SerialiserProtocol protocol, ReflectableClass T>
constexpr void deserialise(IoBuffer &buffer, T &value, const bool readMetaInfo = true, const uint8_t hierarchyDepth = 0) {
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

        if (readMetaInfo && buffer.position() != dataStartPosition) {
            // TODO: see what to do with this info
            // const String unit        = (buffer.position() == dataStartPosition) ? "" : buffer.get<String>();
            // const String description = (buffer.position() == dataStartPosition) ? "" : buffer.get<String>();
            // const String direction   = (buffer.position() == dataStartPosition) ? "" : buffer.get<String>();
            //std::cout << fmt::format("parsed field {:<20} meta data: [{}] {} dir: {}\n", fieldName, unit, description, direction);
        } else {
            //std::cout << fmt::format("parsed field {:<20} - {}\n", fieldName, hashFieldName);
        }
        // skip to data start
        buffer.position() = dataStartPosition;

        if (intDataType == IoSerialiser<protocol, END_MARKER>::getDataTypeId()) {
            // reached end of sub-structure
            try {
                IoSerialiser<protocol, END_MARKER>::deserialise(buffer, fieldName, END_MARKER_INST);
            } catch (...) { // protocol exception
                buffer.position() = dataEndPosition;
                continue;
            }
            buffer.position() = dataEndPosition;
            continue;
        }

        int32_t searchIndex = -1;
        try {
            searchIndex = static_cast<int32_t>(findMemberIndex<T>(fieldName));
        } catch (std::out_of_range &e) {
            //TODO: convert to a an appropriate protocol exception
            std::cout << "caught std::out_of_range for field: " << fieldName << " index = " << searchIndex << " msg " << e.what() << std::endl;
            buffer.position() = dataEndPosition;
            continue;
        }

        if (intDataType == IoSerialiser<protocol, START_MARKER>::getDataTypeId()) {
            // reached start of sub-structure -> dive in
            try {
                IoSerialiser<protocol, START_MARKER>::deserialise(buffer, fieldName, START_MARKER_INST);
            } catch (...) { // protocol exception
                buffer.position() = dataEndPosition;
                continue;
            }

            for_each(refl::reflect<T>().members, [searchIndex, &buffer, &value, &hierarchyDepth, &readMetaInfo](auto member, int32_t index) {
                using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(member(value)))>;
                if constexpr (isReflectableClass<MemberType>()) {
                    if (index == searchIndex) {
                        deserialise<protocol>(buffer, member(value), readMetaInfo, hierarchyDepth + 1);
                    }
                }
            });

            buffer.position() = dataEndPosition;
            continue;
        }

        for_each(refl::reflect<T>().members, [&buffer, &value, &intDataType, &searchIndex, &fieldName](auto member, int32_t index) {
            using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(member(value)))>;
            if constexpr (!isReflectableClass<MemberType>() && is_writable(member) && !is_static(member)) {
                if (IoSerialiser<protocol, MemberType>::getDataTypeId() == intDataType && index == searchIndex) { //TODO: protocol exception for mismatching data-type?
                    IoSerialiser<protocol, MemberType>::deserialise(buffer, fieldName, getAnnotatedMember(member(value)));
                }
            }
        });

        // skip to data end
        buffer.position() = dataEndPosition;
    }
    if (hierarchyDepth == 0 && buffer.position() != buffer.size()) {
        // TODO: convert into protocol exception - reached end of buffer with unparsed data and/or read beyond buffer end
        std::cerr << "serialise class type " << typeName<T>() << " hierarchyDepth = " << static_cast<int>(hierarchyDepth) << '\n';
        std::cout << fmt::format("protocol exception for class type {}: position {} vs. size {}\n", typeName<T>(), buffer.position(), buffer.size());
    }
}

} // namespace opencmw

#endif //OPENCMW_IOCLASSSERIALISER_H
