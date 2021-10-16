
#include <yaz/Meta.hpp>

#include <list>
#include <string>
#include <vector>

// This file just has compile-time tests,
// it is not called from anywhere

static_assert(yaz::meta::is_instantiation_of_v<std::basic_string, std::string>);
static_assert(yaz::meta::is_instantiation_of_v<std::vector, std::vector<int>>);
static_assert(not yaz::meta::is_instantiation_of_v<std::list, std::vector<int>>);
static_assert(not yaz::meta::is_instantiation_of_v<std::list, int>);

template<std::size_t S>
struct value_parametrised {};

static_assert(yaz::meta::is_value_instantiation_of_v<value_parametrised, value_parametrised<0>>);
static_assert(not yaz::meta::is_instantiation_of_v<std::list, int>);

