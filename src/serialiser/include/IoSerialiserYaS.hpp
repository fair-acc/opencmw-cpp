#ifndef OPENCMW_YASERIALISER_H
#define OPENCMW_YASERIALISER_H

#include <list>
#include <map>
#include <queue>

#include <opencmw.hpp>

#include "IoSerialiser.hpp"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-magic-numbers"
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-c-arrays"

namespace opencmw {
struct YaS : Protocol<"YaS"> {};

namespace yas {
static const int              VERSION_MAGIC_NUMBER = -1;    // '-1' since CmwLight cannot have a negative number of entries
static const std::string_view PROTOCOL_NAME        = "YaS"; // Yet another Serialiser implementation
static const uint8_t          VERSION_MAJOR        = 1;
static const uint8_t          VERSION_MINOR        = 0;
static const uint8_t          VERSION_MICRO        = 0;
static const uint8_t          ARRAY_TYPE_OFFSET    = 100U;

// clang-format off
template<typename T> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId() { return 0xFF; } // default value
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<START_MARKER>() { return 0; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<bool>() { return 1; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<int8_t>() { return 2; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<int16_t>() { return 3; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<int32_t>() { return 4; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<int64_t>() { return 5; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<float>() { return 6; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<double>() { return 7; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<char>() { return 8; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::string>() { return 9; }

template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<bool[]>() { return 101; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::vector<bool>>() { return 101; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<int8_t[]>() { return 102; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::vector<int8_t>>() { return 102; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<int16_t[]>() { return 103; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::vector<int16_t>>() { return 103; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<int32_t[]>() { return 104; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::vector<int32_t>>() { return 104; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<int64_t[]>() { return 105; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::vector<int64_t>>() { return 105; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<float[]>() { return 106; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::vector<float>>() { return 106; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<double[]>() { return 107; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::vector<double>>() { return 107; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<char[]>() { return 108; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::vector<char>>() { return 108; }

template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::string[]>() { return 109; }
template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::string_view[]>() { return 109; }

// template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<START_MARKER>() { return 200; }
// template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<enum>()          { return 201; }
// template<typename T> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::list<T>>()     { return 202; }
// template<> OPENCMW_FORCEINLINE constexpr uint8_t getDataTypeId<std::queue>()    { return 204; }

template<>
inline constexpr uint8_t getDataTypeId<OTHER>() { return 0xFD; }

template<>
inline constexpr uint8_t getDataTypeId<END_MARKER>() { return 0xFE; }

// clang-format on
} // namespace yas

template<typename T>
struct IoSerialiser<YaS, T> {
    OPENCMW_FORCEINLINE static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); } // default value
};

template<Number T> // catches all numbers
struct IoSerialiser<YaS, T> {
    OPENCMW_FORCEINLINE static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); }
    OPENCMW_FORCEINLINE constexpr static void    serialise(IoBuffer &buffer, FieldDescription auto const    &/*field*/, const T &value) noexcept {
        buffer.put(value);
    }
    OPENCMW_FORCEINLINE constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) noexcept {
        value = buffer.get<T>();
    }
};

template<StringLike T>
struct IoSerialiser<YaS, T> {
    OPENCMW_FORCEINLINE static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); }
    OPENCMW_FORCEINLINE constexpr static void    serialise(IoBuffer &buffer, FieldDescription auto const    &/*field*/, const T &value) noexcept {
        buffer.put<opencmw::IoBuffer::MetaInfo::WITH, T>(value); // N.B. ensure that the wrapped value and not the annotation itself is serialised
    }
    OPENCMW_FORCEINLINE constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) noexcept {
        value = buffer.get<std::string>();
    }
};

template<ArrayOrVector T>
struct IoSerialiser<YaS, T> {
    OPENCMW_FORCEINLINE static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<T>(); }
    OPENCMW_FORCEINLINE constexpr static void    serialise(IoBuffer &buffer, FieldDescription auto const    &/*field*/, const T &value) noexcept {
        buffer.put(std::array<int32_t, 1>{ static_cast<int32_t>(value.size()) });
        buffer.put(value);
    }
    OPENCMW_FORCEINLINE constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) noexcept {
        buffer.getArray<int32_t, 1>();
        buffer.getArray(value);
    }
};

template<MultiArrayType T>
struct IoSerialiser<YaS, T> {
    OPENCMW_FORCEINLINE static constexpr uint8_t getDataTypeId() {
        return yas::ARRAY_TYPE_OFFSET + yas::getDataTypeId<typename T::value_type>();
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

template<typename V> // set
struct IoSerialiser<YaS, std::set<V>> {
    OPENCMW_FORCEINLINE static constexpr uint8_t getDataTypeId() {
        return yas::ARRAY_TYPE_OFFSET + yas::getDataTypeId<V>();
    }
    constexpr static void serialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, const std::set<V> &value) noexcept {
        buffer.put(std::array<int32_t, 1>{ static_cast<int32_t>(value.size()) }); // dimensions, for set always n=1 and n_1 = nElems
        buffer.put(static_cast<int32_t>(value.size()));                           // size of vector
        for (const V &v : value) {
            buffer.put(v);
        }
    }
    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, std::set<V> &value) noexcept {
        auto           size = buffer.getArray<int32_t, 1>();
        std::vector<V> data{ size[0] };
        buffer.getArray(data, static_cast<size_t>(size[0]));
        value.clear();
        std::copy(data.begin(), data.end(), std::inserter(value, value.begin()));
    }
};

template<MapLike T>
struct IoSerialiser<YaS, T> {
    OPENCMW_FORCEINLINE static constexpr uint8_t getDataTypeId() { return 203; }
    constexpr static void                        serialise(IoBuffer &buffer, FieldDescription auto const &field, const T &value) noexcept {
        using K              = typename T::key_type;
        using V              = typename T::mapped_type;
        const auto nElements = static_cast<int32_t>(value.size());
        buffer.put(nElements); // nElements

        if constexpr (is_supported_number<K> || is_stringlike<K>) {
            constexpr int entrySize = 17; // as an initial estimate
            buffer.reserve_spare(static_cast<size_t>(nElements * entrySize + 9));
            buffer.put(static_cast<uint8_t>(yas::getDataTypeId<K>())); // type-id
            buffer.put(nElements);                                     // nElements
            for (auto &entry : value) {
                buffer.put(entry.first);
            }
        } else {                                     // non-primitive or non-string-like types
            buffer.put(yas::getDataTypeId<OTHER>()); // type-id
            buffer.put(typeName<K>);                 // primary type
            buffer.put("");                          // secondary type (if any) TODO: add appropriate
            buffer.put(nElements);
            for (auto &entry : value) {
                IoSerialiser<YaS, K>::serialise(buffer, field, entry.first);
            }
        }

        if constexpr (is_supported_number<V> || is_stringlike<V>) {
            constexpr int entrySize = 17; // as an initial estimate
            buffer.reserve_spare(static_cast<size_t>(nElements * entrySize + 9));
            buffer.put(static_cast<uint8_t>(yas::getDataTypeId<V>()));
            buffer.put(nElements);
            for (auto &entry : value) {
                buffer.put(entry.second);
            }
        } else {                                         // non-primitive or non-string-like types
            buffer.put(yas::getDataTypeId<OTHER>());     // type-id
            buffer.put(typeName<V>);                     // primary type
            buffer.put("");                              // secondary type (if any) TODO: add appropriate
            buffer.put(static_cast<int32_t>(nElements)); // nElements
            for (auto &entry : value) {
                IoSerialiser<YaS, V>::serialise(buffer, field, entry.second);
            }
        }
    }
    static void deserialise(IoBuffer &buffer, FieldDescription auto const &field, T &value) {
        using K                  = typename T::key_type;
        using V                  = typename T::mapped_type;
        const auto     nElements = static_cast<uint32_t>(buffer.get<int32_t>());

        const auto     keyType   = buffer.get<uint8_t>();
        std::vector<K> keys;
        keys.reserve(nElements);
        if constexpr (is_supported_number<K> || is_stringlike<K>) {
            if (yas::getDataTypeId<K>() != keyType) {
                throw ProtocolException("key type mismatch for field {} - required {} ({}) vs. have {}", field.fieldName, yas::getDataTypeId<K>(), typeid(K).name(), keyType);
            }
            const auto nElementsCheck = static_cast<uint32_t>(buffer.get<int32_t>());
            if (nElements != nElementsCheck) {
                throw ProtocolException("key array length mismatch for field {} - required {} vs. have {}", field.fieldName, nElements, nElementsCheck);
            }
            for (auto i = 0U; i < nElementsCheck; i++) {
                keys.emplace_back(buffer.get<K>());
            }
        } else if (keyType == yas::getDataTypeId<OTHER>()) {
            throw ProtocolException("key type OTHER for field {} not yet implemented", field.fieldName);
        } else {
            throw ProtocolException("unsupported key type {} for field {}", keyType, field.fieldName);
        }

        const auto valueType = buffer.get<uint8_t>();
        if constexpr (is_supported_number<V> || is_stringlike<V>) {
            if (yas::getDataTypeId<V>() != valueType) {
                throw ProtocolException("value type mismatch for field {} - required {} ({}) vs. have {}", field.fieldName, yas::getDataTypeId<V>(), typeid(V).name(), valueType);
            }
            const auto nElementsCheck = static_cast<uint32_t>(buffer.get<int32_t>());
            if (nElements != nElementsCheck) {
                throw ProtocolException("value array length mismatch for field {} - required {} vs. have {}", field.fieldName, nElements, nElementsCheck);
            }
            value.clear();
            for (auto i = 0U; i < nElementsCheck; i++) {
                auto v         = buffer.get<V>();
                value[keys[i]] = v;
            }
        } else if (valueType == yas::getDataTypeId<OTHER>()) {
            std::string value_type_name           = buffer.get<std::string>();
            std::string value_type_name_secondary = buffer.get<std::string>();
            auto        value_elements            = static_cast<std::size_t>(buffer.get<int32_t>());
            for (std::size_t i = 0; i < value_elements; i++) {
                FieldDescriptionShort subfield{
                    .headerStart       = buffer.position(),
                    .dataStartPosition = buffer.position(),
                    .dataEndPosition   = 0U,
                    .subfields         = 0,
                    .fieldName         = std::format("{}[{}]", field.fieldName, i),
                    .intDataType       = IoSerialiser<YaS, V>::getDataTypeId(),
                    .hierarchyDepth    = static_cast<uint8_t>(field.hierarchyDepth + 1),
                };
                V v;
                IoSerialiser<YaS, V>::deserialise(buffer, subfield, v);
                value[keys[i]] = v;
            }
        } else {
            throw ProtocolException("unsupported key type {} for field {}", keyType, field.fieldName);
        }
    }
};

template<>
struct IoSerialiser<YaS, START_MARKER> {
    OPENCMW_FORCEINLINE static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<START_MARKER>(); }
    // do not do anything, as the start marker is of size zero and only the type byte is important
    constexpr static void serialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, const START_MARKER & /*value*/) noexcept {}
    constexpr static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, const START_MARKER &) {}
};

template<>
struct IoSerialiser<YaS, END_MARKER> {
    OPENCMW_FORCEINLINE static constexpr uint8_t getDataTypeId() { return yas::getDataTypeId<END_MARKER>(); }
    // do not do anything, as the end marker is of size zero and only the type byte is important
    constexpr static void serialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, const END_MARKER & /*value*/) noexcept {}
    constexpr static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, const END_MARKER &) {}
};

template<>
struct FieldHeaderWriter<YaS> {
    template<const bool writeMetaInfo, typename DataType>
    constexpr std::size_t static put(IoBuffer &buffer, FieldDescription auto &field, const DataType &data) {
        using StrippedDataType      = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(data)))>;
        constexpr auto dataTypeSize = static_cast<int32_t>(sizeof(StrippedDataType));
        buffer.reserve_spare(((field.fieldName.size() + 18UL) * sizeof(uint8_t)) + dataTypeSize);

        // -- offset 0 vs. field start
        field.headerStart = buffer.size();
        buffer.put(IoSerialiser<YaS, StrippedDataType>::getDataTypeId()); // data type ID
        const std::size_t dataStartOffsetPosition = buffer.size();
        buffer.put(-1); // dataStart offset
        const std::size_t dataSizePosition = buffer.size();
        buffer.put(-1);              // dataSize (N.B. 'headerStart' + 'dataStart + dataSize' == start of next field header
        buffer.put(field.fieldName); // full field name

        if constexpr (writeMetaInfo && is_annotated<DataType>) {
            buffer.put(field.unit);
            buffer.put(field.description);
            buffer.put(static_cast<uint8_t>(field.modifier));
            // TODO: write group meta data
        }

        field.dataStartPosition                            = buffer.size();
        buffer.at<int32_t, false>(dataStartOffsetPosition) = static_cast<int32_t>(field.dataStartPosition - field.headerStart); // -- offset dataStart calculations and write offset to dataStart

        // from hereon there are data specific structures that are written to the IoBuffer
        IoSerialiser<YaS, StrippedDataType>::serialise(buffer, field, getAnnotatedMember(unwrapPointer(data)));

        // adjust data-end position
        buffer.at<int32_t, false>(dataSizePosition) = static_cast<int32_t>(buffer.size() - field.dataStartPosition); // write data size

        return dataSizePosition; // N.B. exported for adjusting START_MARKER -> END_MARKER data size to be adjustable and thus skippable
    }
};

template<>
inline void updateSize<YaS>(IoBuffer &buffer, const size_t posSizePositionStart, const size_t posStartDataStart) {
    buffer.at<int32_t, false>(posSizePositionStart) = static_cast<int32_t>(buffer.size() - posStartDataStart); // write data size
}

template<>
inline void putHeaderInfo<YaS>(IoBuffer &buffer) {
    buffer.reserve_spare(2 * sizeof(int) + 7); // magic int + string length int + 4 byte 'YAS\0` string + 3 version bytes
    buffer.put(yas::VERSION_MAGIC_NUMBER);
    buffer.put(yas::PROTOCOL_NAME);
    buffer.put(yas::VERSION_MAJOR);
    buffer.put(yas::VERSION_MINOR);
    buffer.put(yas::VERSION_MICRO);
}

template<>
inline DeserialiserInfo checkHeaderInfo<YaS>(IoBuffer &buffer, DeserialiserInfo info, const ProtocolCheck check) {
    const auto magic = buffer.get<int>();
    if (yas::VERSION_MAGIC_NUMBER != magic) {
        if (check == ProtocolCheck::LENIENT) {
            info.exceptions.emplace_back(ProtocolException("Wrong serialiser magic number: {} != -1", magic));
        }
        if (check == ProtocolCheck::ALWAYS) {
            throw ProtocolException("Wrong serialiser magic number: {} != -1", magic);
        }
        return info;
    }
    auto proto_name = buffer.get<std::string>();
    if (yas::PROTOCOL_NAME != proto_name) {
        if (check == ProtocolCheck::LENIENT) {
            info.exceptions.emplace_back(ProtocolException("Wrong serialiser identification string: {} != YaS", proto_name));
        }
        if (check == ProtocolCheck::ALWAYS) {
            throw ProtocolException("Wrong serialiser identification string: {} != YaS", proto_name);
        }
        return info;
    }
    auto ver_major = buffer.get<int8_t>();
    auto ver_minor = buffer.get<int8_t>();
    auto ver_micro = buffer.get<int8_t>();
    if (yas::VERSION_MAJOR != ver_major) {
        if (check == ProtocolCheck::LENIENT) {
            info.exceptions.emplace_back(ProtocolException("Major versions do not match, received {}.{}.{}", ver_major, ver_minor, ver_micro));
        }
        if (check == ProtocolCheck::ALWAYS) {
            throw ProtocolException("Major versions do not match, received {}.{}.{}", ver_major, ver_minor, ver_micro);
        }
        return info;
    }
    return info;
}

template<>
struct FieldHeaderReader<YaS> {
    template<ProtocolCheck check>
    inline static void get(IoBuffer &buffer, const DeserialiserInfo & /*info*/, FieldDescriptionLong &result) {
        result.headerStart         = buffer.position();
        result.intDataType         = buffer.get<uint8_t>(); // data type ID
        const auto dataStartOffset = static_cast<uint64_t>(buffer.get<int32_t>());
        const auto dataSize        = static_cast<uint64_t>(buffer.get<int32_t>());
        result.fieldName           = buffer.get<std::string_view>(); // full field name
        result.dataStartPosition   = result.headerStart + dataStartOffset;
        result.dataEndPosition     = result.headerStart + dataStartOffset + dataSize;
        // the following information is optional
        // e.g. could skip to 'headerStart + dataStartOffset' and start reading the data, or
        // e.g. could skip to 'headerStart + dataStartOffset + dataSize' and start reading the next field header

        using namespace std::string_view_literals;
        if constexpr (check == ProtocolCheck::IGNORE) { // safe defaults (will be ignored later on)
            result.unit        = ""sv;
            result.description = ""sv;
            result.modifier    = ExternalModifier::RW;
        } else { // need to read the meta information
            result.unit        = (buffer.position() == result.dataStartPosition) ? ""sv : buffer.get<std::string_view>();
            result.description = (buffer.position() == result.dataStartPosition) ? ""sv : buffer.get<std::string_view>();
            result.modifier    = (buffer.position() == result.dataStartPosition) ? ExternalModifier::RW : get_ext_modifier(buffer.get<uint8_t>());
        }
    }
};

} // namespace opencmw

#pragma clang diagnostic pop
#endif // OPENCMW_YASERIALISER_H
