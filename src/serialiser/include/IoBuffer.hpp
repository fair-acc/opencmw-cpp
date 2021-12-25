#ifndef OPENCMW_IOBUFFER_H
#define OPENCMW_IOBUFFER_H
#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-owning-memory"
#pragma ide diagnostic   ignored "UnreachableCode" // -- allow for alternate non-c-style memory management

#include "MultiArray.hpp"
#include "opencmw.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace opencmw {

class IoBuffer {
private:
    static const bool   _alloc_optimisations = true;
    mutable std::size_t _position            = 0;
    std::size_t         _size                = 0;
    std::size_t         _capacity            = 0;
    uint8_t            *_buffer              = nullptr;

    constexpr void      reallocate(const std::size_t &size) noexcept {
        if (_capacity == size && _buffer != nullptr) {
            return;
        }
        _capacity = size;
        if constexpr (_alloc_optimisations) {
            if (_capacity == 0) {
                freeInternalBuffer();
                return;
            }
            // N.B. 'realloc' is safe as long as the de-allocation is done via 'free'
            _buffer = static_cast<uint8_t *>(realloc(_buffer, _capacity * sizeof(uint8_t)));
        } else {
            if (_buffer == nullptr) {
                _buffer = new uint8_t[_capacity];
            }
            // buffer already exists - copy existing content into newly allocated buffer N.B. maybe larger/smaller
            uint8_t *tBuffer = new uint8_t[_capacity];
            // std::memmove(tBuffer, _buffer, std::min(_size, size) * sizeof(uint8_t));
            std::copy(_buffer, _buffer + std::min(_size, size) * sizeof(uint8_t), tBuffer);
            delete[] _buffer;
            _buffer = tBuffer;
        }
    }

    constexpr void freeInternalBuffer() {
        if (_buffer != nullptr) {
            if constexpr (_alloc_optimisations) {
                free(_buffer);
            } else {
                delete[] _buffer;
            }
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
    [[nodiscard]] constexpr explicit IoBuffer(const std::size_t &initialCapacity = 0) noexcept {
        if (initialCapacity > 0) {
            reallocate(initialCapacity);
        }
    }

    [[nodiscard]] constexpr IoBuffer(const IoBuffer &other) noexcept
        : IoBuffer(other._capacity) {
        resize(other._size);
        _position = other._position;
        std::memmove(_buffer, other._buffer, _size * sizeof(uint8_t));
    }

    [[nodiscard]] constexpr IoBuffer(IoBuffer &&other) noexcept
        : IoBuffer(other._capacity) {
        resize(other._size);
        _position     = other._position;
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

    [[nodiscard]] constexpr IoBuffer &operator=(const IoBuffer &other) {
        auto temp = other;
        swap(temp);
        return *this;
    }

    [[nodiscard]] constexpr IoBuffer &operator=(IoBuffer &&other) noexcept {
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
    [[nodiscard]] forceinline constexpr uint8_t           *data() noexcept { return _buffer; }
    [[nodiscard]] forceinline constexpr const uint8_t     *data() const noexcept { return _buffer; }
    constexpr void                                         clear() noexcept { _position = _size = 0; }

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

    template<MetaInfo meta = WITHOUT, Number I>
    forceinline constexpr void put(const I &value) noexcept {
        constexpr std::size_t byteToCopy = sizeof(I);
        reserve_spare(byteToCopy);
        *(reinterpret_cast<I *>(_buffer + _size)) = value;
        _size += byteToCopy;
    }

    template<MetaInfo meta = WITH, StringLike I>
    forceinline constexpr void put(const I &value) noexcept {
        const std::size_t bytesToCopy = value.size() * sizeof(char);
        if constexpr (meta == WITH) {
            reserve_spare(bytesToCopy + sizeof(int32_t) + sizeof(char)); // educated guess
            put(static_cast<int32_t>(value.size() + 1));                 // size of vector plus string termination
            std::memmove((_buffer + _size), value.data(), bytesToCopy);
            _size += bytesToCopy;
            put(static_cast<uint8_t>('\0')); // zero terminating byte
        } else {
            reserve_spare(bytesToCopy);
            std::memmove((_buffer + _size), value.data(), bytesToCopy);
            _size += bytesToCopy;
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

    [[nodiscard]] forceinline std::string_view asString() noexcept {
        return { reinterpret_cast<char *>(data()), _size };
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
    forceinline constexpr std::vector<R> &getArray(std::vector<R> &&input = std::vector<R>(), const std::size_t &requestedSize = SIZE_MAX) noexcept { return getArray<R>(input, requestedSize); }

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
    [[maybe_unused]] forceinline constexpr std::array<R, size> &getArray(std::array<R, size> &&input = std::array<R, size>(), const std::size_t &requestedSize = size) noexcept { return getArray<R, size>(input, requestedSize); }

    template<StringArray R, typename T = typename R::value_type>
    [[nodiscard]] forceinline constexpr R &get(R &input, const std::size_t &requestedSize = SIZE_MAX) noexcept { return getArray<T>(input, requestedSize); }
    template<StringArray R, typename T = typename R::value_type>
    [[nodiscard]] forceinline constexpr R &get(R &&input = R(), const std::size_t &requestedSize = SIZE_MAX) noexcept { return getArray<T>(input, requestedSize); }
    template<StringArray R, typename T = typename R::value_type, std::size_t size>
    [[nodiscard]] forceinline constexpr R &get(R &input, const std::size_t &requestedSize = size) noexcept { return getArray<T, size>(input, requestedSize); }
    template<StringArray R, typename T = typename R::value_type, std::size_t size>
    [[nodiscard]] forceinline constexpr R &get(R &&input = R(), const std::size_t &requestedSize = size) noexcept { return getArray<T, size>(input, requestedSize); }
};

} // namespace opencmw

#pragma clang diagnostic pop
#endif // OPENCMW_IOBUFFER_H