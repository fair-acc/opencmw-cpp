#ifndef OPENCMW_H
#define OPENCMW_H
//#include "../cmake-build-debug/_deps/refl-cpp-src/refl.hpp"
#include <MultiArray.hpp> // TODO: resolve dangerous circular dependency
#include <fmt/color.h>
#include <fmt/format.h>
#include <refl.hpp>
#include <set>
#include <units/concepts.h>
#include <units/quantity.h>
#include <units/quantity_io.h>

#define ENABLE_REFLECTION_FOR(TypeName, ...) \
    REFL_TYPE(TypeName) \
    REFL_DETAIL_FOR_EACH(REFL_DETAIL_EX_1_field, __VA_ARGS__) \
    REFL_END

namespace opencmw {
template<typename T, typename Type = typename std::decay<T>::type>
inline constexpr bool isStdType = get_name(refl::reflect<Type>()).template substr<0, 5>() == "std::";

template<class T>
constexpr bool isReflectableClass() {
    using Type = typename std::decay<T>::type;
    if constexpr (std::is_class<Type>::value && refl::is_reflectable<Type>() && !std::is_fundamental<Type>::value && !std::is_array<Type>::value) {
        return !isStdType<Type>; // N.B. check this locally since this is not constexpr (yet)
    }
    return false;
}
template<class T>
concept ReflectableClass = isReflectableClass<T>();

using namespace units;
template<class T, class U> // TODO: from mp-units - remove once lib is integrated
inline constexpr bool is_same_v = false;
template<class T>
inline constexpr bool is_same_v<T, T> = true;
template<class T, class U>
using is_same = std::bool_constant<is_same_v<T, U>>;

#ifndef OPENCMW_ENABLE_UNSIGNED_SUPPORT
template<typename T, typename Tp = typename std::decay<T>::type>
inline constexpr bool is_supported_number = is_same_v<Tp, bool> || is_same_v<Tp, char> || is_same_v<Tp, uint8_t> || is_same_v<Tp, int8_t> || is_same_v<Tp, int8_t> || is_same_v<Tp, int16_t> //
                                         || is_same_v<Tp, int32_t> || is_same_v<Tp, int64_t> || is_same_v<Tp, float> || is_same_v<Tp, double>;
#else
inline constexpr bool is_supported_number = std::is_arithmetic<Tp>;
#endif

template<typename T>
constexpr bool isStringLike() {
    using Tp = typename std::decay<T>::type;
    return is_same_v<Tp, std::string> || is_same_v<Tp, std::string_view>;
}

template<typename T>
concept StringLike = isStringLike<T>();

template<typename T>
concept Number = is_supported_number<T>;

template<typename T, typename Tp = typename std::remove_const<T>::type>
concept ArithmeticType = std::is_arithmetic_v<Tp>;

template<typename T>
concept SupportedType = is_supported_number<T> || isStringLike<T>();

template<size_t N>
struct StringLiteral : refl::util::const_string<N> {
#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-c-arrays"
#pragma ide diagnostic   ignored "hicpp-explicit-conversions"
    /**
     * compile-time string construction from string literal
     * N.B. ensures that last character is always 'null terminated'
     * @param str string literal
     */
    constexpr StringLiteral(const char (&str)[N]) noexcept {
        std::copy_n(str, N, refl::util::const_string<N>::data);
        refl::util::const_string<N>::data[N] = '\0';
    }
#pragma clang diagnostic pop
};

template<typename Test, template<typename...> class Ref>
inline constexpr bool is_specialization = false;
template<template<typename...> class Ref, typename... Args>
inline constexpr bool is_specialization<Ref<Args...>, Ref> = true;

template<typename T>
inline constexpr bool is_array = false;
template<typename T, std::size_t N>
inline constexpr bool is_array<std::array<T, N>> = true;

template<typename T>
inline constexpr bool is_array_or_vector = false;
template<typename T, typename A>
inline constexpr bool is_array_or_vector<std::vector<T, A>> = true;
template<typename T, std::size_t N>
inline constexpr bool is_array_or_vector<std::array<T, N>> = true;
template<typename T>
concept ArrayOrVector = is_array_or_vector<T>;

template<typename C, typename T = typename C::value_type, std::size_t size = 0>
concept StringArray = is_array_or_vector<C> && isStringLike<T>();

template<typename T>
concept NumberArray = std::is_bounded_array<T>::value; // && is_supported_number<T[]>::value;

template<typename T, class Deleter = std::default_delete<T>>
inline constexpr bool is_smart_pointer = false;
template<typename T, typename Deleter>
inline constexpr bool is_smart_pointer<std::unique_ptr<T, Deleter>> = true;
template<typename T, typename Deleter>
inline constexpr bool is_smart_pointer<const std::unique_ptr<T, Deleter>> = true;
template<typename T>
inline constexpr bool is_smart_pointer<std::unique_ptr<T>> = true;
template<typename T>
inline constexpr bool is_smart_pointer<const std::unique_ptr<T>> = true;
template<typename T>
inline constexpr bool is_smart_pointer<std::shared_ptr<T>> = true;
template<typename T>
inline constexpr bool is_smart_pointer<const std::shared_ptr<T>> = true;

template<class T>
concept SmartPointerType = is_smart_pointer<std::remove_reference_t<T>>;
template<class T>
concept NotSmartPointerType = !is_smart_pointer<std::remove_reference_t<T>>;

template<NotSmartPointerType T>
constexpr T unwrapPointer(const T &not_smart_pointer) { return not_smart_pointer; }
template<NotSmartPointerType T>
constexpr T &unwrapPointer(T &&not_smart_pointer) { return std::forward<T &>(not_smart_pointer); }
template<NotSmartPointerType T>
constexpr T &unwrapPointerCreateIfAbsent(T &&not_smart_pointer) { return std::forward<T &>(not_smart_pointer); }
template<SmartPointerType T>
constexpr typename T::element_type &unwrapPointer(T &smart_pointer) { return std::forward<typename T::element_type &>(*smart_pointer.get()); }
template<SmartPointerType T>
constexpr typename T::element_type &unwrapPointer(const T &smart_pointer) { return std::forward<typename T::element_type &>(*smart_pointer.get()); }

template<SmartPointerType T>
constexpr typename T::element_type &unwrapPointerCreateIfAbsent(T &smart_pointer) {
    using Type = typename T::element_type;
    // check if we need to create type -- if cascade because '!smart_pointer' is not necessarily constexpr
    if constexpr (is_specialization<T, std::unique_ptr>) {
        if (!smart_pointer) {
            smart_pointer = std::make_unique<Type>();
        }
    } else if constexpr (is_specialization<T, std::shared_ptr>) {
        if (!smart_pointer) {
            smart_pointer = std::make_shared<Type>();
        }
    }
    return std::forward<Type &>(*smart_pointer.get());
}

// clang-format off
/*
 * unit-/description-type annotation
 */
template<typename T, const StringLiteral unit = "", const StringLiteral description = "", const StringLiteral direction = "", const StringLiteral... groups>
struct Annotated {
    constexpr static void isAnnotated() noexcept {} // needed to check Annotated signature
    using ValueType = T;
    using String = std::string_view;
    mutable T value;
    constexpr Annotated() = default;
    constexpr Annotated(const T &initValue) noexcept : value(initValue) {}
    constexpr Annotated(T &&t) : value(std::move(t)) {}
    constexpr Annotated &operator=(const T &newValue) { value = newValue; return *this; }
    [[nodiscard]] constexpr String getUnit() const noexcept { return String(unit.data); }
    [[nodiscard]] constexpr String getDescription() const noexcept { return String(description.data); }
    [[nodiscard]] constexpr String getDirection() const noexcept { return String(direction.data, direction.size); }
    [[nodiscard]] constexpr String typeName() const noexcept;
    constexpr                      operator T &() { return value; }

    auto           operator<=>(const Annotated &) const noexcept = default;
    // constexpr auto operator<=>(const Annotated &) const noexcept = default; TODO: conditionally enable if type T allows it (i.e. is 'constexpr')

    template<typename T2, const StringLiteral ounit = "", const StringLiteral odescription = "", const StringLiteral odirection = "", const StringLiteral... ogroups>
    constexpr bool operator==(const T2 &rhs) const noexcept {
        if (value != rhs.value) { return false; }
        return getUnit() == rhs.getUnit();
    }

    T &            operator()() noexcept { return value; }
    constexpr void operator+=(const T &a) noexcept { value += a; }
    constexpr void operator-=(const T &a) noexcept { value -= a; }
    constexpr void operator*=(const T &a) noexcept { value *= a; }
    constexpr void operator/=(const T &a) { value /= a; }
    constexpr void operator*=(const Annotated &a) noexcept {
        value *= a.value; // N.B. actually also changes 'unit' -- implement? Nice semantic but performance....?
    }
    Annotated operator+(const Annotated &rhs) { return this->value += rhs.value; } //TODO: complete along the line of the units-library
    Annotated operator-(const Annotated &rhs) { return this->value -= rhs.value; }
    Annotated operator*(const Annotated &rhs) { return this->value *= rhs.value; }
    Annotated operator/(const Annotated &rhs) { return this->value /= rhs.value; }
    Annotated operator+(const ValueType &rhs) { return this->value += rhs; } //TODO: complete along the line of the units-library
    Annotated operator-(const ValueType &rhs) { return this->value -= rhs; }
    Annotated operator*(const ValueType &rhs) { return this->value *= rhs; }
    Annotated operator/(const ValueType &rhs) { return this->value /= rhs; }
    template<typename O> Annotated operator+(const O &rhs) { return this->value += rhs.value; }
    template<typename O> Annotated operator-(const O &rhs) { return this->value -= rhs.value; }
    template<typename O> Annotated operator*(const O &rhs) { return this->value *= rhs.value; }
    template<typename O> Annotated operator/(const O &rhs) { return this->value /= rhs.value; }

    //friend constexpr std::ostream &operator<<(std::ostream &os, const Annotated &m) { return os << m.value; }
};
// clang-format on

template<typename T>
inline constexpr bool isAnnotated() {
    using Type = typename std::decay<T>::type;
    // return is_specialization<Type, A>::value; // TODO: does not work with Annotated containing NTTPs
    if constexpr (requires { Type::isAnnotated(); }) {
        return true;
    }
    return false;
}
template<class T>
concept AnnotatedType = isAnnotated<T>();
template<class T>
concept NotAnnotatedType = !isAnnotated<T>();

template<NotAnnotatedType T>
constexpr T getAnnotatedMember(const T &annotatedValue) {
    // N.B. still needed in 'putFieldHeader(IoBuffer&, const std::string_view &, const DataType&data, bool)'
    return annotatedValue; //TODO: sort-out/simplify perfect forwarding/move, see https://compiler-explorer.com/z/zTTjff7Tn
}

template<NotAnnotatedType T>
constexpr T &getAnnotatedMember(const T &annotatedValue) {
    return annotatedValue;
}

template<NotAnnotatedType T>
constexpr T &getAnnotatedMember(T &&annotatedValue) {
    return std::forward<T &>(annotatedValue); // perfect forwarding
}

template<AnnotatedType T>
constexpr typename T::ValueType &getAnnotatedMember(T &annotatedValue) {
    using Type = typename T::ValueType;
    return std::forward<Type &>(annotatedValue.value); // perfect forwarding
}
template<AnnotatedType T>
constexpr typename T::ValueType &getAnnotatedMember(const T &annotatedValue) {
    using Type = typename T::ValueType;
    return std::forward<Type &>(annotatedValue.value); // perfect forwarding
}

/* just some helper function to return nicer human-readable type names */

// clang-format off
//template<ArithmeticType T> //TODO: rationalise/simplify this and extend this for custom classes using type-traits to query nicer class-type name
template<typename T, typename Tp = typename std::remove_const<T>::type>
requires(!std::is_array<Tp>::value && !is_array_or_vector<Tp> && !is_multi_array<Tp> && !isStringLike<T>() && !is_smart_pointer<T> && !isAnnotated<Tp>())
constexpr const char *typeName() noexcept {
        using namespace std::literals;
        if constexpr (is_same_v<T, std::byte>) { return "byte"; }
        if constexpr (is_same_v<T, int8_t>) { return "byte"; }
        if constexpr (is_same_v<T, uint8_t>) { return "byte"; }
        if constexpr (is_same_v<T, char>) { return "char"; }
        if constexpr (is_same_v<T, short>) { return "short"; }
        if constexpr (is_same_v<T, int>) { return "int"; }
        if constexpr (is_same_v<T, long>) { return "long"; }
        if constexpr (is_same_v<T, float>) { return "float"; }
        if constexpr (is_same_v<T, double>) { return "double"; }
        if constexpr (is_same_v<T, char *>) { return "char*"; }

        if constexpr (is_same_v<T, const std::byte>) { return "byte const"; }
        if constexpr (is_same_v<T, const char>) { return "char const"; }
        if constexpr (is_same_v<T, const short>) { return "short const"; }
        if constexpr (is_same_v<T, const int>) { return "int const"; }
        if constexpr (is_same_v<T, const long>) { return "long const"; }
        if constexpr (is_same_v<T, const float>) { return "float const"; }
        if constexpr (is_same_v<T, const double>) { return "double const"; }
        if constexpr (is_same_v<T, const char *>) { return "char* const"; }

    if constexpr (refl::is_reflectable<T>()) {
        return refl::reflect<T>().name.data;
    }
    return typeid(T).name();
}
// clang-format on

// clang-format off
template<typename C, typename T = typename C::value_type, std::size_t size = 0>
std::string typeName() noexcept {
    using Cp = typename std::remove_const<C>::type;
    constexpr std::string_view isConst = std::is_const_v<C> ? " const" : "";

    if constexpr (is_specialization<Cp, std::vector>) { return fmt::format("vector<{}>{}", opencmw::typeName<T>(), isConst); }
    if constexpr (is_array<Cp>) { return fmt::format("array<{},{}>{}", opencmw::typeName<T>(), sizeof(Cp)/sizeof(T), isConst); }
    if constexpr (is_specialization<Cp, std::set>) { return fmt::format("set<{}>{}", opencmw::typeName<T>(), isConst); }
    if constexpr (is_multi_array<Cp>) { return fmt::format("MultiArray<{},{}>{}", opencmw::typeName<T>(), C::n_dims_, isConst); }
    if constexpr (is_specialization<Cp, std::basic_string>) { return fmt::format("string{}", isConst); }
    if constexpr (is_specialization<Cp, std::basic_string_view>) { return fmt::format("string_view{}", isConst); }

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

template<AnnotatedType T>
std::string typeName() noexcept {
    using Type = typename T::ValueType;
    return fmt::format("Annotated<{}>", opencmw::typeName<Type>());
}

// clang-format off
template<SmartPointerType T, typename Tp = typename std::remove_const<T>::type>
std::string typeName() noexcept {
    using Type = typename std::remove_reference_t<T>;
    using ConstType = typename std::remove_reference_t<Tp>;
    using ValueType = typename T::element_type;

    if constexpr (is_specialization<Type, std::unique_ptr>)      { return fmt::format("unique_ptr<{}>", opencmw::typeName<ValueType>()); }
    if constexpr (is_specialization<Type, std::shared_ptr>)      { return fmt::format("shared_ptr<{}>", opencmw::typeName<ValueType>()); }
    if constexpr (is_specialization<ConstType, std::unique_ptr>) { return fmt::format("unique_ptr<{}> const", opencmw::typeName<ValueType>()); }
    if constexpr (is_specialization<ConstType, std::shared_ptr>) { return fmt::format("shared_ptr<{}> const", opencmw::typeName<ValueType>()); }

    return fmt::format("smart_pointer<{}>", opencmw::typeName<ValueType>());
}
// clang-format on

template<typename T, const StringLiteral unit, const StringLiteral description, const StringLiteral direction, const StringLiteral... groups>
[[nodiscard]] constexpr std::string_view Annotated<T, unit, description, direction, groups...>::typeName() const noexcept { return opencmw::typeName<T>(); }

template<typename Key, typename Value, std::size_t size>
struct ConstExprMap {
    std::array<std::pair<Key, Value>, size> data;

    [[nodiscard]] constexpr Value           at(const Key &key) const {
        const auto itr = std::find_if(begin(data), end(data), [&key](const auto &v) { return v.first == key; });
        return (itr != end(data)) ? itr->second : throw std::out_of_range(fmt::format("key '{}' not found", key));
    }
};

} // namespace opencmw
#endif //OPENCMW_H
