#include <MultiArray.hpp>
#include <catch2/catch.hpp>
#include <iostream>

using namespace opencmw;

TEST_CASE("MultiArray.doubleSimple", "[MultiArray]") {
    MultiArray<double, 2> test({1,2,3,4,5,6}, {2,3});
    // test that internal fields where computed correctly
    REQUIRE(2 == test.n(0));
    REQUIRE(3 == test.n(1));
    REQUIRE(3 == test.stride(0));
    REQUIRE(1 == test.stride(1));
    REQUIRE(0 == test.offset(0));
    REQUIRE(0 == test.offset(1));
    // test index computation
    REQUIRE(0 == test.index({0,0}));
    REQUIRE(1 == test.index({0,1}));
    REQUIRE(2 == test.index({0,2}));
    REQUIRE(3 == test.index({1,0}));
    REQUIRE(4 == test.index({1,1}));
    REQUIRE(5 == test.index({1,2}));
    // test reverse index computation
    REQUIRE(std::array<uint32_t, 2>{0,0} == test.indices(0));
    REQUIRE(std::array<uint32_t, 2>{0,1} == test.indices(1));
    REQUIRE(std::array<uint32_t, 2>{0,2} == test.indices(2));
    REQUIRE(std::array<uint32_t, 2>{1,0} == test.indices(3));
    REQUIRE(std::array<uint32_t, 2>{1,1} == test.indices(4));
    REQUIRE(std::array<uint32_t, 2>{1,2} == test.indices(5));
    // multi index access
    REQUIRE(1.0 == test(0U,0U));
    REQUIRE(3.0 == test(0U,2U));
    REQUIRE(4.0 == test(1U,0U));
    REQUIRE(6.0 == test(1U,2U));

    // linear access
    REQUIRE(4.0 == test[3]);

    // assignment
    test[{1,2}] = 42.23;
    REQUIRE(42.23 == (test[{1,2}])); // why are the parentheses necessary? bracket should have precedence over equality

    test[3] = 13.37;
    REQUIRE(13.37 == test[3]);

    // get row
    // auto second_row = test.slice(0, 1);
    // REQUIRE(13.37 == second_row[0])

    // boolean
    MultiArray<bool, 3> test2({2,3,4});
    std::cout << std::boolalpha << test2 << std::endl;

    // open questions:
    // - how to react to dimension mismatches?
    // - allow runtime variable number of dimensions?
    // - copy and mutability

    // use with units
    //using namespace units;
    //using namespace units::physical;
    //using namespace units::physical::si;
    //using namespace units::physical::si;
    //using namespace units::physical::si::literals;
    //Length<kilometre> auto blub= 34.5_q_km;
    //std::cout << quantity_cast<si::metre>(2.0_q_km) << ", " << 24.3 << std::endl;
    //auto with_units = MultiArray(std::vector<si::length<si::kilometre>>(2.0_q_km, 2.5_q_km, 3.3_q_km), std::array<size_t, 2>{1,3});
    //std::cout << with_units << std::endl;
}

TEST_CASE("MultiArray.floatOrder3Offsets", "[MultiArray]") {
    std::vector<float> elements(100);
    std::iota(elements.begin(), elements.end(), 0);
    MultiArray<float, 3> test(elements, {2, 3, 4}, {14,4,1}, {0,0,0});
    std::cout << test << std::endl;
    // test that internal fields where set correctly
    REQUIRE(2 == test.n(0));
    REQUIRE(3 == test.n(1));
    REQUIRE(4 == test.n(2));
    REQUIRE(14 == test.stride(0));
    REQUIRE(4 == test.stride(1));
    REQUIRE(1 == test.stride(2));
    REQUIRE(0 == test.offset(0));
    REQUIRE(0 == test.offset(1));
    REQUIRE(0 == test.offset(2));
    // multi index access
    REQUIRE(0.0f == test(0U,0U,0U));
    REQUIRE(8.0f == test(0U,2U,0U));
    REQUIRE(14.0f == test(1U,0U,0U));
    REQUIRE(22.0f == test(1U,2U,0U));
    REQUIRE(3.0f == test(0U,0U,3U));
    REQUIRE(11.0f == test(0U,2U,3U));
    REQUIRE(17.0f == test(1U,0U,3U));
    REQUIRE(25.0f == test(1U,2U,3U));
}