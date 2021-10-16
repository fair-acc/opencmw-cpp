
#include <tuple>

#include <yaz/Meta.hpp>

#include <catch2/catch.hpp>

TEST_CASE("For each indexed", "[tuple]") {
    std::tuple<int, float, double> ts{ 1, 2.0f, 3.0 };

    std::size_t                    total_size = 0;
    yaz::meta::for_each_indexed(ts, [&](std::size_t index, auto value) {
        total_size += (index + 1) * sizeof(value);
    });

    REQUIRE(total_size == sizeof(int) + 2 * sizeof(float) + 3 * sizeof(double));
}

TEST_CASE("For each", "[tuple]") {
    std::tuple<int, float, double> ts{ 1, 2.0f, 3.0 };

    std::size_t                    total_size = 0;
    yaz::meta::for_each(ts, [&](auto value) {
        total_size += sizeof(value);
    });

    REQUIRE(total_size == sizeof(int) + sizeof(float) + sizeof(double));
}

