#ifndef OPENCMW_UTILS_H
#define OPENCMW_UTILS_H
#include "IoSerialiser.hpp"
#include <array>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <string_view>
#include <type_traits>
#include <vector>

namespace opencmw::utils {

template<std::string_view const &...strings>
struct join {
    // Join all strings into a single std::array of chars
    static constexpr auto impl() noexcept {
        constexpr std::size_t     len = (strings.size() + ... + 0);
        std::array<char, len + 1> concatenatedArray{};
        auto                      append = [i = 0, &concatenatedArray](auto const &s) mutable {
            for (auto c : s) {
                concatenatedArray[i++] = c;
            }
        };
        (append(strings), ...);
        concatenatedArray[len] = 0;
        return concatenatedArray;
    }
    // Give the joined string static storage
    static constexpr auto array = impl();
    // View as a std::string_view
    static constexpr std::string_view value{ array.data(), array.size() - 1 };
};
// Helper to get the value out
template<std::string_view const &...string>
static constexpr auto join_v = join<string...>::value;

inline int            getClassInfoVerbose() {
    static int i = std::ios_base::xalloc();
    return i;
}

template<typename CharT, typename Traits>
std::basic_ostream<CharT, Traits> &ClassInfoVerbose(std::basic_ostream<CharT, Traits> &os) {
    os.iword(getClassInfoVerbose()) = true;
    return os;
}

template<typename CharT, typename Traits>
std::basic_ostream<CharT, Traits> &ClassInfoShort(std::basic_ostream<CharT, Traits> &os) {
    os.iword(getClassInfoVerbose()) = false;
    return os;
}

inline int getClassInfoIndent() {
    static int i = std::ios_base::xalloc();
    return i;
}

inline int getClassInfoIndentStep() {
    static int i = std::ios_base::xalloc();
    return i;
}

inline std::ostream &ClassInfoIndentStep(std::ostream &os, const uint32_t indentValue) {
    os.iword(getClassInfoIndentStep()) += indentValue;
    return os;
}
inline std::ostream &ClassInfoIndentInc(std::ostream &os) {
    os.iword(getClassInfoIndent()) += 1;
    return os;
}
inline std::ostream &ClassInfoIndentDec(std::ostream &os) {
    os.iword(getClassInfoIndent()) -= 1;
    return os;
}

template<ArrayOrVector T>
// requires (!isAnnotated<T>())
inline std::ostream &operator<<(std::ostream &os, const T &v) {
    os << '{';
    for (std::size_t i = 0; i < v.size(); ++i) {
        os << v[i];
        if (i != v.size() - 1) {
            os << ", ";
        }
    }
    os << "}";
    return os;
}

template<typename Rep, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
inline std::ostream &operator<<(std::ostream &os, const Annotated<Rep, Q, description, modifier, groups...> &annotatedValue) {
    if (os.iword(getClassInfoVerbose())) {
        if constexpr (!is_array<Rep> && !is_vector<Rep>) {
            os << fmt::format("{:<5}  // [{}] - {}", annotatedValue.value(), annotatedValue.getUnit(), annotatedValue.getDescription()); // print as number
        } else {
            os << fmt::format("{}  // [{}] - {}", annotatedValue.value(), annotatedValue.getUnit(), annotatedValue.getDescription()); // print as array
        }
        return os;
    }
    os << fmt::format("{}", annotatedValue.value()); // print as number
    return os;
}

template<typename T>
inline std::ostream &operator<<(std::ostream &os, const std::unique_ptr<T> &v) {
    if (v) {
        return os << "unique_ptr{" << (*v.get()) << '}';
    } else {
        return os << "unique_ptr{nullptr}";
    }
}

template<typename T>
inline std::ostream &operator<<(std::ostream &os, const std::shared_ptr<T> &v) {
    if (v) {
        return os << "shared_ptr{" << (*v.get()) << '}';
    } else {
        return os << "shared_ptr{nullptr}";
    }
}

template<MapLike T>
inline std::ostream &operator<<(std::ostream &os, const T &map) {
    os << '{';
    bool first = true;
    for (auto const &[key, val] : map) {
        if (first) {
            first = false;
        } else {
            os << ", ";
        }
        os << key << ':' << val;
    }
    os << "}";
    return os;
}

template<ReflectableClass T>
inline std::ostream &operator<<(std::ostream &os, const T &value) {
    const bool    verbose    = os.iword(getClassInfoVerbose());
    const int64_t indent     = os.iword(getClassInfoIndent());
    const int64_t indentStep = os.iword(getClassInfoIndentStep()) == 0 ? (os.iword(getClassInfoIndentStep()) = 2) : os.iword(getClassInfoIndentStep());
    os << fmt::format("{}({}", (indent == 0) ? refl::reflect(value).name.data : "", verbose ? "\n" : "");
    using ValueType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(value)))>;
    if constexpr (isReflectableClass<ValueType>()) {
        for_each(
                refl::reflect(value).members, [&](const auto member, const auto index) constexpr {
                    using MemberType          = std::remove_reference_t<decltype(unwrapPointer(member(value)))>;
                    const auto &typeNameShort = typeName<MemberType>;
                    // const auto& typeNameShort = refl::reflect(getAnnotatedMember(member(value))).name.data; // alt type-definition:
                    if (verbose) {
                        os << fmt::format("{:{}} {}: {:<25} {:<35}= ", "", indent * indentStep + 1, index, typeNameShort, get_debug_name(member));
                    } else {
                        os << fmt::format("{}{}=", (index > 0) ? ", " : "", get_display_name(member));
                    }
                    ClassInfoIndentInc(os);
                    os << member(value);
                    ClassInfoIndentDec(os);
                    os << (verbose ? "\n" : ""); // calls this operator<< if necessary
                });
        os << fmt::format("{:{}})", "", verbose ? (indent * indentStep + 1) : 0);
    }
    return os;
}

template<typename T>
inline constexpr void diffView(std::ostream &os, const T &lhs, const T &rhs) {
    const bool    verbose    = os.iword(getClassInfoVerbose());
    const int64_t indent     = os.iword(getClassInfoIndent());
    const int64_t indentStep = os.iword(getClassInfoIndentStep()) == 0 ? (os.iword(getClassInfoIndentStep()) = 2) : os.iword(getClassInfoIndentStep());
    using ValueType          = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(lhs)))>;
    if constexpr (isReflectableClass<ValueType>()) {
        os << fmt::format("{}{}({}", (indent == 0) ? "diffView: " : "", (indent == 0) ? refl::reflect(lhs).name.data : "", verbose ? "\n" : "");
        for_each(
                refl::reflect(lhs).members, [&](const auto member, const auto index) constexpr {
                    if constexpr (is_field(member)) {
                        using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(lhs))))>;
                        if (verbose) {
                            os << fmt::format("{:{}} {}: {:<20} {:<30}= ", "", indent * indentStep + 1, index, typeName<MemberType>, fmt::format("{}::{}", member.declarator.name, member.name));
                        } else {
                            os << fmt::format("{}{}=", (index > 0) ? ", " : "", member.name);
                        }
                        ClassInfoIndentInc(os);
                        if constexpr (isReflectableClass<MemberType>()) {
                            using UnwrappedMemberType = std::remove_reference_t<decltype(member(lhs))>;
                            if constexpr (is_smart_pointer<std::remove_reference_t<UnwrappedMemberType>>) {
                                // check for empty smart pointer
                                if (!member(lhs) || !member(rhs)) {
                                    // clang-format off
                                    if (!member(lhs) && !member(rhs)) { os << "{nullPointer}" << (verbose ? "\n" : ""); return; }
                                    os << fmt::format(fg(fmt::color::red), "differ: ");
                                    if (member(lhs)) { os << *member(lhs).get(); } else { os << "{nullPointer}"; }
                                    os << " vs. ";
                                    if (member(rhs)) { os << *member(rhs).get(); } else { os << "{nullPointer}"; }
                                    os << (verbose ? "\n" : ""); // calls this operator<< if necessary
                                    // clang-format on
                                    return;
                                }
                            }
                            // non-empty member structs, dive further into hierarchy
                            diffView(os, getAnnotatedMember(unwrapPointer(member(lhs))), getAnnotatedMember(unwrapPointer(member(rhs))));
                        } else {
                            if (member(lhs) == member(rhs)) {
                                os << member(lhs);
                            } else {
                                diffView(os, member(lhs), member(rhs));
                            }
                        }
                        ClassInfoIndentDec(os);
                        os << (verbose ? "\n" : ""); // calls this operator<< if necessary
                    }
                });
        os << fmt::format("{:{}})", "", verbose ? (indent * indentStep + 1) : 0);
    } else {
        // primitive type
        auto lhsValue = getAnnotatedMember(unwrapPointer(lhs));
        auto rhsValue = getAnnotatedMember(unwrapPointer(rhs));
        if (lhsValue == rhsValue) {
            os << lhsValue;
        } else {
            os << fmt::format(fg(fmt::color::red), "{} vs. {}", lhsValue, rhsValue); // coloured terminal output
        }
    }
    if (indent == 0) {
        os << std::endl;
    }
}

inline std::ostream &operator<<(std::ostream &os, const DeserialiserInfo &info) {
    os << typeName<DeserialiserInfo> << "\nset fields:\n";
    if (!info.setFields.empty()) {
        for (auto fieldMask : info.setFields) {
            os << "   class '" << fieldMask.first << "' bit field: " << fieldMask.second << '\n';
        }
    }
    if (!info.additionalFields.empty()) {
        os << "additional fields:\n";
        for (auto e : info.additionalFields) {
            os << "    field name: " << std::get<0>(e) << " typeID: " << std::get<1>(e) << '\n';
        }
    }
    if (!info.exceptions.empty()) {
        os << "thrown exceptions:\n";
        int count = 0;
        for (auto e : info.exceptions) {
            os << "    " << (count++) << ": " << e << '\n';
        }
    }
    return os;
}

} // namespace opencmw::utils

template<opencmw::ReflectableClass T>
struct fmt::formatter<T> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(T const &value, FormatContext &ctx) {
        using namespace opencmw::utils; // for operator<< overloading
        std::stringstream ss;           // N.B. std::stringstream construct to avoid recursion with 'operator<<' definition
        ss << value << std::flush;
        return fmt::format_to(ctx.out(), "{}", ss.str());
    }
};

#endif // OPENCMW_UTILS_H
