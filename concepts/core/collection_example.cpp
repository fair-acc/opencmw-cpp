#include <cassert>
#include <chrono>
#include <format>
#include <print>

#include <collection.hpp>

template<typename T>
concept Arithmetic = std::is_arithmetic_v<T>;

template<Arithmetic... Ts>
void function(opencmw::collection<Ts...> &c) {
    std::size_t foo = 1;
    c.visit([](auto &arg) noexcept { arg *= 1; },
            [&foo](size_t &arg) noexcept { arg *= foo; },
            [](float &arg) noexcept { arg *= 1; },
            [](char &arg) noexcept { arg *= 1; });
}
/**
 * Example to demonstrate the compile-time type-constrained collection
 */
int main() {
    using opencmw::collection;
    // some simple benchmarks for writing/reading (visiting)
    const auto nRepetitions = 1'000'000U;
    auto       time_start   = std::chrono::system_clock::now();

    // explicit declaration
    collection<size_t, float, char> c;
    // alternate list-style construction:
    [[maybe_unused]] auto c1 = collection{ 1LU, 1.0f, 'a' };
    c1.push_back('b');
    c1.push_back('c');
    [[maybe_unused]] collection c2 = { 1LU, 1.0f, 'a', 'b', 'c' };
    assert(c1 == c2);

    for (size_t i = 0; i < nRepetitions; ++i) {
        c.push_back(i);
        c.push_back(static_cast<float>(i));
        c.push_back(static_cast<char>(i % 256));
        // c.push_back(static_cast<short>(i % 256)); // correctly fails - 'short' not part of class definition
    }
    for (size_t i = 0; i < nRepetitions; ++i) {
        function(c);
    }

    std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
    std::print("push_back and visit (fixed types): {} milliseconds or {} ops/s\n", time_elapsed.count(), (1000 * nRepetitions / time_elapsed.count()));
}
