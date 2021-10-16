#ifndef YAZ_STRING_H
#define YAZ_STRING_H

#include <array>
#include <cstring>
#include <string_view>

#include "Meta.hpp"

namespace yaz {

constexpr std::size_t sized_string_max_capacity = 255;
//
// Class that holds a small string that is O(1) convertible
// to both 0mq and C strings
//
template<std::size_t Capacity = sized_string_max_capacity>
class SizedString {
    static_assert(Capacity > 0 and Capacity <= sized_string_max_capacity, "Capacity needs to be larger than 0 and less than 255");

private:
    // Capacity denotes the largest string that this SizedString
    // can contain. To be compatible with 0mq, _data[0] needs
    // to hold the string length, while to be compatible with C,
    // it needs to be '\0'-terminated
    std::array<unsigned char, Capacity + 2> _data;

public:
    constexpr static const auto capacity = Capacity;

    constexpr SizedString() {
        _data[0] = 0;    // 0mq size is 0
        _data[1] = '\0'; // terminating a C string
    }

    constexpr explicit SizedString(const char *data, std::size_t size) {
        const auto clipped_length = std::min(Capacity, size);
        _data[0]                  = clipped_length;
        std::copy(data, data + clipped_length, &_data[1]);
        _data[clipped_length + 1] = '\0';
    }

    constexpr explicit SizedString(std::string_view other)
        : SizedString(other.data(), other.length()) {
    }

    [[nodiscard]] constexpr explicit operator std::string_view() const {
        return std::string_view(c_str(), _data[0]);
    }

    [[nodiscard]] const char *c_str() const {
        return reinterpret_cast<const char *>(&_data[1]);
    }

    [[nodiscard]] const char *zmq_str() const {
        return reinterpret_cast<const char *>(_data.data());
    }
};

template<typename T>
concept sized_string_instance = meta::is_value_instantiation_of_v<::yaz::SizedString, T>;

} // namespace yaz

#endif // include guard

