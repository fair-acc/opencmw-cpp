#ifndef OPENCMW_IOBUFFER_H
#define OPENCMW_IOBUFFER_H
#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-owning-memory"
#pragma ide diagnostic ignored "UnreachableCode" // -- allow for alternate non-c-style memory management

#include <format>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#if defined(_LIBCPP_VERSION) and _LIBCPP_VERSION < 16000
#include <experimental/memory_resource>
namespace std {
namespace pmr {
using memory_resource = std::experimental::pmr::memory_resource;
template<typename T>
using polymorphic_allocator = std::experimental::pmr::polymorphic_allocator<T>;
}
} // namespace std::pmr
#else
#include <memory_resource>
#endif
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <opencmw.hpp>

#include "MultiArray.hpp"

namespace opencmw {

/* special memory resource which explicitly uses malloc, free and allows the containers
 * using it to call to realloc themselves, because it is not in the allocators API.
 */
class Reallocator : public std::pmr::memory_resource {
    void *do_allocate(size_t bytes, size_t /*alignment*/) override {
        return malloc(bytes);
    }

    void do_deallocate(void *p, size_t /*bytes*/, size_t /*alignment*/) override {
        return free(p);
    }

    [[nodiscard]] bool do_is_equal(const memory_resource &other) const noexcept override {
        return this == &other;
    }

public:
    static inline Reallocator *defaultReallocator() {
        static Reallocator instance = Reallocator();
        return &instance;
    }
};

/*
 * Special Memory Resource which disallows allocation to use for wrapping byte arrays
 */
class ThrowingAllocator : public std::pmr::memory_resource {
    std::function<void(void *)> _deleter;

    void                       *do_allocate(size_t /*bytes*/, size_t /*alignment*/) override {
        throw std::bad_alloc{};
    }

    void do_deallocate(void *p, size_t /*bytes*/, size_t /*alignment*/) override {
        _deleter(p);
    }

    [[nodiscard]] bool do_is_equal(const memory_resource &other) const noexcept override {
        return this == &other;
    }

public:
    explicit ThrowingAllocator(std::function<void(void *)> deleter)
        : _deleter(std::move(deleter)) {}

    static inline ThrowingAllocator *defaultNonOwning() {
        static ThrowingAllocator instance = ThrowingAllocator([](void * /*p*/) {});
        return &instance;
    }

    static inline ThrowingAllocator *defaultOwning() {
        static ThrowingAllocator instance = ThrowingAllocator([](void *p) { free(p); });
        return &instance;
    }
};

struct IoBuffer {
    enum MetaInfo {
        WITH,
        WITHOUT
    };

private:
    using Allocator         = std::pmr::polymorphic_allocator<uint8_t>;
    using ByteBufferPointer = typename std::uint8_t *;
    Allocator                   _allocator{};
    ByteBufferPointer           _buffer   = nullptr;
    mutable std::size_t         _position = 0LU;
    std::size_t                 _size     = 0LU;
    std::size_t                 _capacity = 0LU;

    constexpr ByteBufferPointer reallocate(const std::size_t capacity) noexcept {
        if (capacity == 0) {
            freeInternalBuffer();
            _capacity = capacity;
            return _buffer;
        } else if (_capacity == capacity && _buffer != nullptr) {
            return _buffer;
        } else if (_buffer == nullptr) {
            _buffer   = _allocator.allocate(capacity);
            _capacity = capacity;
            return _buffer;
        }
        const auto oldCapacity = _capacity;
        _capacity = capacity;
        if (dynamic_cast<Reallocator *>(_allocator.resource())) {
            // N.B. 'realloc' is safe as long as the de-allocation is done via 'free'
            _buffer = static_cast<uint8_t *>(realloc(_buffer, _capacity * sizeof(uint8_t)));
        } else {
            // buffer already exists - copy existing content into newly allocated buffer N.B. maybe larger/smaller
            auto *tBuffer = _allocator.allocate(_capacity);
            // std::memmove(tBuffer, _buffer, std::min(_size, size) * sizeof(uint8_t));
            std::copy(_buffer, _buffer + std::min(_size, _capacity) * sizeof(uint8_t), tBuffer);
            _allocator.deallocate(_buffer, oldCapacity);
            _buffer = tBuffer;
        }
        return _buffer;
    }

    constexpr void freeInternalBuffer() {
        if (_buffer != nullptr) {
            _allocator.deallocate(_buffer, _capacity);
            _buffer = nullptr;
        }
    }

    template<typename T>
    constexpr T swapBytes(const T &value) const noexcept {
        T          result     = 0;
        const auto value_ptr  = reinterpret_cast<const uint8_t *>(&value);
        auto       result_ptr = reinterpret_cast<uint8_t *>(&result);

        for (std::size_t i = 0; i < sizeof(T); ++i) {
            result_ptr[i] = value_ptr[sizeof(T) - i - 1];
        }

        return result;
    }

    template<std::endian targetEndianness = std::endian::little, typename T>
    constexpr T swapEndian(T value) const noexcept {
        if constexpr (targetEndianness == std::endian::native || sizeof(T) == 1LU) {
            return value;
        }
        if constexpr (std::is_integral_v<T>) {
            if constexpr (targetEndianness != std::endian::native) {
                return swapBytes(value);
            }
            return value;
        } else if constexpr (std::is_floating_point_v<T>) {
            using IntType = std::conditional_t<sizeof(T) == sizeof(float), std::uint32_t, std::uint64_t>;
            if constexpr (targetEndianness != std::endian::native) {
                return std::bit_cast<T>(swapBytes(std::bit_cast<IntType>(value)));
            }
            return value;
        } else {
            static_assert(always_false<T>, "swapEndian only supports integral and floating-point types");
        }
    }

    template<Number I>
    constexpr void put(const std::span<const I> &values) noexcept {
        const std::size_t byteToCopy = values.size() * sizeof(I);
        reserve_spare(byteToCopy + sizeof(int32_t) + sizeof(I));
        if constexpr ((std::endian::native == std::endian::little) && !is_same_v<I, bool>) {
            put(static_cast<int32_t>(values.size())); // size of vector

            std::memmove((_buffer + _size), values.data(), byteToCopy);
            _size += byteToCopy;
        } else {
            put(swapEndian(static_cast<int32_t>(values.size()))); // size of vector

            for (std::size_t i = 0U; i < values.size(); i++) {
                put(swapEndian(values[i]));
            }
        }
    }

    template<StringLike I>
    constexpr void put(const std::span<const I> &values) noexcept {
        reserve_spare(values.size() * 25 + sizeof(int32_t) + sizeof(char)); // educated guess
        put(swapEndian(static_cast<int32_t>(values.size())));               // size of vector
        for (std::size_t i = 0U; i < values.size(); i++) {
            put(values[i]);
        }
    }

    std::string_view extractStringView() const noexcept {
        const auto strLength = static_cast<std::size_t>(swapEndian(get<int32_t>()) - 1);
        auto       start     = reinterpret_cast<const char *>(_buffer + _position);
        _position += strLength + 1LU; // +1 for the zero-terminating byte
        return { start, strLength };
    }

    template<MetaInfo meta = WITH, typename Container>
    OPENCMW_FORCEINLINE constexpr void putBoolContainer(const Container &values) noexcept {
        const std::size_t size       = values.size();
        const std::size_t byteToCopy = size * sizeof(bool);
        if constexpr (meta == WITH) {
            reserve_spare(byteToCopy + sizeof(int32_t));
            put(static_cast<int32_t>(size)); // size of vector/array
        } else {
            reserve_spare(byteToCopy);
        }
        std::ranges::for_each(values, [&](const auto &v) { put<meta, bool>(v); });
    }

public:
    [[nodiscard]] explicit IoBuffer(const std::size_t initialCapacity, Allocator allocator = Allocator(Reallocator::defaultReallocator())) noexcept
        : _allocator{ allocator } {
        if (initialCapacity > 0) {
            reallocate(initialCapacity);
        }
    }

    [[nodiscard]] IoBuffer() noexcept
        : IoBuffer(std::size_t{ 0 }) {}

    [[nodiscard]] explicit IoBuffer(const char *data)
        : IoBuffer(data, data ? std::strlen(data) : 0) {}

    [[nodiscard]] explicit IoBuffer(const char *data, const std::size_t size, Allocator allocator = Allocator(Reallocator::defaultReallocator()))
        : IoBuffer(size, allocator) {
        _buffer = resize(size);
        std::memmove(_buffer, data, _size);
    }

    [[nodiscard]] explicit IoBuffer(std::span<uint8_t> data, Allocator allocator = ThrowingAllocator::defaultNonOwning())
        : _allocator(allocator), _buffer(data.data()), _size(data.size()), _capacity(data.size()) {}

    [[nodiscard]] explicit IoBuffer(std::span<uint8_t> data, bool owning)
        : IoBuffer(data, Allocator(owning ? ThrowingAllocator::defaultOwning()
                                          : ThrowingAllocator::defaultNonOwning())) {};

    [[nodiscard]] explicit IoBuffer(uint8_t *data, std::size_t size, Allocator allocator = ThrowingAllocator::defaultNonOwning())
        : IoBuffer({ data, size }, allocator) {};

    [[nodiscard]] explicit IoBuffer(uint8_t *data, std::size_t size, bool owning)
        : IoBuffer({ data, size }, Allocator(owning ? ThrowingAllocator::defaultOwning() : ThrowingAllocator::defaultNonOwning())) {};

    [[nodiscard]] IoBuffer(const IoBuffer& other) noexcept
      : IoBuffer(other._capacity, (dynamic_cast<ThrowingAllocator*>(other._allocator.resource()) != nullptr)
                ? other._allocator.select_on_container_copy_construction(): Allocator(other._allocator.resource()))
    {
        _buffer   = resize(other._size);
        _position = other._position;
        std::memmove(_buffer, other._buffer, _size * sizeof(uint8_t));
    }

    [[nodiscard]] IoBuffer(IoBuffer &&other) noexcept
        : _allocator(other._allocator.resource())
        , _buffer(std::exchange(other._buffer, nullptr))
        , _position(other._position)
        , _size(other._size)
        , _capacity(other._capacity) {}

    constexpr ~IoBuffer() {
        freeInternalBuffer();
    }

    constexpr void swap(IoBuffer &other) {
        std::swap(_position, other._position);
        std::swap(_size, other._size);
        std::swap(_capacity, other._capacity);
        std::swap(_buffer, other._buffer);
    }

    IoBuffer &operator=(const IoBuffer &other) {
        auto temp = other;
        swap(temp);
        return *this;
    }

    IoBuffer &operator=(IoBuffer &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        auto temp = std::move(other);
        swap(temp);
        return *this;
    }

    constexpr uint8_t                         &operator[](const std::size_t i) noexcept { return _buffer[i]; }
    constexpr const uint8_t                   &operator[](const std::size_t i) const noexcept { return _buffer[i]; }
    constexpr void                             reset() noexcept { _position = 0; }
    constexpr void                             set_position(size_t position) noexcept { _position = position; }
    [[nodiscard]] constexpr const std::size_t &position() const noexcept { return _position; }
    [[nodiscard]] constexpr const std::size_t &capacity() const noexcept { return _capacity; }
    [[nodiscard]] constexpr const std::size_t &size() const noexcept { return _size; }
    [[nodiscard]] constexpr bool               empty() const noexcept { return _size == 0; }
    [[nodiscard]] constexpr uint8_t           *data() noexcept { return _buffer; }
    [[nodiscard]] constexpr const uint8_t     *data() const noexcept { return _buffer; }
    constexpr void                             clear() noexcept { _position = _size = 0; }

    template<bool checkRange = true>
    OPENCMW_FORCEINLINE constexpr void skip(int bytes) noexcept(!checkRange) {
        if constexpr (checkRange) {
            if (_position + static_cast<std::size_t>(bytes) > size()) { // catches both over and underflow
                throw std::out_of_range(std::format("requested index {} is out-of-range [0,{}]", static_cast<std::ptrdiff_t>(_position) + bytes, _size));
            }
        }
        _position += static_cast<std::size_t>(bytes);
    }

    template<typename R, bool checkRange = true>
    OPENCMW_FORCEINLINE constexpr R &at(const size_t index) noexcept(!checkRange) {
        if constexpr (checkRange) {
            if (index >= _size) {
                throw std::out_of_range(std::format("requested index {} is out-of-range [0,{}]", index, _size));
            }
        }
        return *(reinterpret_cast<R *>(_buffer + index));
    }

    constexpr ByteBufferPointer reserve(const std::size_t minCapacity) noexcept {
        if (minCapacity < _capacity) {
            return _buffer;
        }
        return reallocate(minCapacity);
    }

    constexpr void shrink_to_fit() { reallocate(_size); }

    constexpr void reserve_spare(const std::size_t additionalCapacity) noexcept {
        if (additionalCapacity >= (_capacity - _size)) {
            reserve((_size + additionalCapacity) << 2U); // TODO: experiment with auto-grow parameter
        }
    }

    constexpr ByteBufferPointer resize(const std::size_t newSize) noexcept {
        if (newSize > _capacity) {
            reserve(newSize);
        }
        _size = newSize;
        return _buffer;
    }

    template<MetaInfo meta = WITH, Number I>
    OPENCMW_FORCEINLINE constexpr void put(const I &value) noexcept {
        constexpr std::size_t byteToCopy = sizeof(I);
        reserve_spare(byteToCopy);

        std::memcpy(_buffer + _size, &value, byteToCopy);
        _size += byteToCopy;
    }

    template<MetaInfo meta = WITH, StringLike I>
    OPENCMW_FORCEINLINE constexpr void put(const I &value) noexcept {
        const std::size_t bytesToCopy = value.size() * sizeof(char);
        reserve_spare(bytesToCopy + sizeof(int32_t) + sizeof(char)); // educated guess
        if constexpr (meta == WITH) {
            put(static_cast<int32_t>(value.size() + 1)); // size of vector plus string termination
            std::memmove((_buffer + _size), value.data(), bytesToCopy);
            _size += bytesToCopy;
            put(static_cast<uint8_t>('\0')); // zero terminating byte
        } else {
            std::memmove((_buffer + _size), value.data(), bytesToCopy);
            _size += bytesToCopy;
            *(reinterpret_cast<char *>(_buffer + _size)) = '\0'; // zero terminating byte (additional safety)
        }
    }

    template<SupportedType I, size_t size>
    OPENCMW_FORCEINLINE constexpr void put(I const (&values)[size]) noexcept { put(std::span<const I>(values, size)); } // NOLINT int a[30]; OK <-> std::array<int, 30>
    template<SupportedType I>
    OPENCMW_FORCEINLINE constexpr void put(std::vector<I> const &values) noexcept { put(std::span<const I>(values.data(), values.size())); }
    template<SupportedType I, size_t size>
    OPENCMW_FORCEINLINE constexpr void put(std::array<I, size> const &values) noexcept { put(std::span<const I>(values.data(), values.size())); }

    template<MetaInfo meta = WITH>
    OPENCMW_FORCEINLINE constexpr void put(std::vector<bool> const &values) noexcept {
        putBoolContainer<meta>(values);
    }

    template<MetaInfo meta = WITH, size_t size>
    OPENCMW_FORCEINLINE constexpr void put(std::array<bool, size> const &values) noexcept {
        putBoolContainer<meta>(values);
    }

    template<bool checkRange = true>
    [[nodiscard]] OPENCMW_FORCEINLINE constexpr std::string_view asString(const size_t index = 0U, const int requestedSize = -1) const noexcept(!checkRange) {
        const auto unsigned_size = static_cast<std::size_t>(requestedSize);
        if constexpr (checkRange) {
            if (index > _size) {
                throw std::out_of_range(std::format("requested index {} is out-of-range [0,{}]", index, _size));
            }
            if (requestedSize >= 0 && (index + unsigned_size) > _size) {
                throw std::out_of_range(std::format("requestedSize {} is out-of-range {} -> [0,{}]", requestedSize, index, index + unsigned_size, _size));
            }
        }
        if (requestedSize < 0) {
            return { reinterpret_cast<const char *>(data() + index), _size - index };
        }
        return { reinterpret_cast<const char *>(data() + index), std::min(_size - index, unsigned_size) };
    }

    template<typename R, typename RawType = std::remove_cvref_t<R>>
    [[nodiscard]] OPENCMW_FORCEINLINE constexpr R get() const noexcept {
        if constexpr (Number<R>) {
            const std::size_t localPosition = _position;
            _position += sizeof(R);
            R value;
            std::memcpy(&value, _buffer + localPosition, sizeof(R));
            return value;
        } else if constexpr (units::is_derived_from_specialization_of<RawType, std::basic_string>) {
            return std::string(extractStringView());
        } else if constexpr (units::is_derived_from_specialization_of<RawType, std::basic_string_view>) {
            return std::string_view(extractStringView());
        } else {
            static_assert(always_false<R>, "Unsupported type for get()");
        }
    }

    template<ArrayOrVector Container, bool checkRange = false>
    OPENCMW_FORCEINLINE constexpr Container &getArray(Container &input, const std::size_t requestedSize = SIZE_MAX) const noexcept(!checkRange) {
        using R                        = Container::value_type;
        const auto        arraySize    = std::min(static_cast<std::size_t>(get<int32_t>()), _size - _position);
        const std::size_t minArraySize = std::min(arraySize, requestedSize);

        if constexpr (opencmw::is_vector<Container>) {
            input.resize(minArraySize);
        } else if constexpr (opencmw::is_array<Container>) {
            if constexpr (checkRange) {
                if (std::tuple_size_v<Container> < minArraySize) {
                    throw std::out_of_range(std::format("std::array<SupportedType, size = {}> wire-format size does not match design {}", std::tuple_size_v<Container>, minArraySize));
                }
            }
            assert(std::tuple_size_v<Container> >= minArraySize && "std::array<SupportedType, size> wire-format size does not match design");
        }

        if constexpr (std::is_trivially_copyable_v<R> && !std::is_same_v<R, bool> && !opencmw::StringLike<R>) {
            std::memcpy(input.data(), _buffer + _position, minArraySize * sizeof(R));
            _position += minArraySize * sizeof(R);
        } else {
            for (auto i = 0U; i < minArraySize; i++) {
                input[i] = get<R>();
            }
        }

        return input;
    }

    template<SupportedType R>
    OPENCMW_FORCEINLINE constexpr std::vector<R> getArray(std::vector<R> &&input = std::vector<R>(), const std::size_t requestedSize = SIZE_MAX) noexcept { return getArray(input, requestedSize); }
    template<SupportedType R, std::size_t size>
    [[maybe_unused]] OPENCMW_FORCEINLINE constexpr std::array<R, size> getArray(std::array<R, size> &&input = std::array<R, size>(), const std::size_t requestedSize = size) noexcept { return getArray(input, requestedSize); }

    template<StringArray R, typename T = typename R::value_type>
    [[nodiscard]] OPENCMW_FORCEINLINE constexpr R &get(R &input, const std::size_t requestedSize = SIZE_MAX) noexcept { return getArray(input, requestedSize); }
    template<StringArray R, typename T = typename R::value_type>
    [[nodiscard]] OPENCMW_FORCEINLINE constexpr R get(R &&input = R(), const std::size_t requestedSize = SIZE_MAX) noexcept { return getArray(std::forward<R>(input), requestedSize); }
};

} // namespace opencmw

template<>
struct std::formatter<opencmw::IoBuffer> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin(); // not (yet) implemented
    }

    template<typename FormatContext>
    auto format(const opencmw::IoBuffer &v, FormatContext &ctx) const {
        return std::format_to(ctx.out(), "{}", v.asString());
    }
};

#pragma clang diagnostic pop
#endif // OPENCMW_IOBUFFER_H
