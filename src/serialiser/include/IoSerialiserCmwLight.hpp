#ifndef OPENCMW_CMWLIGHTSERIALISER_H
#define OPENCMW_CMWLIGHTSERIALISER_H

#include "IoSerialiser.hpp"
#include <string_view>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-magic-numbers"
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-c-arrays"

namespace opencmw {

struct CmwLight : Protocol<"CmwLight"> {
};

namespace cmwlight {

void skipString(IoBuffer &buffer);

template<typename T>
inline void skipArray(IoBuffer &buffer) {
    if constexpr (is_stringlike<T>) {
        for (auto i = buffer.get<int32_t>(); i > 0; i--) {
            skipString(buffer);
        }
    } else {
        buffer.skip(buffer.get<int32_t>() * static_cast<int32_t>(sizeof(T)));
    }
}

inline void skipString(IoBuffer &buffer) { skipArray<char>(buffer); }

template<typename T>
inline void skipMultiArray(IoBuffer &buffer) {
    buffer.skip(buffer.get<int32_t>() * static_cast<int32_t>(sizeof(int32_t))); // skip size header
    skipArray<T>(buffer);                                                       // skip elements
}

template<typename T>
inline int getTypeId() {
    return IoSerialiser<CmwLight, T>::getDataTypeId();
}
template<typename T>
inline int getTypeIdVector() {
    return IoSerialiser<CmwLight, std::vector<T>>::getDataTypeId();
}
template<typename T, size_t N>
inline int getTypeIdMultiArray() {
    return IoSerialiser<CmwLight, MultiArray<T, N>>::getDataTypeId();
}

} // namespace cmwlight

template<typename T>
struct IoSerialiser<CmwLight, T> {
    inline static constexpr uint8_t getDataTypeId() { return 0xFF; } // default value
};

template<Number T>
struct IoSerialiser<CmwLight, T> {
    inline static constexpr uint8_t getDataTypeId() {
        // clang-format off
        if      constexpr (std::is_same_v<bool   , T>) { return 0; }
        else if constexpr (std::is_same_v<int8_t , T>) { return 1; }
        else if constexpr (std::is_same_v<int16_t, T>) { return 2; }
        else if constexpr (std::is_same_v<int32_t, T>) { return 3; }
        else if constexpr (std::is_same_v<int64_t, T>) { return 4; }
        else if constexpr (std::is_same_v<float  , T>) { return 5; }
        else if constexpr (std::is_same_v<double , T>) { return 6; }
        else if constexpr (std::is_same_v<char   , T>) { return 201; }
        else { static_assert(opencmw::always_false<T>); }
        // clang-format on
    }

    constexpr static void serialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, const T &value) noexcept {
        buffer.put(value);
    }

    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) noexcept {
        value = buffer.get<T>();
    }
};

template<StringLike T>
struct IoSerialiser<CmwLight, T> {
    inline static constexpr uint8_t getDataTypeId() { return 7; }

    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const           &/*field*/, const T &value) noexcept {
        buffer.put(value);
    }

    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) noexcept {
        value = buffer.get<T>();
    }
};

template<ArrayOrVector T>
struct IoSerialiser<CmwLight, T> {
    using MemberType = typename T::value_type;

    inline static constexpr uint8_t getDataTypeId() {
        // clang-format off
        if      constexpr (std::is_same_v<bool   , MemberType>) { return  9; }
        else if constexpr (std::is_same_v<int8_t , MemberType>) { return 10; }
        else if constexpr (std::is_same_v<int16_t, MemberType>) { return 11; }
        else if constexpr (std::is_same_v<int32_t, MemberType>) { return 12; }
        else if constexpr (std::is_same_v<int64_t, MemberType>) { return 13; }
        else if constexpr (std::is_same_v<float  , MemberType>) { return 14; }
        else if constexpr (std::is_same_v<double , MemberType>) { return 15; }
        else if constexpr (std::is_same_v<char   , MemberType>) { return 202; }
        else if constexpr (opencmw::is_stringlike<MemberType> ) { return 16; }
        else { static_assert(opencmw::always_false<T>); }
        // clang-format on
    }

    constexpr static void serialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, const T &value) noexcept {
        buffer.put(value);
    }

    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) noexcept {
        buffer.getArray(value);
    }
};

template<MultiArrayType T>
struct IoSerialiser<CmwLight, T> {
    inline static constexpr uint8_t getDataTypeId() {
        // clang-format off
        if      constexpr (std::is_same_v<bool   , typename T::value_type> && T::n_dims_ == 1) { return cmwlight::getTypeIdVector<typename T::value_type>(); }
        else if constexpr (std::is_same_v<int8_t , typename T::value_type> && T::n_dims_ == 1) { return cmwlight::getTypeIdVector<typename T::value_type>(); }
        else if constexpr (std::is_same_v<int16_t, typename T::value_type> && T::n_dims_ == 1) { return cmwlight::getTypeIdVector<typename T::value_type>(); }
        else if constexpr (std::is_same_v<int32_t, typename T::value_type> && T::n_dims_ == 1) { return cmwlight::getTypeIdVector<typename T::value_type>(); }
        else if constexpr (std::is_same_v<int64_t, typename T::value_type> && T::n_dims_ == 1) { return cmwlight::getTypeIdVector<typename T::value_type>(); }
        else if constexpr (std::is_same_v<float  , typename T::value_type> && T::n_dims_ == 1) { return cmwlight::getTypeIdVector<typename T::value_type>(); }
        else if constexpr (std::is_same_v<double , typename T::value_type> && T::n_dims_ == 1) { return cmwlight::getTypeIdVector<typename T::value_type>(); }
        else if constexpr (std::is_same_v<char   , typename T::value_type> && T::n_dims_ == 1) { return cmwlight::getTypeIdVector<typename T::value_type>(); }
        else if constexpr (opencmw::is_stringlike<typename T::value_type>  && T::n_dims_ == 1) { return cmwlight::getTypeIdVector<typename T::value_type>(); }
        else if constexpr (std::is_same_v<bool   , typename T::value_type> && T::n_dims_ == 2) { return 17; }
        else if constexpr (std::is_same_v<int8_t , typename T::value_type> && T::n_dims_ == 2) { return 18; }
        else if constexpr (std::is_same_v<int16_t, typename T::value_type> && T::n_dims_ == 2) { return 19; }
        else if constexpr (std::is_same_v<int32_t, typename T::value_type> && T::n_dims_ == 2) { return 20; }
        else if constexpr (std::is_same_v<int64_t, typename T::value_type> && T::n_dims_ == 2) { return 21; }
        else if constexpr (std::is_same_v<float  , typename T::value_type> && T::n_dims_ == 2) { return 22; }
        else if constexpr (std::is_same_v<double , typename T::value_type> && T::n_dims_ == 2) { return 23; }
        else if constexpr (std::is_same_v<char   , typename T::value_type> && T::n_dims_ == 2) { return 203; }
        else if constexpr (opencmw::is_stringlike<typename T::value_type>  && T::n_dims_ == 2) { return 24; }
        else if constexpr (std::is_same_v<bool   , typename T::value_type>) { return 25; }
        else if constexpr (std::is_same_v<int8_t , typename T::value_type>) { return 26; }
        else if constexpr (std::is_same_v<int16_t, typename T::value_type>) { return 27; }
        else if constexpr (std::is_same_v<int32_t, typename T::value_type>) { return 28; }
        else if constexpr (std::is_same_v<int64_t, typename T::value_type>) { return 29; }
        else if constexpr (std::is_same_v<float  , typename T::value_type>) { return 30; }
        else if constexpr (std::is_same_v<double , typename T::value_type>) { return 31; }
        else if constexpr (std::is_same_v<char   , typename T::value_type>) { return 204; }
        else if constexpr (opencmw::is_stringlike<typename T::value_type> ) { return 32; }
        else { static_assert(opencmw::always_false<T>); }
        // clang-format on
    }

    constexpr static void serialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, const T &value) noexcept {
        std::array<int32_t, T::n_dims_> dims;
        for (uint32_t i = 0U; i < T::n_dims_; i++) {
            dims[i] = static_cast<int32_t>(value.dimensions()[i]);
        }
        buffer.put(dims);
        buffer.put(value.elements()); // todo: account for strides and offsets (possibly use iterators?)
    }

    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) noexcept {
        const std::array<int32_t, T::n_dims_> dimWire = buffer.getArray<int32_t, T::n_dims_>();
        for (auto i = 0U; i < T::n_dims_; i++) {
            value.dimensions()[i] = static_cast<typename T::size_t_>(dimWire[i]);
        }
        value.element_count()        = value.dimensions()[T::n_dims_ - 1];
        value.stride(T::n_dims_ - 1) = 1;
        value.offset(T::n_dims_ - 1) = 0;
        for (auto i = T::n_dims_ - 1; i > 0; i--) {
            value.element_count() *= value.dimensions()[i - 1];
            value.stride(i - 1) = value.stride(i) * value.dimensions()[i];
            value.offset(i - 1) = 0;
        }
        buffer.getArray(value.elements());
    }
};

template<typename MemberType>
struct IoSerialiser<CmwLight, std::set<MemberType>> {
    inline static constexpr uint8_t getDataTypeId() {
        // clang-format off
        if      constexpr (std::is_same_v<bool   , MemberType>) { return  9; }
        else if constexpr (std::is_same_v<int8_t , MemberType>) { return 10; }
        else if constexpr (std::is_same_v<int16_t, MemberType>) { return 11; }
        else if constexpr (std::is_same_v<int32_t, MemberType>) { return 12; }
        else if constexpr (std::is_same_v<int64_t, MemberType>) { return 13; }
        else if constexpr (std::is_same_v<float  , MemberType>) { return 14; }
        else if constexpr (std::is_same_v<double , MemberType>) { return 15; }
        else if constexpr (std::is_same_v<char   , MemberType>) { return 202; }
        else if constexpr (opencmw::is_stringlike<MemberType> ) { return 16; }
        else { static_assert(opencmw::always_false<MemberType>); }
        // clang-format on
    }

    constexpr static void serialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, const std::set<MemberType> &value) noexcept {
        buffer.put(static_cast<int32_t>(value.size()));
        if constexpr (std::is_arithmetic_v<MemberType>) {
            for (const auto v : value) {
                buffer.put(v);
            }
        } else {
            for (const auto &v : value) {
                buffer.put(v);
            }
        }
    }

    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const &field, std::set<MemberType> &value) noexcept {
        value.clear();
        auto n_elems = buffer.get<int32_t>();
        for (int i = 0; i < n_elems; i++) {
            MemberType            v;
            FieldDescriptionShort subfield{
                .headerStart       = buffer.position(),
                .dataStartPosition = buffer.position(),
                .dataEndPosition   = 0U,
                .subfields         = 0,
                .fieldName         = std::format("{}[{}]", field.fieldName, i),
                .intDataType       = IoSerialiser<CmwLight, MemberType>::getDataTypeId(),
                .hierarchyDepth    = static_cast<uint8_t>(field.hierarchyDepth + 1),
            };
            IoSerialiser<CmwLight, MemberType>::deserialise(buffer, subfield, v);
            value.insert(v);
        }
    }
};

template<>
struct IoSerialiser<CmwLight, START_MARKER> {
    inline static constexpr uint8_t getDataTypeId() { return 0x08; }

    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const &field, const START_MARKER           &/*value*/) noexcept {
        buffer.put(static_cast<int32_t>(field.subfields));
    }

    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto &field, const START_MARKER &) {
        field.subfields         = static_cast<uint16_t>(buffer.get<int32_t>());
        field.dataStartPosition = buffer.position();
    }
};

template<>
struct IoSerialiser<CmwLight, END_MARKER> {
    inline static constexpr uint8_t getDataTypeId() { return 0xFE; }

    static void                     serialise(IoBuffer                     &/*buffer*/, FieldDescription auto const                     &/*field*/, const END_MARKER                     &/*value*/) noexcept {
        // do not do anything, as the end marker is of size zero and only the type byte is important
    }

    constexpr static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, const END_MARKER &) {
        // do not do anything, as the end marker is of size zero and only the type byte is important
    }
};

template<>
struct IoSerialiser<CmwLight, OTHER> {
    inline static constexpr uint8_t getDataTypeId() { return 0xFD; }

    static void                     serialise(IoBuffer                     &/*buffer*/, FieldDescription auto const                     &/*field*/, const END_MARKER                     &/*value*/) noexcept {
        // do not do anything, as the end marker is of size zero and only the type byte is important
    }

    static void deserialise(IoBuffer &buffer, FieldDescription auto const &field, const OTHER &) {
        using namespace cmwlight;
        auto typeId = field.intDataType;
        // clang-format off
        // primitives
        if      (typeId == getTypeId<bool       >()) { buffer.skip(sizeof(bool   )); }
        else if (typeId == getTypeId<int8_t     >()) { buffer.skip(sizeof(int8_t )); }
        else if (typeId == getTypeId<int16_t    >()) { buffer.skip(sizeof(int16_t)); }
        else if (typeId == getTypeId<int32_t    >()) { buffer.skip(sizeof(int32_t)); }
        else if (typeId == getTypeId<int64_t    >()) { buffer.skip(sizeof(int64_t)); }
        else if (typeId == getTypeId<float      >()) { buffer.skip(sizeof(float  )); }
        else if (typeId == getTypeId<double     >()) { buffer.skip(sizeof(double )); }
        else if (typeId == getTypeId<char       >()) { buffer.skip(sizeof(char   )); }
        else if (typeId == getTypeId<std::string>()) { skipString(buffer); }
        // arrays
        else if (typeId == getTypeIdVector<int8_t     >()) { skipArray<int8_t     >(buffer); }
        else if (typeId == getTypeIdVector<int16_t    >()) { skipArray<int16_t    >(buffer); }
        else if (typeId == getTypeIdVector<int32_t    >()) { skipArray<int32_t    >(buffer); }
        else if (typeId == getTypeIdVector<int64_t    >()) { skipArray<int64_t    >(buffer); }
        else if (typeId == getTypeIdVector<float      >()) { skipArray<float      >(buffer); }
        else if (typeId == getTypeIdVector<double     >()) { skipArray<double     >(buffer); }
        else if (typeId == getTypeIdVector<char       >()) { skipArray<char       >(buffer); }
        else if (typeId == getTypeIdVector<std::string>()) { skipArray<std::string>(buffer); }
        // 2D and Multi array
        else if (typeId == getTypeIdMultiArray<int8_t     , 2>() || typeId == getTypeIdMultiArray<int8_t     , 3>()) { skipMultiArray<int8_t     >(buffer); }
        else if (typeId == getTypeIdMultiArray<int16_t    , 2>() || typeId == getTypeIdMultiArray<int16_t    , 3>()) { skipMultiArray<int16_t    >(buffer); }
        else if (typeId == getTypeIdMultiArray<int32_t    , 2>() || typeId == getTypeIdMultiArray<int32_t    , 3>()) { skipMultiArray<int32_t    >(buffer); }
        else if (typeId == getTypeIdMultiArray<int64_t    , 2>() || typeId == getTypeIdMultiArray<int64_t    , 3>()) { skipMultiArray<int64_t    >(buffer); }
        else if (typeId == getTypeIdMultiArray<float      , 2>() || typeId == getTypeIdMultiArray<float      , 3>()) { skipMultiArray<float      >(buffer); }
        else if (typeId == getTypeIdMultiArray<double     , 2>() || typeId == getTypeIdMultiArray<double     , 3>()) { skipMultiArray<double     >(buffer); }
        else if (typeId == getTypeIdMultiArray<char       , 2>() || typeId == getTypeIdMultiArray<char       , 3>()) { skipMultiArray<char       >(buffer); }
        else if (typeId == getTypeIdMultiArray<std::string, 2>() || typeId == getTypeIdMultiArray<std::string, 3>()) { skipMultiArray<std::string>(buffer); }
        // nested
        // unsupported
        else { throw ProtocolException(std::format("Skipping data type {} is not supported", typeId)); }
        // clang-format on
    }
};

template<>
struct FieldHeaderWriter<CmwLight> {
    template<const bool /*writeMetaInfo*/, typename DataType>
    constexpr std::size_t static put(IoBuffer &buffer, FieldDescription auto const &field, const DataType &data) {
        using StrippedDataType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(data)))>;
        if constexpr (is_same_v<DataType, END_MARKER>) { // do not put endMarker type id into buffer
            return std::numeric_limits<size_t>::max();
        }
        constexpr auto dataTypeSize = static_cast<int32_t>(sizeof(StrippedDataType));
        buffer.reserve_spare(((field.fieldName.size() + 2UL) * sizeof(uint8_t)) + dataTypeSize);
        if (field.hierarchyDepth != 0) {
            buffer.put<IoBuffer::MetaInfo::WITH>(field.fieldName); // full field name with zero termination
        }
        if (!is_same_v<DataType, START_MARKER> || field.hierarchyDepth != 0) {     // do not put startMarker type id into buffer
            buffer.put(IoSerialiser<CmwLight, StrippedDataType>::getDataTypeId()); // data type ID
        }
        IoSerialiser<CmwLight, StrippedDataType>::serialise(buffer, field, getAnnotatedMember(unwrapPointer(data)));
        return std::numeric_limits<size_t>::max();
    }
};

template<>
struct FieldHeaderReader<CmwLight> {
    template<ProtocolCheck protocolCheckVariant>
    inline static void get(IoBuffer &buffer, DeserialiserInfo & /*info*/, FieldDescription auto &field) {
        field.headerStart     = buffer.position();
        field.dataEndPosition = std::numeric_limits<size_t>::max();
        field.modifier        = ExternalModifier::UNKNOWN;
        if (field.subfields == 0) {
            if (field.hierarchyDepth != 0 && field.intDataType == IoSerialiser<CmwLight, START_MARKER>::getDataTypeId() && buffer.get<int32_t>() != 0) {
                throw ProtocolException("logic error, parent serialiser claims no data but header differs");
            }
            field.intDataType = IoSerialiser<CmwLight, END_MARKER>::getDataTypeId();
            field.hierarchyDepth--;
            field.dataStartPosition = buffer.position();
            return;
        }
        if (field.subfields == -1) {
            if (field.hierarchyDepth != 0) {                        // do not read field description for root element
                field.fieldName   = buffer.get<std::string_view>(); // full field name
                field.intDataType = buffer.get<uint8_t>();          // data type ID
            } else {
                field.intDataType = IoSerialiser<CmwLight, START_MARKER>::getDataTypeId();
            }
        } else {
            field.fieldName   = buffer.get<std::string_view>(); // full field name
            field.intDataType = buffer.get<uint8_t>();          // data type ID
            field.subfields--;                                  // decrease the number of remaining fields in the structure...
        }
        field.dataStartPosition = buffer.position();
    }
};

template<>
inline DeserialiserInfo checkHeaderInfo<CmwLight>(IoBuffer &buffer, DeserialiserInfo info, const ProtocolCheck /*protocolCheckVariant*/) {
    // cmw has no specific header to check, we could try to find a common pattern in all cmw output to match to that
    // minimal thing to do is to check the number of fields to be non-zero. N.B. this fails for YaS data
    if (buffer.at<int32_t>(buffer.position()) == 0) {
        throw ProtocolException("Illegal number of members"); // todo: respect protocol check type
    }
    return info;
}

template<typename ValueType>
struct IoSerialiser<CmwLight, std::map<std::string, ValueType>> {
    inline static constexpr uint8_t getDataTypeId() {
        return IoSerialiser<CmwLight, START_MARKER>::getDataTypeId();
    }

    constexpr static void serialise(IoBuffer &buffer, FieldDescription auto const &parentField, const std::map<std::string, ValueType> &value) noexcept {
        buffer.put(static_cast<int32_t>(value.size()));
        for (auto &[key, val] : value) {
            if constexpr (isReflectableClass<ValueType>()) { // nested data-structure
                const auto            subfields            = value.size();
                FieldDescription auto field                = newFieldHeader<CmwLight, true>(buffer, parentField.hierarchyDepth + 1, FWD(val), subfields);
                const std::size_t     posSizePositionStart = FieldHeaderWriter<CmwLight>::template put<true>(buffer, field, val);
                const std::size_t     posStartDataStart    = buffer.size();
                return;
            } else { // field is a (possibly annotated) primitive type
                FieldDescription auto field = opencmw::detail::newFieldHeader<CmwLight, true>(buffer, key.c_str(), parentField.hierarchyDepth + 1, val, 0);
                FieldHeaderWriter<CmwLight>::template put<true>(buffer, field, val);
            }
        }
    }

    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const &parent, std::map<std::string, ValueType> &value) noexcept {
        DeserialiserInfo        info;
        constexpr ProtocolCheck check = ProtocolCheck::IGNORE;
        using protocol                = CmwLight;
        auto field                    = opencmw::detail::newFieldHeader<CmwLight, true>(buffer, "", parent.hierarchyDepth, ValueType{}, parent.subfields);
        while (buffer.position() < buffer.size()) {
            auto previousSubFields = field.subfields;
            FieldHeaderReader<protocol>::template get<check>(buffer, info, field);
            buffer.set_position(field.dataStartPosition); // skip to data start

            if (field.intDataType == IoSerialiser<protocol, END_MARKER>::getDataTypeId()) { // reached end of sub-structure
                try {
                    IoSerialiser<protocol, END_MARKER>::deserialise(buffer, field, END_MARKER_INST);
                } catch (...) {
                    if (opencmw::detail::handleDeserialisationErrorAndSkipToNextField<check>(buffer, field, info, "IoSerialiser<{}, END_MARKER>::deserialise(buffer, {}::{}, END_MARKER_INST): position {} vs. size {} -- exception: {}",
                                protocol::protocolName(), parent.fieldName, field.fieldName, buffer.position(), buffer.size(), what())) {
                        continue;
                    }
                }
                return; // step down to previous hierarchy depth
            }

            const auto [fieldValue, _] = value.insert({ std::string{ field.fieldName }, ValueType{} });
            if constexpr (isReflectableClass<ValueType>()) {
                field.intDataType = IoSerialiser<protocol, START_MARKER>::getDataTypeId();
                buffer.set_position(field.headerStart); // reset buffer position for the nested deserialiser to read again
                field.hierarchyDepth++;
                deserialise<protocol, check>(buffer, fieldValue->second, info, field);
                field.hierarchyDepth--;
                field.subfields = previousSubFields - 1;
            } else {
                constexpr int requestedType = IoSerialiser<protocol, ValueType>::getDataTypeId();
                if (requestedType != field.intDataType) { // mismatching data-type
                    opencmw::detail::moveToFieldEndBufferPosition(buffer, field);
                    opencmw::detail::handleDeserialisationError<check>(info, "mismatched field type for map field {} - requested type: {} (typeID: {}) got: {}", field.fieldName, typeName<ValueType>, requestedType, field.intDataType);
                    return;
                }
                IoSerialiser<protocol, ValueType>::deserialise(buffer, field, fieldValue->second);
            }
        }
    }
};
} // namespace opencmw

#pragma clang diagnostic pop
#endif // OPENCMW_CMWLIGHTSERIALISER_H
