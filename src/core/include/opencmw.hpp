#ifndef OPENCMW_H
#define OPENCMW_H

#include <array>
#include <concepts>
#include <map>
#include <set>
#include <vector>

#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <refl.hpp>
#include <units/concepts.h>
#include <units/quantity.h>
#include <units/quantity_io.h>

#define FWD(x) std::forward<decltype(x)>(x)               // short-hand notation
#define forceinline inline __attribute__((always_inline)) // use this for hot-spots only <-> may bloat code size, not fit into cache and consequently slow down execution
#define neverinline __attribute__((noinline))             // primarily used to avoid inlining (rare) exception handling code

#define ENABLE_REFLECTION_FOR(TypeName, ...) \
    REFL_TYPE(TypeName __VA_OPT__(, )) \
    REFL_DETAIL_FOR_EACH(REFL_DETAIL_EX_1_field __VA_OPT__(, ) __VA_ARGS__) \
    REFL_END

namespace units::detail { // TODO: temporary -> remove with next mp-units release
template<typename T>
requires units::is_derived_from_specialization_of<T, units::quantity> && requires {
    typename T::dimension;
    typename T::unit;
    typename T::rep;
}
inline constexpr const bool is_quantity<T> = true;
} // namespace units::detail

namespace opencmw {
template<typename>
constexpr bool always_false = false;

using units::basic_fixed_string;
using units::is_same_v;

constexpr auto &unmove(auto &&t) { return t; } // opposite of std::move(...)

template<typename T, typename Type = std::remove_cvref_t<T>>
inline constexpr const bool isStdType = get_name(refl::reflect<Type>()).template substr<0, 5>() == "std::";

template<class T, typename RawType = std::remove_cvref_t<T>>
inline constexpr bool isReflectableClass() {
    if constexpr (std::is_class_v<RawType> && refl::is_reflectable<RawType>() && !std::is_fundamental_v<RawType> && !std::is_array_v<RawType>) {
        return !isStdType<RawType>; // N.B. check this locally since this is not constexpr (yet)
    }
    return false;
}
template<typename T>
constexpr bool isReflectableClass_v = isReflectableClass<T>();
template<class T>
concept ReflectableClass = isReflectableClass<T>();

#ifndef OPENCMW_ENABLE_UNSIGNED_SUPPORT
template<typename T, typename RawType = std::remove_cvref_t<T>>
inline constexpr bool is_supported_number = std::is_signed_v<RawType> || is_same_v<RawType, bool> || is_same_v<RawType, uint8_t>; // int[8, 64]_t || float || double || bool || uint8_t;
#else
inline constexpr bool is_supported_number = std::is_arithmetic<Tp>;
#endif

template<typename T, typename RawType = std::remove_cvref_t<T>>
inline constexpr bool is_stringlike = units::is_derived_from_specialization_of<RawType, std::basic_string> || units::is_derived_from_specialization_of<RawType, std::basic_string_view>;

template<typename T>
concept StringLike = is_stringlike<T>;

template<typename T>
concept Number = is_supported_number<T>;

template<typename T, typename Tp = std::remove_const_t<T>>
concept ArithmeticType = std::is_arithmetic_v<Tp>;

template<typename T>
concept SupportedType = is_supported_number<T> || is_stringlike<T>;

template<typename T, typename RawType = std::remove_cvref_t<T>>
inline constexpr bool is_map_like = units::is_derived_from_specialization_of<RawType, std::map> || units::is_derived_from_specialization_of<RawType, std::unordered_map>;

template<typename T>
concept MapLike = is_map_like<T>;

template<typename T>
inline constexpr const bool is_array = false;
template<typename T, std::size_t N>
inline constexpr const bool is_array<std::array<T, N>> = true;
template<typename T, std::size_t N>
inline constexpr const bool is_array<const std::array<T, N>> = true;

template<typename T, typename Tp = std::remove_const_t<T>>
inline constexpr bool is_vector = units::is_derived_from_specialization_of<Tp, std::vector>;

template<typename T>
concept ArrayOrVector = is_vector<T> || is_array<T>;

template<typename C, typename T = typename C::value_type, std::size_t size = 0>
concept StringArray = (is_array<C> || is_vector<C>) &&is_stringlike<T>;

template<typename T, class Deleter = std::default_delete<T>>
inline constexpr const bool is_smart_pointer = false;
template<typename T, typename Deleter>
inline constexpr const bool is_smart_pointer<std::unique_ptr<T, Deleter>> = true;
template<typename T, typename Deleter>
inline constexpr const bool is_smart_pointer<const std::unique_ptr<T, Deleter>> = true;
template<typename T>
inline constexpr const bool is_smart_pointer<std::unique_ptr<T>> = true;
template<typename T>
inline constexpr const bool is_smart_pointer<const std::unique_ptr<T>> = true;
template<typename T>
inline constexpr const bool is_smart_pointer<std::shared_ptr<T>> = true;
template<typename T>
inline constexpr const bool is_smart_pointer<const std::shared_ptr<T>> = true;

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
    if constexpr (units::is_specialization_of<T, std::unique_ptr>) {
        if (!smart_pointer) {
            smart_pointer = std::make_unique<Type>();
        }
    } else if constexpr (units::is_specialization_of<T, std::shared_ptr>) {
        if (!smart_pointer) {
            smart_pointer = std::make_shared<Type>();
        }
    }
    return std::forward<Type &>(*smart_pointer.get());
}

template<typename Type, template<typename...> typename Template>
struct is_instance_of : std::false_type {};
template<typename... Args, template<typename...> typename Template>
struct is_instance_of<Template<Args...>, Template> : std::true_type {};
template<typename Type, template<typename...> typename Template>
constexpr bool is_instance_of_v = is_instance_of<Type, Template>::value;

template<template<typename...> typename To,
        template<typename...> typename From,
        typename... Items>
auto to_instance(From<Items...> from) -> To<Items...>;

namespace detail {

template<typename Out, typename... Ts>
struct filter_tuple : std::type_identity<Out> {};

template<typename... Out, typename InCar, typename... InCdr> // template for std::tuple arguments
struct filter_tuple<std::tuple<Out...>, std::tuple<InCar, InCdr...>>
    : std::conditional_t<(std::is_same_v<InCar, Out> || ...),
              filter_tuple<std::tuple<Out...>, std::tuple<InCdr...>>,
              filter_tuple<std::tuple<Out..., InCar>, std::tuple<InCdr...>>> {};

template<typename Out, typename... Ts>
struct filter_tuple2 : std::type_identity<Out> {};

template<typename... Ts, typename U, typename... Us> // template for variadic arguments returning std::tuple
struct filter_tuple2<std::tuple<Ts...>, U, Us...> : std::conditional_t<(std::is_same_v<U, Ts> || ...), filter_tuple2<std::tuple<Ts...>, Us...>, filter_tuple2<std::tuple<Ts..., U>, Us...>> {};

} // namespace detail

template<typename... input_t>
using tuple_cat_t = decltype(std::tuple_cat(std::declval<input_t>()...));

template<typename>
constexpr bool is_tuple = false;
template<typename... T>
constexpr bool is_tuple<std::tuple<T...>> = true;

template<class T>
requires(is_tuple<T>) using tuple_unique = typename detail::filter_tuple<std::tuple<>, T>::type;

template<template<typename...> typename Type, typename... Items>
using find_type = decltype(std::tuple_cat(std::declval<std::conditional_t<is_instance_of_v<Items, Type>, std::tuple<Items>, std::tuple<>>>()...));

template<typename T, typename... List>
struct is_in_list : std::disjunction<std::is_same<T, List>...> {};

template<typename... Ts>
struct is_uniq : std::false_type {};

template<>
struct is_uniq<> : std::true_type {};

template<typename T0, typename... Ts>
requires(!is_in_list<T0, Ts...>::value) struct is_uniq<T0, Ts...> : is_uniq<Ts...> {};

template<typename... Ts>
struct type_list {};

namespace detail {

template<template<typename...> class C, typename Uniq, typename Input>
struct filter_dups_impl {};

template<template<typename...> class C, typename... Uniqs>
struct filter_dups_impl<C, type_list<Uniqs...>, type_list<>> {
    using type = C<Uniqs...>;
};

template<template<typename...> class C, typename... Uniqs, typename Input0, typename... Inputs>
requires(... && !std::same_as<Input0, Uniqs>) struct filter_dups_impl<C, type_list<Uniqs...>, type_list<Input0, Inputs...>>
    : filter_dups_impl<C, type_list<Uniqs..., Input0>, type_list<Inputs...>> {
};

template<template<typename...> class C, typename... Uniqs, typename Input0, typename... Inputs>
requires(... || std::same_as<Input0, Uniqs>) struct filter_dups_impl<C, type_list<Uniqs...>, type_list<Input0, Inputs...>>
    : filter_dups_impl<C, type_list<Uniqs...>, type_list<Inputs...>> {};

template<template<typename...> class C, typename... Ts>
struct filter_duplicates : filter_dups_impl<C, type_list<>, type_list<Ts...>> {
};
} // namespace detail

template<template<typename...> class C, typename... Ts>
using filter_duplicates_t = typename detail::filter_duplicates<C, Ts...>::type;

// clang-format off
/*
 * unit-/description-type annotation
 */

template<typename T>
concept NotRepresentation = units::Quantity<T> || units::QuantityLike<T> || units::wrapped_quantity_<T> || (!std::regular<T>) || (!units::scalable_<T>) || std::is_class_v<T>;

using NoUnit = units::dimensionless<units::one>;

enum ExternalModifier {
RW = 0, // read-write access
RO = 1, // read-only access
RW_DEPRECATED = 2, // read-write access -- deprecated
RO_DEPRECATED = 3, // read-only access -- deprecated
RW_PRIVATE = 4, // read-write access -- private/non-production API
RO_PRIVATE = 5, // read-only access -- private/non-production API
UNKNOWN = 255
};

constexpr bool is_readonly(const ExternalModifier mod) { return mod % 2 == 1; }
constexpr bool is_deprecated(const ExternalModifier mod) { return (mod & 2) > 0; }
constexpr bool is_private(const ExternalModifier mod) { return (mod & 4) > 0; }

constexpr ExternalModifier get_ext_modifier(const uint8_t byteValue) {
    switch(byteValue) {
    [[likely]] case RW: return RW;
    case RO: return RO;
    case RW_DEPRECATED: return RW_DEPRECATED;
    case RO_DEPRECATED: return RO_DEPRECATED;
    case RW_PRIVATE: return RW_PRIVATE;
    case RO_PRIVATE: return RO_PRIVATE;
    default: return UNKNOWN;
    }
}
} // namespace opencmw

template<>
struct fmt::formatter<opencmw::ExternalModifier> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }
    template<typename FormatContext>
    constexpr auto format(const opencmw::ExternalModifier &state, FormatContext &ctx) {
        using namespace opencmw;
        switch (state) {
        case RW: return fmt::format_to(ctx.out(), "RW");
        case RO: return fmt::format_to(ctx.out(), "RO");
        case RW_DEPRECATED: return fmt::format_to(ctx.out(), "RW_DEPRECATED");
        case RO_DEPRECATED: return fmt::format_to(ctx.out(), "RO_DEPRECATED");
        case RW_PRIVATE: return fmt::format_to(ctx.out(), "RW_PRIVATE");
        case RO_PRIVATE: return fmt::format_to(ctx.out(), "RO_PRIVATE");
        case UNKNOWN: [[fallthrough]];
            default:
                throw std::logic_error("unknown ExternalModifier state");
        }
    }
};

inline std::ostream &operator<<(std::ostream &os, const opencmw::ExternalModifier &v) {
    return os << fmt::format("{}", v);
}

namespace opencmw {
template<typename Rep, units::Quantity Q = NoUnit, const basic_fixed_string description = "" , const ExternalModifier modifier = RW, const basic_fixed_string... groups>
struct Annotated; // prototype template -- N.B. there are two implementations since most non-numeric classes do not qualify as units::Representation

template<typename T> inline constexpr const bool is_annotated = false;
template<typename T, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline constexpr const bool is_annotated<Annotated<T, Q, description, modifier, groups...>> = true;
template<typename T, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline constexpr const bool is_annotated<const Annotated<T, Q, description, modifier, groups...>> = true;

template<class T>
concept AnnotatedType = is_annotated<T>;
template<class T>
concept NotAnnotatedType = !is_annotated<T>;

template<units::Representation Rep, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
struct Annotated<Rep, Q, description, modifier, groups...> : public units::quantity<typename Q::dimension, typename Q::unit, Rep> {
    using dimension = typename Q::dimension;
    using unit = typename Q::unit;
    using rep = Rep;
    using R = units::quantity<dimension, unit, rep>;
    static constexpr basic_fixed_string unitStr = units::detail::unit_text<dimension, unit>().ascii();

    constexpr Annotated() : R() {};
    constexpr explicit (!std::is_trivial_v<Rep>) Annotated(const rep &initValue) noexcept : R(initValue) {}
    constexpr explicit (!std::is_trivial_v<Rep>) Annotated(const R& t) : R(t) {}
    constexpr explicit (!std::is_trivial_v<Rep>) Annotated(R&& t) : R(std::move(t)) {}

    constexpr Annotated &operator=(const R& t) {  R::operator= (t); return *this; }
    constexpr Annotated &operator=(R&& t) { R::operator= (std::move(t)); return *this; }

    [[nodiscard]] constexpr std::string_view getUnit() const noexcept { return unitStr.c_str(); }
    [[nodiscard]] constexpr std::string_view getDescription() const noexcept { return description.c_str(); }
    [[nodiscard]] constexpr ExternalModifier getModifier() const noexcept { return modifier; }
   // constexpr                      operator rep &() { return this->number(); } // TODO: check if this is safe and/or whether we want this (by-passes type safety)

    constexpr auto operator<=>(const Annotated &) const noexcept = default;
    // raw data access
    [[nodiscard]] inline constexpr rep& value() & noexcept { return this->number(); }
    [[nodiscard]] inline constexpr const rep& value() const & noexcept { return this->number(); }
    [[nodiscard]] inline constexpr rep&& value() && noexcept { return std::move(this->number()); }
    [[nodiscard]] inline constexpr const rep&& value() const && noexcept { return std::move(this->number()); }

    constexpr operator R&() &{ return *this; }
    constexpr operator const R&() const &{ return *this; }
    constexpr operator R&&() &&{ return std::move(*this); }
    constexpr operator const R&&() const &&{ return std::move(*this); }
};

template<NotRepresentation T, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
struct Annotated<T, Q, description, modifier, groups...> : public T { // inherit from T directly to inherit also all its potential operators & member functions
    using dimension = typename Q::dimension;
    using unit = typename Q::unit;
    using rep = T;
    static constexpr basic_fixed_string unitStr = units::detail::unit_text<dimension, unit>().ascii();
    constexpr Annotated(): T() {}
    explicit (!std::is_trivial_v<T>) constexpr Annotated(const T& t) : T(t) {}
    explicit (!std::is_trivial_v<T>) constexpr Annotated(T&& t) : T(std::move(t)) {}

    template<class... S>
    explicit Annotated(S&&... v) : T{std::forward<S>(v)...} {}

    template<class S , std::enable_if_t<std::is_constructible_v<T, std::initializer_list<S>>, int> = 0>
    Annotated(std::initializer_list<S> init) : T(init) {}

    template<typename U>
    requires (std::is_assignable_v<T, U>)
    Annotated(const U& u) : T(u) {}

    template<typename U>
    requires (std::is_assignable_v<T, U>)
    Annotated(U&& u) : T(std::move(u)) {}

    [[nodiscard]] constexpr std::string_view getUnit() const noexcept { return unitStr.c_str(); }
    [[nodiscard]] constexpr std::string_view getDescription() const noexcept { return description.c_str(); }
    [[nodiscard]] constexpr ExternalModifier getModifier() const noexcept { return modifier; }

    auto           operator<=>(const Annotated &) const noexcept = default;
//    constexpr auto operator<=>(const Annotated &) const noexcept = default; //TODO: conditionally enable if type T allows it (i.e. is 'constexpr')

    // raw data access
    [[nodiscard]] inline constexpr Annotated::rep& value() & noexcept { return *this; }
    [[nodiscard]] inline constexpr const Annotated::rep& value() const & noexcept { return *this; }
    [[nodiscard]] inline constexpr Annotated::rep&& value() && noexcept { return std::move(*this); }
    [[nodiscard]] inline constexpr const Annotated::rep&& value() const && noexcept { return std::move(*this); }
};

template<typename T, std::size_t N, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline constexpr const bool is_array<Annotated<std::array<T, N>, Q, description, modifier, groups...>> = true;
template<typename T, std::size_t N, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline constexpr const bool is_array<Annotated<const std::array<T, N>, Q, description, modifier, groups...>> = true;
template<typename T, std::size_t N, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline constexpr const bool is_array<const Annotated<std::array<T, N>, Q, description, modifier, groups...>> = true;
template<typename T, std::size_t N, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline constexpr const bool is_array<const Annotated<const std::array<T, N>, Q, description, modifier, groups...>> = true;

template<typename T, typename A, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline constexpr const bool is_vector<Annotated<std::vector<T,A>, Q, description, modifier, groups...>> = true;
template<typename T, typename A, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline constexpr const bool is_vector<Annotated<const std::vector<T,A>, Q, description, modifier, groups...>> = true;
template<typename T, typename A, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline constexpr const bool is_vector<const Annotated<std::vector<T,A>, Q, description, modifier, groups...>> = true;
template<typename T, typename A, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline constexpr const bool is_vector<const Annotated<const std::vector<T,A>, Q, description, modifier, groups...>> = true;

// clang-format on

template<NotAnnotatedType T>
constexpr T getAnnotatedMember(const T &annotatedValue) noexcept {
    // N.B. still needed in 'putFieldHeader(IoBuffer&, const std::string_view &, const DataType&data, bool)'
    return annotatedValue; // TODO: sort-out/simplify perfect forwarding/move, see https://compiler-explorer.com/z/zTTjff7Tn
}

template<NotAnnotatedType T>
constexpr T &getAnnotatedMember(const T &annotatedValue) noexcept {
    return annotatedValue;
}

template<NotAnnotatedType T>
constexpr T &getAnnotatedMember(T &&annotatedValue) noexcept {
    return std::forward<T &>(annotatedValue); // perfect forwarding
}

template<AnnotatedType T>
constexpr typename T::rep &getAnnotatedMember(T &annotatedValue) noexcept {
    using Type = typename T::rep;
    return std::forward<Type &>(annotatedValue.value()); // perfect forwarding
}

template<AnnotatedType T>
constexpr typename T::rep &getAnnotatedMember(T &&annotatedValue) noexcept {
    using Type = typename T::rep;
    return std::forward<Type &>(annotatedValue.value()); // perfect forwarding
}

/* just some helper function to return nicer human-readable type names */
#pragma clang diagnostic push
#pragma ide diagnostic   ignored "misc-definitions-in-headers"
// clang-format off
template<typename T> const std::string_view typeName = typeid(T).name(); // safe fall-back
template<ReflectableClass T> inline constexpr const std::string_view typeName<T> = refl::reflect<T>().name.data;

template<ArithmeticType T> requires is_same_v<std::remove_const_t<T>, int8_t>    inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "int8_t const" : "int8_t";
template<ArithmeticType T> requires is_same_v<std::remove_const_t<T>, int16_t>   inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "int16_t const" : "int16_t";
template<ArithmeticType T> requires is_same_v<std::remove_const_t<T>, int32_t>   inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "int32_t const" : "int32_t";
template<ArithmeticType T> requires is_same_v<std::remove_const_t<T>, int64_t>   inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "int64_t const" : "int64_t";
template<ArithmeticType T> requires is_same_v<std::remove_const_t<T>, long long> inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "int128_t const" : "int128_t";
template<ArithmeticType T> requires is_same_v<std::remove_const_t<T>, uint8_t>    inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "uint8_t const" : "uint8_t";
template<ArithmeticType T> requires is_same_v<std::remove_const_t<T>, uint16_t>   inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "uint16_t const" : "uint16_t";
template<ArithmeticType T> requires is_same_v<std::remove_const_t<T>, uint32_t>   inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "uint32_t const" : "uint32_t";
template<ArithmeticType T> requires is_same_v<std::remove_const_t<T>, uint64_t>   inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "uint64_t const" : "uint64_t";
template<ArithmeticType T> requires is_same_v<std::remove_const_t<T>, unsigned long long> inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "uint128_t const" : "uint128_t";

template<typename T> requires is_same_v<std::remove_const_t<T>, std::byte> inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "int8_t const" : "int8_t";
template<typename T> requires is_same_v<std::remove_const_t<T>, char>      inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "byte const" : "byte";
template<typename T> requires is_same_v<std::remove_const_t<T>, float_t>   inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "float_t const" : "float_t";
template<typename T> requires is_same_v<std::remove_const_t<T>, double_t>  inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "double_t const" : "double_t";

template<StringLike T> requires units::is_derived_from_specialization_of<T, std::basic_string> inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "string const" : "string";
template<StringLike T> requires units::is_derived_from_specialization_of<T, std::basic_string_view> inline constexpr const std::string_view typeName<T> = std::is_const_v<T> ? "string const" : "string";

template<typename T, std::size_t N> inline const std::string &typeName<std::array<T,N>> = fmt::format("array<{},{}>", typeName<T>, N);
template<typename T, std::size_t N> inline const std::string &typeName<std::array<T,N> const> = fmt::format("array<{},{}> const", typeName<T>, N);
template<typename T, typename A> inline const std::string &typeName<std::vector<T,A>> = fmt::format("vector<{}>", typeName<T>);
template<typename T, typename A> inline const std::string &typeName<std::vector<T,A> const> = fmt::format("vector<{}> const", typeName<T>);

template<MapLike T> inline const std::string &typeName<T> =  fmt::format("map<{},{}>", opencmw::typeName<typename T::key_type>, opencmw::typeName<typename T::mapped_type>);
template<MapLike T> inline const std::string &typeName<T const> =  fmt::format("map<{},{}> const", opencmw::typeName<typename T::key_type>, opencmw::typeName<typename T::mapped_type>);

template<typename T, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline const std::string &typeName<Annotated<T, Q, description, modifier, groups...>> = fmt::format("Annotated<{}>", opencmw::typeName<T>);
template<typename T, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline const std::string &typeName<Annotated<T, Q, description, modifier, groups...> const> =  fmt::format("Annotated<{}> const", opencmw::typeName<T>);

// legacy arrays
template<ArithmeticType T, std::size_t size> inline const std::string &typeName<T[size]> = fmt::format("{}[{}]", opencmw::typeName<T>, size);
template<ArithmeticType T, std::size_t size> inline const std::string &typeName<const T[size]> = fmt::format("{}[{}] const", opencmw::typeName<T>, size);
template<ArithmeticType T> inline const std::string &typeName<T*> = fmt::format("{}[?]", opencmw::typeName<T>);
template<ArithmeticType T> inline const std::string &typeName<const T*> = fmt::format("({} const)[?] ", opencmw::typeName<T>);
template<ArithmeticType T> inline const std::string &typeName<T* const> = fmt::format("{} [?] const", opencmw::typeName<T>);

// clang-format off
#pragma clang diagnostic pop

template<typename Key, typename Value, std::size_t size>
struct ConstExprMap {
    const std::array<std::pair<Key, Value>, size> data;

    [[nodiscard]] constexpr Value           at(const Key &key) const {
        const auto itr = std::ranges::find_if(begin(data), end(data), [&key](const auto &v) { return v.first == key; });
        return (itr != end(data)) ? itr->second : throw std::out_of_range(fmt::format("key '{}' not found", key));
    }

    [[nodiscard]] constexpr Value           at(const Key &key, const Value &defaultValue) const noexcept {
        auto itr = std::ranges::find_if(begin(data), end(data), [&key](const auto &v) { return v.first == key; });
        return (itr != end(data)) ? itr->second : defaultValue;
    }
};

/**
 * A java compatible string hash function.
 * Might be be replaced by a more efficient algorithm in the future.
 * @param string the string to compute the hash for
 * @return hash value of the string
 */
inline constexpr int hash(const std::string_view &string) noexcept {
    int h = 0;
    for (char c : string) {
        h = h * 31 + static_cast<int>(c);
    }
    return h;
}

// Exception types
class startup_error : public std::runtime_error {
    using runtime_error::runtime_error;
};


/***************** operator<< and other print overloads ***********************/

inline int            getClassInfoVerbose() {
    static int i = std::ios_base::xalloc();
    return i;
}

template<typename CharT, typename Traits>
std::basic_ostream<CharT, Traits> &ClassInfoVerbose(std::basic_ostream<CharT, Traits> &os) {
    os.iword(getClassInfoVerbose()) = true;
    return os;
}

template<typename CharT, typename Traits>
std::basic_ostream<CharT, Traits> &ClassInfoShort(std::basic_ostream<CharT, Traits> &os) {
    os.iword(getClassInfoVerbose()) = false;
    return os;
}

inline int getClassInfoIndent() {
    static int i = std::ios_base::xalloc();
    return i;
}

inline int getClassInfoIndentStep() {
    static int i = std::ios_base::xalloc();
    return i;
}

inline std::ostream &ClassInfoIndentStep(std::ostream &os, const uint32_t indentValue) {
    os.iword(getClassInfoIndentStep()) += indentValue;
    return os;
}
inline std::ostream &ClassInfoIndentInc(std::ostream &os) {
    os.iword(getClassInfoIndent()) += 1;
    return os;
}
inline std::ostream &ClassInfoIndentDec(std::ostream &os) {
    os.iword(getClassInfoIndent()) -= 1;
    return os;
}

template<ArrayOrVector T>
// requires (!isAnnotated<T>())
inline std::ostream &operator<<(std::ostream &os, const T &v) {
    os << '{';
    for (std::size_t i = 0; i < v.size(); ++i) {
        os << v[i];
        if (i != v.size() - 1) {
            os << ", ";
        }
    }
    os << "}";
    return os;
}

template<typename Rep, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline std::ostream &operator<<(std::ostream &os, const Annotated<Rep, Q, description, modifier, groups...> &annotatedValue) {
    if (os.iword(getClassInfoVerbose())) {
        if constexpr (!is_array<Rep> && !is_vector<Rep>) {
            os << fmt::format("{:<5}  // [{}] - {}", annotatedValue.value(), annotatedValue.getUnit(), annotatedValue.getDescription()); // print as number
        } else {
            os << fmt::format("{}  // [{}] - {}", annotatedValue.value(), annotatedValue.getUnit(), annotatedValue.getDescription()); // print as array
        }
        return os;
    }
    os << fmt::format("{}", annotatedValue.value()); // print as number
    return os;
}

template<typename T>
inline std::ostream &operator<<(std::ostream &os, const std::unique_ptr<T> &v) {
    if (v) {
        return os << "unique_ptr{" << (*v.get()) << '}';
    } else {
        return os << "unique_ptr{nullptr}";
    }
}

template<typename T>
inline std::ostream &operator<<(std::ostream &os, const std::shared_ptr<T> &v) {
    if (v) {
        return os << "shared_ptr{" << (*v.get()) << '}';
    } else {
        return os << "shared_ptr{nullptr}";
    }
}

template<MapLike T>
inline std::ostream &operator<<(std::ostream &os, const T &map) {
    os << '{';
    bool first = true;
    for (auto const &[key, val] : map) {
        if (first) {
            first = false;
        } else {
            os << ", ";
        }
        os << key << ':' << val;
    }
    os << "}";
    return os;
}

template<ReflectableClass T>
inline std::ostream &operator<<(std::ostream &os, const T &value) {
    const bool    verbose    = os.iword(getClassInfoVerbose());
    const int64_t indent     = os.iword(getClassInfoIndent());
    const int64_t indentStep = os.iword(getClassInfoIndentStep()) == 0 ? (os.iword(getClassInfoIndentStep()) = 2) : os.iword(getClassInfoIndentStep());
    os << fmt::format("{}({}", (indent == 0) ? refl::reflect(value).name.data : "", verbose ? "\n" : "");
    using ValueType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(value)))>;
    if constexpr (isReflectableClass<ValueType>()) {
        for_each(
                refl::reflect(value).members, [&](const auto member, const auto ) constexpr {
//                    using MemberType          = std::remove_reference_t<decltype(unwrapPointer(member(value)))>;
//                    const auto &typeNameShort = typeName<MemberType>;
                    // const auto& typeNameShort = refl::reflect(getAnnotatedMember(member(value))).name.data; // alt type-definition:
                    if (verbose) {
//                        os << fmt::format("{:{}} {}: {:<25} {:<35}= ", "", indent * indentStep + 1, index, typeNameShort, get_debug_name(member));
                    } else {
//                        os << fmt::format("{}{}=", (index > 0) ? ", " : "", get_display_name(member));
                    }
                    ClassInfoIndentInc(os);
                    os << fmt::format("{}", member(value));
                    ClassInfoIndentDec(os);
                    os << (verbose ? "\n" : ""); // calls this operator<< if necessary
                });
        os << fmt::format("{:{}})", "", verbose ? (indent * indentStep + 1) : 0);
    }
    return os;
}

template<typename T>
inline constexpr void diffView(std::ostream &os, const T &lhs, const T &rhs) {
    const bool    verbose    = os.iword(getClassInfoVerbose());
    const int64_t indent     = os.iword(getClassInfoIndent());
    const int64_t indentStep = os.iword(getClassInfoIndentStep()) == 0 ? (os.iword(getClassInfoIndentStep()) = 2) : os.iword(getClassInfoIndentStep());
    using ValueType          = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(lhs)))>;
    if constexpr (isReflectableClass<ValueType>()) {
        os << fmt::format("{}{}({}", (indent == 0) ? "diffView: " : "", (indent == 0) ? refl::reflect(lhs).name.data : "", verbose ? "\n" : "");
        for_each(
                refl::reflect(lhs).members, [&](const auto member, const auto index) constexpr {
                    if constexpr (is_field(member)) {
                        using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(lhs))))>;
                        if (verbose) {
                            os << fmt::format("{:{}} {}: {:<20} {:<30}= ", "", indent * indentStep + 1, index, typeName<MemberType>, fmt::format("{}::{}", member.declarator.name, member.name));
                        } else {
                            os << fmt::format("{}{}=", (index > 0) ? ", " : "", member.name);
                        }
                        ClassInfoIndentInc(os);
                        if constexpr (isReflectableClass<MemberType>()) {
                            using UnwrappedMemberType = std::remove_reference_t<decltype(member(lhs))>;
                            if constexpr (is_smart_pointer<std::remove_reference_t<UnwrappedMemberType>>) {
                                // check for empty smart pointer
                                if (!member(lhs) || !member(rhs)) {
                                    // clang-format off
                                    if (!member(lhs) && !member(rhs)) { os << "{nullPointer}" << (verbose ? "\n" : ""); return; }
                                    os << fmt::format(fg(fmt::color::red), "differ: ");
                                    if (member(lhs)) { os << *member(lhs).get(); } else { os << "{nullPointer}"; }
                                    os << " vs. ";
                                    if (member(rhs)) { os << *member(rhs).get(); } else { os << "{nullPointer}"; }
                                    os << (verbose ? "\n" : ""); // calls this operator<< if necessary
                                    // clang-format on
                                    return;
                                }
                            }
                            // non-empty member structs, dive further into hierarchy
                            diffView(os, getAnnotatedMember(unwrapPointer(member(lhs))), getAnnotatedMember(unwrapPointer(member(rhs))));
                        } else {
                            if (member(lhs) == member(rhs)) {
                                os << member(lhs);
                            } else {
                                diffView(os, member(lhs), member(rhs));
                            }
                        }
                        ClassInfoIndentDec(os);
                        os << (verbose ? "\n" : ""); // calls this operator<< if necessary
                    }
                });
        os << fmt::format("{:{}})", "", verbose ? (indent * indentStep + 1) : 0);
    } else {
        // primitive type
        auto lhsValue = getAnnotatedMember(unwrapPointer(lhs));
        auto rhsValue = getAnnotatedMember(unwrapPointer(rhs));
        if (lhsValue == rhsValue) {
            os << lhsValue;
        } else {
            os << fmt::format(fg(fmt::color::red), "{} vs. {}", lhsValue, rhsValue); // coloured terminal output
        }
    }
    if (indent == 0) {
        os << std::endl;
    }
}

} // namespace opencmw

template<opencmw::ReflectableClass T>
struct fmt::formatter<T> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(T const &value, FormatContext &ctx) {
        using namespace opencmw; // for operator<< overloading
        std::stringstream ss;    // N.B. std::stringstream construct to avoid recursion with 'operator<<' definition
        ss << value << std::flush;
        return fmt::format_to(ctx.out(), "{}", ss.str());
    }
};

#endif // OPENCMW_H
