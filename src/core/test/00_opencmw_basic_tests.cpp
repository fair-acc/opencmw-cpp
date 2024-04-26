#include <catch2/catch.hpp>
#include <opencmw.hpp>

namespace opencmw::test {

/**** FWD(..) short-hand macro tests ***/
constexpr int    fwd_func(const int &) { return 1; }                                              // '1' -> lvalue received in final function
constexpr int    fwd_func(int &&) { return 2; }                                                   // '2' -> rvalue received in final function
constexpr int    no_fwd_wrapper(auto const &v) { return fwd_func(v); }                            // non-forwarding wrapper
constexpr int    fwd_wrapper(auto &&v) { return fwd_func(FWD(v)); }                               // forwarding wrapper - short notation
constexpr int    fwd_wrapper_verbose(auto &&v) { return fwd_func(std::forward<decltype(v)>(v)); } // forwarding wrapper - long notation

static const int lvalue = 0;
static_assert(fwd_func(lvalue) == 1);
static_assert(fwd_func(0) == 2);
static_assert(no_fwd_wrapper(lvalue) == 1, "l-value forwarding");
static_assert(fwd_wrapper_verbose(lvalue) == 1, "l-value forwarding");
static_assert(fwd_wrapper(lvalue) == 1, "l-value forwarding");
static_assert(no_fwd_wrapper(0) == 1, "r-value non-forwarding -> converted to lvalue '1'");
static_assert(fwd_wrapper_verbose(0) == 2, "r-value forwarding");
static_assert(fwd_wrapper(0) == 2, "r-value forwarding");

/**** power 2 and bit-magic tests ***/
static_assert(isPower2(1024U));
static_assert(is_power2_v<1024U>);
static_assert(floorlog2(1) == 0);
static_assert(floorlog2(1023) == 9);
static_assert(floorlog2(1024) == 10);
static_assert(floorlog2(1025) == 10);
static_assert(ceillog2(1) == 0);
static_assert(ceillog2(1023) == 10);
static_assert(ceillog2(1024) == 10);
static_assert(ceillog2(1025) == 11);

/**** isReflectableClass() tests ***/
struct NonReflStruct {
    int a;
};
struct ReflStruct {
    int a;
};
} // namespace opencmw::test
ENABLE_REFLECTION_FOR(opencmw::test::ReflStruct, a);

namespace opencmw::test {
static_assert(!isReflectableClass<NonReflStruct>(), "non-reflectable class");
static_assert(isReflectableClass<ReflStruct>(), "reflectable class");
constexpr int refl_test(ReflectableClass auto const &) {
    return 1;
}
static_assert(refl_test(ReflStruct{}), "reflectable class");

/**** is_supported_number<> tests ***/
static_assert(is_supported_number<char>);
static_assert(is_supported_number<int8_t>);
static_assert(is_supported_number<uint8_t>);
static_assert(is_supported_number<int16_t>);
static_assert(!is_supported_number<uint16_t>, "unsigned integral not supported");
static_assert(is_supported_number<int32_t>);
static_assert(!is_supported_number<uint32_t>, "unsigned integral not supported");
static_assert(is_supported_number<int64_t>);
static_assert(!is_supported_number<uint64_t>, "unsigned integral not supported");
static_assert(is_supported_number<float>);
static_assert(is_supported_number<double>);

/**** is_stringlike<> tests ***/
static_assert(is_stringlike<std::string>);
static_assert(is_stringlike<std::string_view>);
static_assert(!is_stringlike<char>);
static_assert(!is_stringlike<decltype("")>);
static_assert(!is_stringlike<double>);
static_assert(!is_stringlike<std::vector<char>>);
static_assert(!is_stringlike<std::array<char, 0>>);

/**** MapLike<> tests ***/
static_assert(is_map_like<std::map<int, int>>);
static_assert(is_map_like<std::unordered_map<int, int>>);
static_assert(!is_map_like<std::array<char, 0>>);
const std::map<int, int>           testMap;
const std::unordered_map<int, int> testUnorderedMap;
static_assert([](MapLike auto &) { return true; }(testMap));
static_assert([](MapLike auto &) { return true; }(testUnorderedMap));

/**** is_array<> and is_vector tests ***/
static_assert(is_array<std::array<int, 2>>);
static_assert(!is_array<std::vector<int>>);
static_assert(!is_vector<std::array<int, 2>>);

const std::array<int, 0> testArray{};
const std::vector<int>   testVector;
static_assert([](ArrayOrVector auto &) { return true; }(testArray));
static_assert([](ArrayOrVector auto &) { return true; }(testVector));

const std::array<std::string, 0> testStringArray{};
const std::vector<std::string>   testStringVector;
static_assert([](StringArray auto &) { return true; }(testStringArray));
static_assert([](StringArray auto &) { return true; }(testStringVector));

} // namespace opencmw::test
