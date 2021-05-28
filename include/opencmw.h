#ifndef OPENCMW_H
#define OPENCMW_H
//#include "../cmake-build-debug/_deps/refl-cpp-src/refl.hpp"
#include <fmt/color.h>
#include <fmt/format.h>
#include <refl.hpp>
#include <set>
#include <MultiArray.hpp>

#define REFL_CUSTOM(TypeName, ...) \
    REFL_TYPE(TypeName) \
    REFL_DETAIL_FOR_EACH(REFL_DETAIL_EX_1_field, __VA_ARGS__) \
    REFL_END

namespace opencmw {
template<typename T>
constexpr bool isStdType() {
    return get_name(refl::reflect<T>()).template substr<0, 5>() == "std::";
}

template<class T>
constexpr bool isReflectableClass() {
    using Type = std::remove_reference_t<T>;
    if constexpr (std::is_class<Type>::value && refl::is_reflectable<Type>() && !std::is_fundamental<Type>::value && !std::is_array<Type>::value) {
        return !isStdType<Type>(); // N.B. check this locally since this is not constexpr (yet)
    }
    return false;
}

template<class T>
concept ReflectableClass = isReflectableClass<T>();

template<typename T>
struct is_supported_number {
    static const bool value = std::is_same<T, uint8_t>::value || std::is_same<T, int8_t>::value || std::is_same<T, int16_t>::value //
                           || std::is_same<T, int32_t>::value || std::is_same<T, int64_t>::value || std::is_same<T, float>::value || std::is_same<T, double>::value;
};

template<typename T>
concept Number = is_supported_number<T>::value;

template<typename T>
constexpr bool isStringLike() {
    return std::is_same<T, std::string>::value || std::is_same<T, std::string_view>::value;
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

//template<std::size_t size> // -> add type traits for reflectable
//constexpr std::ostream& operator<<(std::ostream& os, const StringLiteral<size>& m) {
//    os << m.value;
//    return os;
//}

template<typename T>
struct is_multi_array {
    static const bool value = false;
};

template<typename T, uint32_t n_dims>
struct is_multi_array<MultiArray<T, n_dims>> {
    static const bool value = true;
};

template<typename T>
concept MultiArrayType = is_multi_array<T>::value;

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

template<class N>
struct is_vector { static const bool value = false; };

template<class N, class A>
struct is_vector<std::vector<N, A>> { static const bool value = true; };

template<typename Test, template<typename...> class Ref>
struct is_specialization : std::false_type {};

template<template<typename...> class Ref, typename... Args>
struct is_specialization<Ref<Args...>, Ref> : std::true_type {};

/* just some helper function to return nicer human-readable type names */
template<typename T>
// N.B. extend this for custom classes using type-traits to query nicer class-type name
requires(!std::is_array<T>::value && !is_array_or_vector<T>::value && !is_multi_array<T>::value) constexpr const char *typeName() noexcept {
    // clang-format off
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
        if constexpr (is_specialization<T, std::basic_string>::value) { return "string"; }
        if constexpr (is_specialization<T, std::basic_string_view>::value) { return "string_view"; }
        //if constexpr (is_specialization<T, std::vector>::value) { return "vector<T>"; }
        if constexpr (std::is_same<T, char *>::value) { return "char*"; }

        if constexpr (std::is_same<T, const std::byte>::value) { return "byte const"; }
        if constexpr (std::is_same<T, const char>::value) { return "char const"; }
        if constexpr (std::is_same<T, const short>::value) { return "short const"; }
        if constexpr (std::is_same<T, const int>::value) { return "int const"; }
        if constexpr (std::is_same<T, const long>::value) { return "long const"; }
        if constexpr (std::is_same<T, const float>::value) { return "float const"; }
        if constexpr (std::is_same<T, const double>::value) { return "double const"; }
        if constexpr (is_specialization<T, std::basic_string>::value) { return "string const"; }
        if constexpr (is_specialization<T, std::basic_string_view>::value) { return "string_view const"; }
//        if constexpr (is_specialization<T, std::vector>::value) { return "vector<T>"; }
//        if constexpr (std::is_same<T, const char *>::value) { return "char* const"; }
    // clang-format on
    if constexpr (refl::is_reflectable<T>()) {
        return refl::reflect<T>().name.data;
    }
    return typeid(T).name();
}

template<typename C, typename T = typename C::value_type, std::size_t size = 0>
requires(!is_specialization<C, std::basic_string>::value && !is_specialization<C, std::basic_string_view>::value && !is_multi_array<C>::value)
        std::string typeName()
noexcept {
    using namespace std::literals;
    // clang-format off
    if constexpr (is_specialization<C, std::vector>::value) { return std::string("vector") + '<' + opencmw::typeName<T>() + '>'; }
    if constexpr (is_array_or_vector<C>::value) { return std::string("array") + '<' + opencmw::typeName<T>() +',' + std::to_string(size) + '>'; } // TODO: improve template to get proper size
    if constexpr (is_specialization<C, std::set>::value) { return std::string("set") + '<' + opencmw::typeName<T>() + '>'; }

    // clang-format on
    return std::string("CONTAINER") + '<' + opencmw::typeName<T>() + '>';
}

static constexpr std::string_view OPENCMW_BRACKET_OPEN  = "[";
static constexpr std::string_view OPENCMW_BRACKET_CLOSE = "[";

template<NumberArray T, std::size_t size>
std::string typeName() /* const */ noexcept {
    using namespace std::literals;
    using Type = typename std::remove_all_extents<T>::type;
    return std::string(opencmw::typeName<Type>()) + '[' + std::to_string(size) + ']';
}

template<NumberArray T>
std::string typeName() noexcept {
    using namespace std::literals;
    using Type = typename std::remove_all_extents<T>::type;
    return std::string(opencmw::typeName<Type>()) + "[?]";
}

template<MultiArrayType T>
std::string typeName() noexcept {
    using namespace std::literals;
    return fmt::format("MultiArray<{}, {}>", typeName<typename T::value_type>, T::n_dims_);
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
