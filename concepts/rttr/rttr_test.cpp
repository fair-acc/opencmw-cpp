#include <iostream>
#include <rttr/registration>
#include <sstream>
#include <string>
#include <utility>

static void write_arithmetic_property(std::stringstream &ss, const rttr::variant &value, const rttr::type &type) {
    if (type == rttr::type::get<bool>()) {
        ss << value.to_bool();
    } else if (type == rttr::type::get<char>()) {
        ss << value.to_uint8();
    } else if (type == rttr::type::get<int8_t>()) {
        ss << value.to_int8();
    } else if (type == rttr::type::get<int16_t>()) {
        ss << value.to_int16();
    } else if (type == rttr::type::get<int32_t>()) {
        ss << value.to_int32();
    } else if (type == rttr::type::get<int64_t>()) {
        ss << value.to_int64();
    } else if (type == rttr::type::get<float>()) {
        ss << value.to_float();
    } else if (type == rttr::type::get<double>()) {
        ss << value.to_double();
    }
}

static bool is_string(const rttr::variant &value) {
    bool ret = false;
    value.to_string(&ret);
    return ret;
}

static bool is_uint64(const rttr::variant &value) {
    bool ret = false;
    value.to_uint64(&ret);
    return ret;
}

static void write_enum_property(std::stringstream &ss, const rttr::variant &value) {
    if (is_string(value)) {
        ss << value.to_string();
    } else if (is_uint64(value)) {
        ss << value.to_uint64();
    }
}

static void write_string_property(std::stringstream &ss, const rttr::variant &value) {
    ss << value.to_string();
}

auto get_display_name(const rttr::property &prop) {
    return prop.get_name();
}

std::string member(const rttr::property &prop, const rttr::instance &value) {
    std::stringstream ss;
    rttr::variant prop_value = prop.get_value(value);
    rttr::type value_type = prop_value.get_type();
    if (value_type.is_arithmetic()) {
        write_arithmetic_property(ss, prop_value, value_type);
    } else if (value_type.is_enumeration()) {
        write_enum_property(ss, prop_value);
    } else if (value_type == rttr::type::get<std::string>()) {
        write_string_property(ss, prop_value);
    }
    return ss.str();
}

void serialize(std::ostream &os, const rttr::instance &value) {
    os << "\n{\n";
    bool add_comma = false;
    for (auto prop : value.get_type().get_properties()) {
        if (add_comma)
            os << ",\n";
        os << "\"" << get_display_name(prop) << "\":";
        os << "\"" << member(prop, value) << "\"";
        add_comma = true;
    }
    os << "\n}\n";
}

struct Test {
    float f = 0.0;
    int i = 0;
    std::string s;
};

RTTR_REGISTRATION {
    rttr::registration::class_<Test>("Test")
            .property("f", &Test::f)
            .property("i", &Test::i)
            .property("s", &Test::s);
}

int main() {
    std::cout << "Make JSON: ";
    Test t = { 1.23f, 2, "Three" };
    serialize(std::cout, t);
    std::cout << std::endl;
}
