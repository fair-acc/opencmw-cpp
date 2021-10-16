#ifndef YAZ_ERROR_H
#define YAZ_ERROR_H

#include <cerrno>
#include <utility>

#include "Meta.hpp"

namespace yaz {

namespace result_type {
// TODO: refactor to proper policies instead of type tags
class only_zero_valid;
class positive_values_valid;
class error_in_errno;
} // namespace result_type

template<typename T, typename... policies>
class result;

template<typename T>
class [[nodiscard]] result<T, result_type::only_zero_valid> {
public:
    constexpr explicit result(T value)
        : _value{ std::move(value) } {}

    [[nodiscard]] constexpr bool has_value() const {
        return _value == 0;
    }

    constexpr explicit operator bool() const {
        return has_value();
    }

    constexpr T &value() {
        assert(has_value());
        return _value;
    }
    constexpr const T &value() const {
        assert(has_value());
        return _value;
    }

    constexpr T &error() {
        assert(!has_value());
        return _value;
    }
    constexpr const T &error() const {
        assert(!has_value());
        return _value;
    }

private:
    T _value;
};

template<typename T>
class [[nodiscard]] result<T, result_type::positive_values_valid> {
public:
    constexpr explicit result(T value)
        : _value{ std::move(value) } {}

    [[nodiscard]] constexpr bool has_value() const {
        return _value > 0;
    }

    constexpr explicit operator bool() const {
        return _value > 0;
    }

    constexpr T &value() {
        assert(has_value());
        return _value;
    }
    constexpr const T &value() const {
        assert(has_value());
        return _value;
    }

    constexpr T &error() {
        assert(!has_value());
        return _value;
    }
    constexpr const T &error() const {
        assert(!has_value());
        return _value;
    }

private:
    T _value;
};

template<typename T>
class [[nodiscard]] result<T, result_type::positive_values_valid, result_type::error_in_errno> {
public:
    constexpr explicit result(T value)
        : _value{ std::move(value) }, _error{ errno } {}

    [[nodiscard]] constexpr bool has_value() const {
        return _value > 0;
    }

    constexpr explicit operator bool() const {
        return _value > 0;
    }

    constexpr T &value() {
        assert(has_value());
        return _value;
    }
    constexpr const T &value() const {
        assert(has_value());
        return _value;
    }

    constexpr T &error() {
        assert(!has_value());
        return _error;
    }
    constexpr const T &error() const {
        assert(!has_value());
        return _error;
    }

private:
    T   _value;
    int _error;
};

template<typename T>
using positive_result = result<T, result_type::positive_values_valid>;
template<typename T>
using positive_or_errno = result<T, result_type::positive_values_valid, result_type::error_in_errno>;
template<typename T>
using shell_result = result<T, result_type::only_zero_valid>;

template<typename T>
concept result_instance = meta::is_instantiation_of_v<result, T>;

} // namespace yaz

#endif // include guard
