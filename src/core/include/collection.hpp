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
class collection {
    tuple_unique<std::tuple<std::vector<Ts>...>> _items;

public:
    // clang-format off
    constexpr collection() noexcept = default;
    constexpr collection(Ts... element) noexcept { (..., push_back(element)); }
    [[nodiscard]] constexpr std::size_t size() const noexcept { std::size_t size = 0; std::apply([&size]<typename... T>(T & ...v) { (..., (size += v.size())); }, _items); return size; }
    [[nodiscard]] constexpr bool        empty() const noexcept { return size() == 0; }
    void                                clear() noexcept { std::apply([]<typename... T>(T & ...v) { (..., v.clear()); }, _items);}
    void                                reserve(std::size_t count) { std::apply([&count]<typename... T>(T & ...v) { (..., v.reserve(count)); }, _items);}
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { std::size_t size = 0; std::apply([&size]<typename... T>(T & ...v) { (..., (size = std::max(size, v.capacity()))); }, _items); return size; }
    // clang-format on

    template<typename T>
    requires std::disjunction_v<std::is_same<std::remove_cvref_t<T>, Ts>...>
    void push_back(T &&e) {
        std::get<std::vector<std::remove_cvref_t<T>>>(_items).push_back(std::forward<T>(e));
    }

    template<typename T>
    requires std::disjunction_v<std::is_same<std::remove_cvref_t<T>, Ts>...>
    void remove(T &&e) noexcept {
        auto &vec = std::get<std::vector<std::remove_cvref_t<T>>>(_items);
        if (const auto pos = std::ranges::find(vec.begin(), vec.end(), e); pos != vec.end()) {
            vec.erase(pos, pos + 1);
        }
    }

    template<typename... F>
    requires(sizeof...(F) > 0) void visit(F &&...visitors) {
        overloaded visitor{ std::forward<F>(visitors)... };
        std::apply([&visitor](auto &&...vs) {
            (..., [&visitor](auto &v) {
                std::ranges::for_each(v, [&visitor](auto &&e) { visitor(std::forward<decltype(e)>(e)); });
            }(std::forward<decltype(vs)>(vs)));
        },
                _items);
    }

    template<typename... Ts1, typename... Ts2>
    friend constexpr auto operator<=>(const collection<Ts1...> &lhs, const collection<Ts2...> &rhs) noexcept;
    template<typename... Ts1, typename... Ts2>
    friend constexpr bool operator==(const collection<Ts1...> &lhs, const collection<Ts2...> &rhs) noexcept;

private:
    template<class... Us>
    struct overloaded : Us... { using Us::operator()...; };
};

template<typename... Ts1, typename... Ts2>
constexpr auto operator<=>(const collection<Ts1...> &lhs, const collection<Ts2...> &rhs) noexcept { return lhs._items <=> rhs._items; }
template<typename... Ts1, typename... Ts2>
constexpr bool operator==(const collection<Ts1...> &lhs, const collection<Ts2...> &rhs) noexcept { return lhs._items == rhs._items; }

} // namespace opencmw

#endif // OPENCMW_CPP_COLLECTION_H
