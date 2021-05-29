#ifndef OPENCMW_H
#define OPENCMW_H
//#include "../cmake-build-debug/_deps/refl-cpp-src/refl.hpp"
#include <MultiArray.hpp> // TODO: resolve dangerous circular dependency
#include <fmt/color.h>
#include <fmt/format.h>
#include <refl.hpp>
#include <set>

#define REFL_CUSTOM(TypeName, ...) \
    REFL_TYPE(TypeName) \
    REFL_DETAIL_FOR_EACH(REFL_DETAIL_EX_1_field, __VA_ARGS__) \
    REFL_END

namespace opencmw {
template<typename T>
constexpr bool isStdType() {
    using Type = std::decay<T>::type;
    return get_name(refl::reflect<Type>()).template substr<0, 5>() == "std::";
}

template<class T>
constexpr bool isReflectableClass() {
    using Type = std::decay<T>::type;
    if constexpr (std::is_class<Type>::value && refl::is_reflectable<Type>() && !std::is_fundamental<Type>::value && !std::is_array<Type>::value) {
        return !isStdType<Type>(); // N.B. check this locally since this is not constexpr (yet)
    }
    return false;
}

template<class T>
concept ReflectableClass = isReflectableClass<T>();

template<typename T>
struct is_supported_number {
    using Tp = std::decay<T>::type;
    static const bool value = std::is_same<Tp, uint8_t>::value || std::is_same<Tp, int8_t>::value || std::is_same<Tp, int16_t>::value //
                           || std::is_same<Tp, int32_t>::value || std::is_same<Tp, int64_t>::value || std::is_same<Tp, float>::value || std::is_same<Tp, double>::value;
};

template<typename T>
concept Number = is_supported_number<T>::value;

template<typename T>
constexpr bool isStringLike() {
    using Tp = std::decay<T>::type;
    return std::is_same<Tp, std::string>::value || std::is_same<Tp, std::string_view>::value;
}

template<typename T>
concept StringLike = isStringLike<T>();

template<size_t N>
struct StringLiteral {
    char         value[N + 1]{};
    const size_t size;

    constexpr StringLiteral(const char (&str)[N])
        : size(N) {
        std::copy_n(str, N, value);
        value[N] = '\0';
    }
};

template<typename Test, template<typename...> class Ref>
struct is_specialization : std::false_type {};

template<template<typename...> class Ref, typename... Args>
struct is_specialization<Ref<Args...>, Ref> : std::true_type {};

template<typename T>
struct is_array {
    static const bool value = false;
};

template<typename T, std::size_t N>
struct is_array<std::array<T, N>> {
    static const bool value = true;
};

template<typename T>
struct is_array_or_vector {
    static const bool value = false;
};

template<typename T, typename A>
struct is_array_or_vector<std::vector<T, A>> {
    static const bool value = true;
};

template<typename T, std::size_t N>
struct is_array_or_vector<std::array<T, N>> {
    static const bool value = true;
};

template<typename T>
concept ArrayOrVector = is_array_or_vector<T>::value;

template<typename T>
concept NumberArray = std::is_bounded_array<T>::value; // && is_supported_number<T[]>::value;

/* just some helper function to return nicer human-readable type names */
// clang-format off
template<typename T, typename Tp = std::remove_const<T>::type>
// N.B. extend this for custom classes using type-traits to query nicer class-type name
requires(!std::is_array<Tp>::value && !is_array_or_vector<Tp>::value && !is_multi_array<Tp>::value && !isStringLike<T>())
constexpr const char *typeName() noexcept {
        using namespace std::literals;
        if constexpr (std::is_same<T, std::byte>::value) { return "byte"; }
        if constexpr (std::is_same<T, int8_t>::value) { return "byte"; }
        if constexpr (std::is_same<T, uint8_t>::value) { return "byte"; }
        if constexpr (std::is_same<T, char>::value) { return "char"; }
        if constexpr (std::is_same<T, short>::value) { return "short"; }
        if constexpr (std::is_same<T, int>::value) { return "int"; }
        if constexpr (std::is_same<T, long>::value) { return "long"; }
        if constexpr (std::is_same<T, float>::value) { return "float"; }
        if constexpr (std::is_same<T, double>::value) { return "double"; }
        if constexpr (std::is_same<T, char *>::value) { return "char*"; }

        if constexpr (std::is_same<T, const std::byte>::value) { return "byte const"; }
        if constexpr (std::is_same<T, const char>::value) { return "char const"; }
        if constexpr (std::is_same<T, const short>::value) { return "short const"; }
        if constexpr (std::is_same<T, const int>::value) { return "int const"; }
        if constexpr (std::is_same<T, const long>::value) { return "long const"; }
        if constexpr (std::is_same<T, const float>::value) { return "float const"; }
        if constexpr (std::is_same<T, const double>::value) { return "double const"; }
//        if constexpr (std::is_same<T, const char *>::value) { return "char* const"; }

    if constexpr (refl::is_reflectable<T>()) {
        return refl::reflect<T>().name.data;
    }
    return typeid(T).name();
}
// clang-format on

// clang-format off
template<typename C, typename T = typename C::value_type, std::size_t size = 0>
std::string typeName() noexcept {
    using Cp = std::remove_const<C>::type;
    constexpr std::string_view isConst = std::is_const_v<C> ? " const" : "";

    if constexpr (is_specialization<Cp, std::vector>::value) { return fmt::format("vector<{}>{}", opencmw::typeName<T>(), isConst); }
    if constexpr (is_array<Cp>::value) { return fmt::format("array<{},{}>{}", opencmw::typeName<T>(), size, isConst); } // TODO: improve template to get proper size
    if constexpr (is_specialization<Cp, std::set>::value) { return fmt::format("set<{}>{}", opencmw::typeName<T>(), isConst); }
    if constexpr (is_multi_array<Cp>::value) { return fmt::format("MultiArray<{},{}>{}", opencmw::typeName<T>(), C::n_dims_, isConst); }
    if constexpr (is_specialization<Cp, std::basic_string>::value) { return fmt::format("string{}", isConst); }
    if constexpr (is_specialization<Cp, std::basic_string_view>::value) { return fmt::format("string_view{}", isConst); }

    return fmt::format("CONTAINER<{}>{}", opencmw::typeName<T>(), isConst);
}
// clang-format on

template<NumberArray T, std::size_t size>
std::string typeName() /* const */ noexcept {
    using Type = typename std::remove_all_extents<T>::type;
    return fmt::format("[][{}}]", opencmw::typeName<Type>(), std::to_string(size));
}

template<NumberArray T>
std::string typeName() noexcept {
    using Type = typename std::remove_all_extents<T>::type;
    return fmt::format("{}[?]", opencmw::typeName<Type>());
}

template<typename Key, typename Value, std::size_t size>
struct ConstExprMap {
    std::array<std::pair<Key, Value>, size> data;

    [[nodiscard]] constexpr Value           at(const Key &key) const {
        const auto itr = std::find_if(begin(data), end(data), [&key](const auto &v) { return v.first == key; });
        return (itr != end(data)) ? itr->second : throw std::range_error(fmt::format("key '{}' not found", key));
    }
};

} // namespace opencmw
#endif //OPENCMW_H
