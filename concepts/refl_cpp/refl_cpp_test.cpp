#include <any>
#include <cassert>
#include <iostream>
#include <refl.hpp>
#include <string>
#include <unordered_map>
#include <vector>

struct serializable : refl::attr::usage::field, refl::attr::usage::function {};

template<typename T>
void serialize(std::ostream &os, T &&value) {
    os << "\n{\n";
    bool add_comma = false;
    for_each(refl::reflect(value).members, [&](auto member) {
        if constexpr (is_readable(member) && refl::descriptor::has_attribute<serializable>(member)) {
            if (add_comma)
                os << ",\n";
            os << "\"" << get_display_name(member) << "\":";
            os << "\"" << member(value) << "\"";
            add_comma = true;
        }
    });
    os << "\n}\n";
}

struct Test {
    float f = 0.0;
    int i = 0;
    std::string s = "";
};

void debug_test(std::ostream &os, const Test &context) {
    os << "(" << context.f << ", " << context.i << ", " << context.s << ")";
}

REFL_TYPE(Test, debug(debug_test), bases<>)
REFL_FIELD(f, serializable())
REFL_FIELD(i, serializable())
REFL_FIELD(s, serializable())
REFL_END

int main() {
    std::cout << "Make JSON: ";
    serialize(std::cout, Test{ 1, 2, "Three" });

    std::cout << "Use debug print function: \n";
    std::vector<Test> tests{ { 0, 1, "Hello" }, { 1, 0, "World" } };
    refl::runtime::debug(std::cout, tests);

    std::cout << std::endl;
}
