#ifndef OPENCMW_CMWLIGHTSERIALISER_H
#define OPENCMW_CMWLIGHTSERIALISER_H

#include "IoSerialiser.hpp"
#include <string_view>

#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-c-arrays"

namespace opencmw {

struct CmwLight : Protocol<"CmwLight"> {
};

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
        if      constexpr (std::is_same_v<bool   , typename T::value_type> && T::n_dims_ == 1) { return  9; }
        else if constexpr (std::is_same_v<int8_t , typename T::value_type> && T::n_dims_ == 1) { return 10; }
        else if constexpr (std::is_same_v<int16_t, typename T::value_type> && T::n_dims_ == 1) { return 11; }
        else if constexpr (std::is_same_v<int32_t, typename T::value_type> && T::n_dims_ == 1) { return 12; }
        else if constexpr (std::is_same_v<int64_t, typename T::value_type> && T::n_dims_ == 1) { return 13; }
        else if constexpr (std::is_same_v<float  , typename T::value_type> && T::n_dims_ == 1) { return 14; }
        else if constexpr (std::is_same_v<double , typename T::value_type> && T::n_dims_ == 1) { return 15; }
        else if constexpr (std::is_same_v<char   , typename T::value_type> && T::n_dims_ == 1) { return 202; }
        else if constexpr (opencmw::is_stringlike<typename T::value_type>  && T::n_dims_ == 1) { return 16; }
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

template<>
struct IoSerialiser<CmwLight, START_MARKER> {
    inline static constexpr uint8_t getDataTypeId() { return 0xFC; }

    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const &field, const START_MARKER           &/*value*/) noexcept {
        buffer.put(static_cast<int32_t>(field.subfields));
    }

    constexpr static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, const START_MARKER &) {
        // do not do anything, as the start marker is of size zero and only the type byte is important
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

    static void deserialise(IoBuffer &buffer, FieldDescription auto const &field, const END_MARKER &) {
        auto typeId = field.intDataType;
        if (typeId == IoSerialiser<CmwLight, int>::getDataTypeId()) {
            IoSerialiser<CmwLight, int>::deserialise(buffer, field, std::ignore);
            // todo: implement more types/find generic way to express this
        } else {
            throw ProtocolException(fmt::format("Skipping data type {} is not supported", typeId));
        }
        // todo: check how this works with nested data
    }
};

template<>
struct FieldHeaderWriter<CmwLight> {
    template<const bool /*writeMetaInfo*/, typename DataType>
    constexpr std::size_t static put(IoBuffer &buffer, FieldDescription auto const &field, const DataType &data) {
        using StrippedDataType      = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(data)))>;
        constexpr auto dataTypeSize = static_cast<int32_t>(sizeof(StrippedDataType));
        buffer.reserve_spare(((field.fieldName.size() + 2UL) * sizeof(uint8_t)) + dataTypeSize);
        if (field.hierarchyDepth != 0) {
            buffer.put<IoBuffer::MetaInfo::WITH>(field.fieldName);                 // full field name with zero termination
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
        if (field.subfields == 0) {
            field.intDataType = IoSerialiser<CmwLight, END_MARKER>::getDataTypeId();
            field.hierarchyDepth--;
            return;
        }
        field.headerStart = buffer.position();
        if (field.hierarchyDepth == 0) { // do not read field description for root element
            field.subfields = static_cast<std::size_t>(buffer.get<int32_t>());
            field.hierarchyDepth++;
        } else {
            field.fieldName   = buffer.get<std::string_view>();                               // full field name
            field.intDataType = buffer.get<uint8_t>();                                        // data type ID
            if (field.intDataType == IoSerialiser<CmwLight, START_MARKER>::getDataTypeId()) { // read number of fields for start markers
                field.subfields = static_cast<std::size_t>(buffer.get<int32_t>());
                field.hierarchyDepth++;
            } else {
                field.subfields--; // decrease the number of remaining fields in the structure... todo: adapt strategy for nested fields (has to somewhere store subfields)
            }
        }
        field.dataStartPosition = buffer.position();
        field.dataEndPosition   = std::numeric_limits<size_t>::max();
        // field.unit = ""sv;
        // field.description = ""sv;
        field.modifier = ExternalModifier::UNKNOWN;
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

} // namespace opencmw

#pragma clang diagnostic pop
#endif // OPENCMW_CMWLIGHTSERIALISER_H
