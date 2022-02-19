#ifndef OPENCMW_CPP_COLLECTION_H
#define OPENCMW_CPP_COLLECTION_H

#include <algorithm>
#include <tuple>
#include <vector>

#include <opencmw.hpp>

namespace opencmw {

/**
 * <h2>compile-time type-constrained collection</h2>
 *
 * <p>
 * It is intended to be used with concept constraints to exchange collections of heterogeneous types <br>
 * as parameters between user-code and methods, e.g. (N.B. 'DataSet' being a concept type-constraint):
 * @code
 * template &#60;DataSet... Ts&#62;
 * void func(const opencmw::collection&#60;Ts...&#62; &) { ... }
 *
 * <b>usage examples:</b>
 * using opencmw::collection;
 * collection&#60;size_t, float, char&#62; c {1LU, 1.0f, 'a'};
 *   c.push_back('c');
 *
 *   c.visit([](size_t &arg) noexcept { ... }, &#47;&#47; need one type-specific visitor for each type:
 *     [](float &arg) noexcept { ... },
 *     [](char &arg) noexcept { ... },
 *     []<typename T>(T &arg) noexcept { ... }); &#47;&#47; and/or: generic type fall-back visitor lambda.
 *
 * <b>alternate list-style construction:</b>
 * auto       c1 = collection{1LU, 1.0f, 'c'};
 * collection c2 = {1LU, 1.0f, 'c'};
 *
 * @authors Matthias Kretz (@mattkretz), Ralph J. Steinhagen (@RalphSteinhagen)
 */
template<typename... Ts>
class collection;

template<typename... Ts>
requires is_uniq<Ts...>::value class collection<Ts...> {
    std::tuple<std::vector<Ts>...> _items;

public:
    // clang-format off
    constexpr collection() noexcept = default;
    template <typename... Us>
    requires std::conjunction_v<is_in_list<std::decay_t<Us>, Ts...>...>
    explicit constexpr collection(Us &&...elements) noexcept { (..., push_back(std::forward<Us>(elements))); }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return (... + std::get<std::vector<Ts> >(_items).size()); }
    [[nodiscard]] constexpr bool        empty() const noexcept { return (... && std::get<std::vector<Ts> >(_items).empty()); }
    void                                clear() noexcept { (..., std::get<std::vector<Ts> >(_items).clear()); }
    void                                reserve(std::size_t count) { std::apply([&count]<typename... T>(T & ...v) { (..., v.reserve(count)); }, _items);}
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { std::size_t size = 0; std::apply([&size]<typename... T>(T & ...v) { (..., (size = std::max(size, v.capacity()))); }, _items); return size; }
    // clang-format on

    template<typename T>
    requires std::disjunction_v<std::is_same<std::decay_t<T>, Ts>...>
    void push_back(T &&e) {
        std::get<std::vector<std::decay_t<T>>>(_items).push_back(std::forward<T>(e));
    }

    template<typename T>
    requires std::disjunction_v<std::is_same<std::decay_t<T>, Ts>...>
    void remove(T &&e) noexcept {
        auto &vec = std::get<std::vector<std::decay_t<T>>>(_items);
        vec.erase(std::ranges::remove(vec, std::forward<T>(e)).begin(), vec.end());
    }

    template<typename... F>
    requires(sizeof...(F) > 0) void visit(F &&...visitors) {
        overloaded visitor{ std::forward<F>(visitors)... };
        (..., std::ranges::for_each(std::get<std::vector<Ts>>(_items), visitor));
    }

    constexpr auto operator<=>(const collection &) const = default;

private:
    template<class... Us>
    struct overloaded : Us... { using Us::operator()...; };
};

template<typename... Ts>
requires(!is_uniq<Ts...>::value) class collection<Ts...> : public filter_duplicates_t<collection, Ts...> {
    using Base = filter_duplicates_t<collection, Ts...>;

public:
    constexpr collection() noexcept              = default;
    constexpr collection(const collection &)     = default;
    constexpr collection(collection &&) noexcept = default;

    template<typename... Us>
    requires std::conjunction_v<is_in_list<std::decay_t<Us>, Ts...>...>
    constexpr collection(Us &&...args)
        : Base(std::forward<Us>(args)...) {}
};

// deduction guide is necessary
template<typename... Ts>
collection(Ts...) -> collection<Ts...>;

} // namespace opencmw

#endif // OPENCMW_CPP_COLLECTION_H
