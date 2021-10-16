
#ifndef YAZ_META_H
#define YAZ_META_H

#include <functional>
#include <type_traits>

// Perfect forwarding macro, as it is customary
#define YAZ_FWD(A) std::forward<decltype(A)>(A)

namespace yaz::meta {

// Useful for static_assert in constexpr branching to express
// that certain branches are always invalid
template<typename>
constexpr bool always_false = false;

// When you try to call this meta-function, it will produce a compiler error
// which will print out the types you pass to it. Useful for type deduction
// debugging
template<typename...>
class error_print_types;

// Just like the above, but prints out the values you pass to it
template<auto...>
class error_print_values;

// Meta-function that checks whether a type is an instantiation of
// a given template. For example:
//
// is_instantiation_of_v<std::vector, std::vector<int>> // true
// is_instantiation_of_v<std::basic_string, std::string> // true
// is_instantiation_of_v<std::vector, std::list<int>> // false

template<template<typename...> typename Template,
        typename Type>
inline constexpr bool is_instantiation_of_v = false;

template<template<typename...> typename Template,
        typename... Args>
inline constexpr bool is_instantiation_of_v<Template,
        Template<Args...>> = true;

template<template<auto...> typename Template,
        typename Type>
inline constexpr bool is_value_instantiation_of_v = false;

template<template<auto...> typename Template,
        auto... Args>
inline constexpr bool is_value_instantiation_of_v<Template,
        Template<Args...>> = true;

namespace detail {
template<typename Tuple, typename F, std::size_t... Idx>
void for_each_indexed_impl(Tuple &&tuple, F &&function, std::index_sequence<Idx...> /* indices */) {
    (std::invoke(function, Idx, std::get<Idx>(std::forward<Tuple>(tuple))), ...);
}

template<typename Tuple, typename F, std::size_t... Idx>
void for_each_impl(Tuple &&tuple, F &&function, std::index_sequence<Idx...> /* indices */) {
    (std::invoke(function, std::get<Idx>(std::forward<Tuple>(tuple))), ...);
}
} // namespace detail

// Like std::for_each, but for traverses values in a tuple.
// Accepts a callable with two arguments -- the index and the value
// of a field in the tuple
template<typename Tuple, typename Function /* (int, value) -> void */>
void for_each_indexed(Tuple &&tuple, Function &&function) {
    constexpr auto size = std::tuple_size<std::remove_cvref_t<Tuple>>::value;
    detail::for_each_indexed_impl(std::forward<Tuple>(tuple), std::forward<Function>(function), std::make_index_sequence<size>{});
}

// Like std::for_each, but for traverses values in a tuple.
// Accepts a unary callable which accepts only the value of a tuple field
template<typename Tuple, typename Function /* (value) -> void */>
void for_each(Tuple &&tuple, Function &&function) {
    constexpr auto size = std::tuple_size<std::remove_cvref_t<Tuple>>::value;
    detail::for_each_impl(std::forward<Tuple>(tuple), std::forward<Function>(function), std::make_index_sequence<size>{});
}

// A helper function to convert references to pointers.
// Useful when you want to be able to handle both references and pointers
// in the same generic code.
template<typename T>
auto to_pointer(T &value) {
    if constexpr (!std::is_pointer_v<T>) {
        return &value;
    } else {
        return value;
    }
}

// Empty type instead of regular void (void values)
struct regular_void {};

} // namespace yaz::meta

#endif // include guard

