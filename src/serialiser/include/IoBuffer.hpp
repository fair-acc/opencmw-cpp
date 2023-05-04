#ifndef OPENCMW_IOBUFFER_H
#define OPENCMW_IOBUFFER_H
#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-owning-memory"
#pragma ide diagnostic   ignored "UnreachableCode" // -- allow for alternate non-c-style memory management

#include "MultiArray.hpp"
#include "opencmw.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cassert>
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

class IoBuffer {
private:
    using Allocator               = std::pmr::polymorphic_allocator<uint8_t>;
    mutable std::size_t _position = 0;
    std::size_t         _size     = 0;
    std::size_t         _capacity = 0;
    uint8_t            *_buffer   = nullptr;
    Allocator           _allocator{};

    constexpr void      reallocate(const std::size_t &size) noexcept {
        if (size == 0) {
            freeInternalBuffer();
            _capacity = size;
            return;
        } else if (_capacity == size && _buffer != nullptr) {
            return;
        } else if (_buffer == nullptr) {
            _buffer   = _allocator.allocate(size);
            _capacity = size;
            return;
        }
        if (dynamic_cast<Reallocator *>(_allocator.resource())) {
            // N.B. 'realloc' is safe as long as the de-allocation is done via 'free'
            _buffer   = static_cast<uint8_t *>(realloc(_buffer, size * sizeof(uint8_t)));
            _capacity = size;
        } else {
            // buffer already exists - copy existing content into newly allocated buffer N.B. maybe larger/smaller
            auto *tBuffer = _allocator.allocate(size);
            // std::memmove(tBuffer, _buffer, std::min(_size, size) * sizeof(uint8_t));
            std::copy(_buffer, _buffer + std::min(_size, size) * sizeof(uint8_t), tBuffer);
            _allocator.deallocate(_buffer, _capacity);
            _buffer   = tBuffer;
            _capacity = size;
        }
    }

    constexpr void freeInternalBuffer() {
        if (_buffer != nullptr) {
            _allocator.deallocate(_buffer, _capacity);
        } else {
            // throw std::runtime_error("double free"); //TODO: reenable
        }
        _buffer = nullptr;
    }

    template<Number I>
    constexpr void put(const I *values, const std::size_t size) noexcept { // int* a; --> need N
        const std::size_t byteToCopy = size * sizeof(I);
        reserve_spare(byteToCopy + sizeof(int32_t) + sizeof(I));
        put(static_cast<int32_t>(size)); // size of vector
        if constexpr (is_same_v<I, bool>) {
            for (std::size_t i = 0U; i < size; i++) {
                put<bool>(values[i]);
            }
        } else {
            std::memmove((_buffer + _size), values, byteToCopy);
            _size += byteToCopy;
        }
    }

    template<StringLike I>
    constexpr void put(const I *values, const std::size_t size) noexcept {
        reserve_spare(size * 25 + sizeof(int32_t) + sizeof(char)); // educated guess
        put(static_cast<int32_t>(size));                           // size of vector
        for (std::size_t i = 0U; i < size; i++) {
            put(values[i]);
        }
    }

public:
    [[nodiscard]] explicit IoBuffer(const std::size_t &initialCapacity = 0, Allocator allocator = Allocator(Reallocator::defaultReallocator())) noexcept
        : _allocator{ allocator } {
        if (initialCapacity > 0) {
            reallocate(initialCapacity);
        }
    }

    [[nodiscard]] explicit IoBuffer(const char *data)
        : IoBuffer(data, data ? std::strlen(data) : 0) {}

    [[nodiscard]] explicit IoBuffer(const char *data, const std::size_t size, Allocator allocator = Allocator(Reallocator::defaultReallocator()))
        : IoBuffer(size, allocator) {
        resize(size);
        std::memmove(_buffer, data, _size);
    }

    [[nodiscard]] explicit IoBuffer(std::span<uint8_t> data, Allocator allocator = ThrowingAllocator::defaultNonOwning())
        : _size(data.size()), _capacity(data.size()), _buffer(data.data()), _allocator(allocator) {}

    [[nodiscard]] explicit IoBuffer(std::span<uint8_t> data, bool owning)
        : IoBuffer(data, Allocator(owning ? ThrowingAllocator::defaultOwning()
                                          : ThrowingAllocator::defaultNonOwning())){};

    [[nodiscard]] explicit IoBuffer(uint8_t *data, std::size_t size, Allocator allocator = ThrowingAllocator::defaultNonOwning())
        : IoBuffer({ data, size }, allocator){};

    [[nodiscard]] explicit IoBuffer(uint8_t *data, std::size_t size, bool owning)
        : IoBuffer({ data, size }, Allocator(owning ? ThrowingAllocator::defaultOwning() : ThrowingAllocator::defaultNonOwning())){};

    [[nodiscard]] IoBuffer(const IoBuffer &other) noexcept
        : IoBuffer(other._capacity, other._allocator.select_on_container_copy_construction()) {
        resize(other._size);
        _position = other._position;
        std::memmove(_buffer, other._buffer, _size * sizeof(uint8_t));
    }

    [[nodiscard]] IoBuffer(IoBuffer &&other) noexcept
        : _allocator(other._allocator.select_on_container_copy_construction()) {
        resize(other._size);
        _position     = other._position;
        _capacity     = other.capacity();
        _buffer       = other._buffer;
        other._buffer = nullptr;
    }

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
        auto temp = std::move(other);
        swap(temp);
        return *this;
    }

    enum MetaInfo {
        WITH,
        WITHOUT
    };

    forceinline constexpr uint8_t                         &operator[](const std::size_t i) noexcept { return _buffer[i]; }
    forceinline constexpr const uint8_t                   &operator[](const std::size_t i) const noexcept { return _buffer[i]; }
    forceinline constexpr void                             reset() noexcept { _position = 0; }
    forceinline constexpr void                             set_position(size_t position) noexcept { _position = position; }
    [[nodiscard]] forceinline constexpr const std::size_t &position() const noexcept { return _position; }
    [[nodiscard]] forceinline constexpr const std::size_t &capacity() const noexcept { return _capacity; }
    [[nodiscard]] forceinline constexpr const std::size_t &size() const noexcept { return _size; }
    [[nodiscard]] forceinline constexpr bool               empty() const noexcept { return _size == 0; }
    [[nodiscard]] forceinline constexpr uint8_t           *data() noexcept { return _buffer; }
    [[nodiscard]] forceinline constexpr const uint8_t     *data() const noexcept { return _buffer; }
    constexpr void                                         clear() noexcept { _position = _size = 0; }

    template<bool checkRange = true>
    forceinline constexpr void skip(int bytes) noexcept(!checkRange) {
        if constexpr (checkRange) {
            if (_position + static_cast<std::size_t>(bytes) > size()) { // catches both over and underflow
                throw std::out_of_range(fmt::format("requested index {} is out-of-range [0,{}]", static_cast<std::ptrdiff_t>(_position) + bytes, _size));
            }
        }
        _position += static_cast<std::size_t>(bytes);
    }

    template<typename R, bool checkRange = true>
    forceinline constexpr R &at(const size_t &index) noexcept(!checkRange) {
        if constexpr (checkRange) {
            if (index >= _size) {
                throw std::out_of_range(fmt::format("requested index {} is out-of-range [0,{}]", index, _size));
            }
        }
        return *(reinterpret_cast<R *>(_buffer + index));
    }

    constexpr void reserve(const std::size_t &minCapacity) noexcept {
        if (minCapacity < _capacity) {
            return;
        }
        reallocate(minCapacity);
    }

    constexpr void shrink_to_fit() { reallocate(_size); }

    constexpr void reserve_spare(const std::size_t &additionalCapacity) noexcept {
        if (additionalCapacity >= (_capacity - _size)) {
            reserve((_size + additionalCapacity) << 2U); // TODO: experiment with auto-grow parameter
        }
    }

    constexpr void resize(const std::size_t &size) noexcept {
        if (size == _size) {
            return;
        }
        _size = size;
        if (size < _capacity) {
            return;
        }
        reserve(size);
    }

    template<MetaInfo meta = WITH, Number I>
    forceinline constexpr void put(const I &value) noexcept {
        constexpr std::size_t byteToCopy = sizeof(I);
        reserve_spare(byteToCopy);
        *(reinterpret_cast<I *>(_buffer + _size)) = value;
        _size += byteToCopy;
    }

    template<MetaInfo meta = WITH, StringLike I>
    forceinline constexpr void put(const I &value) noexcept {
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
    forceinline constexpr void put(I const (&values)[size]) noexcept { put(values, size); } // NOLINT int a[30]; OK <-> std::array<int, 30>
    template<SupportedType I>
    forceinline constexpr void put(std::vector<I> const &values) noexcept { put(values.data(), values.size()); }
    template<SupportedType I, size_t size>
    forceinline constexpr void put(std::array<I, size> const &values) noexcept { put(values.data(), size); }

    template<MetaInfo meta = WITH>
    forceinline void put(std::vector<bool> const &values) noexcept { // TODO: re-enable constexpr (N.B. should be since C++20)
        const std::size_t size       = values.size();
        const std::size_t byteToCopy = size * sizeof(bool);
        if constexpr (meta == WITH) {
            reserve_spare(byteToCopy + sizeof(int32_t));
            put(static_cast<int32_t>(size)); // size of vector
        } else {
            reserve_spare(byteToCopy);
        }
        std::for_each(values.begin(), values.end(), [&](const auto &v) { put<meta, bool>(v); });
    }

    template<MetaInfo meta = WITH, size_t size>
    forceinline constexpr void put(std::array<bool, size> const &values) noexcept {
        constexpr std::size_t byteToCopy = size * sizeof(bool);
        if constexpr (meta == WITH) {
            reserve_spare(byteToCopy + sizeof(int32_t));
            put(static_cast<int32_t>(size)); // size of vector
        } else {
            reserve_spare(byteToCopy);
        }
        std::for_each(values.begin(), values.end(), [&](const auto &v) { put<meta, bool>(v); });
    }

    template<bool checkRange = true>
    [[nodiscard]] forceinline constexpr std::string_view asString(const size_t index = 0U, const int requestedSize = -1) noexcept(!checkRange) {
        const auto unsigned_size = static_cast<std::size_t>(requestedSize);
        if constexpr (checkRange) {
            if (index > _size) {
                throw std::out_of_range(fmt::format("requested index {} is out-of-range [0,{}]", index, _size));
            }
            if (requestedSize >= 0 && (index + unsigned_size) > _size) {
                throw std::out_of_range(fmt::format("requestedSize {} is out-of-range {} -> [0,{}]", requestedSize, index, index + unsigned_size, _size));
            }
        }
        if (requestedSize < 0) {
            return { reinterpret_cast<char *>(data() + index), _size - index };
        }
        return { reinterpret_cast<char *>(data() + index), std::min(_size - index, unsigned_size) };
    }

    template<bool checkRange = true>
    [[nodiscard]] forceinline constexpr std::string_view asString(const size_t index = 0U, const int requestedSize = -1) const noexcept(!checkRange) {
        const auto unsigned_size = static_cast<std::size_t>(requestedSize);
        if constexpr (checkRange) {
            if (index > _size) {
                throw std::out_of_range(fmt::format("requested index {} is out-of-range [0,{}]", index, _size));
            }
            if (requestedSize >= 0 && (index + unsigned_size) > _size) {
                throw std::out_of_range(fmt::format("requestedSize {} is out-of-range {} -> [0,{}]", requestedSize, index, index + unsigned_size, _size));
            }
        }
        if (requestedSize < 0) {
            return { reinterpret_cast<const char *>(data() + index), _size - index };
        }
        return { reinterpret_cast<const char *>(data() + index), std::min(_size - index, unsigned_size) };
    }

    template<Number R>
    [[maybe_unused]] forceinline constexpr R get() noexcept {
        const std::size_t localPosition = _position;
        _position += sizeof(R);
        return get<R>(localPosition);
    }

    template<SupportedType R>
    [[maybe_unused]] forceinline constexpr R get(const std::size_t &index) noexcept {
        return *(reinterpret_cast<R *>(_buffer + index));
    }

    template<StringLike R>
    [[nodiscard]] forceinline R get() noexcept {
        const std::size_t bytesToCopy = std::min(static_cast<std::size_t>(get<int32_t>()) * sizeof(char), _size - _position);
        const std::size_t oldPosition = _position;
        _position += bytesToCopy;
        return R((reinterpret_cast<const char *>(_buffer + oldPosition)), bytesToCopy - 1);
    }

    template<StringLike R>
    [[nodiscard]] forceinline R get(const std::size_t &index) noexcept {
        const std::size_t bytesToCopy = std::min(static_cast<std::size_t>(get<int32_t>()) * sizeof(char), _size - _position);
        _position += bytesToCopy - 1;
        get<int8_t>();
        return R((reinterpret_cast<const char *>(_buffer + index + sizeof(int32_t))), bytesToCopy);
    }

    template<SupportedType R>
    forceinline constexpr std::vector<R> &getArray(std::vector<R> &input, const std::size_t &requestedSize = SIZE_MAX) noexcept {
        const auto        arraySize    = std::min(static_cast<std::size_t>(get<int32_t>()), _size - _position);
        const std::size_t minArraySize = std::min(arraySize, requestedSize);
        input.resize(minArraySize);
        if constexpr (is_stringlike<R> || is_same_v<R, bool>) {
            for (auto i = 0U; i < minArraySize; i++) {
                input[i] = get<R>();
            }
        } else {
            std::memmove(input.data(), (reinterpret_cast<const R *>(_buffer + _position)), minArraySize * sizeof(R));
            _position += arraySize * sizeof(R);
        }
        return input;
    }

    template<SupportedType R>
    forceinline constexpr std::vector<R> getArray(std::vector<R> &&input = std::vector<R>(), const std::size_t &requestedSize = SIZE_MAX) noexcept { return getArray<R>(input, requestedSize); }

    template<SupportedType R, std::size_t size>
    forceinline constexpr std::array<R, size> &getArray(std::array<R, size> &input, const std::size_t &requestedSize = SIZE_MAX) noexcept {
        const auto        arraySize    = std::min(static_cast<std::size_t>(get<int32_t>()), _size - _position);
        const std::size_t minArraySize = std::min(arraySize, requestedSize);
        assert(size >= minArraySize && "std::array<SupportedType, size> wire-format size does not match design");
        if constexpr (is_stringlike<R> || is_same_v<R, bool>) {
            for (auto i = 0U; i < minArraySize; i++) {
                input[i] = get<R>();
            }
        } else {
            std::memmove(input.data(), (reinterpret_cast<const R *>(_buffer + _position)), minArraySize * sizeof(R));
            _position += arraySize * sizeof(R);
        }
        return input;
    }

    template<SupportedType R, std::size_t size>
    [[maybe_unused]] forceinline constexpr std::array<R, size> getArray(std::array<R, size> &&input = std::array<R, size>(), const std::size_t &requestedSize = size) noexcept { return getArray<R, size>(input, requestedSize); }

    template<StringArray R, typename T = typename R::value_type>
    [[nodiscard]] forceinline constexpr R &get(R &input, const std::size_t &requestedSize = SIZE_MAX) noexcept { return getArray<T>(input, requestedSize); }
    template<StringArray R, typename T = typename R::value_type>
    [[nodiscard]] forceinline constexpr R get(R &&input = R(), const std::size_t &requestedSize = SIZE_MAX) noexcept { return getArray<T>(input, requestedSize); }
    template<StringArray R, typename T = typename R::value_type, std::size_t size>
    [[nodiscard]] forceinline constexpr R &get(R &input, const std::size_t &requestedSize = size) noexcept { return getArray<T, size>(input, requestedSize); }
    template<StringArray R, typename T = typename R::value_type, std::size_t size>
    [[nodiscard]] forceinline constexpr R get(R &&input = R(), const std::size_t &requestedSize = size) noexcept { return getArray<T, size>(input, requestedSize); }
};

} // namespace opencmw

template<>
struct fmt::formatter<opencmw::IoBuffer> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin(); // not (yet) implemented
    }

    template<typename FormatContext>
    auto format(const opencmw::IoBuffer &v, FormatContext &ctx) const {
        return fmt::format_to(ctx.out(), "{}", v.asString());
    }
};

#pragma clang diagnostic pop
#endif // OPENCMW_IOBUFFER_H
