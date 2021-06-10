#ifndef OPENCMW_UTILS_H
#define OPENCMW_UTILS_H
#include <IoClassSerialiser.hpp>
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

std::ostream &ClassInfoIndentStep(std::ostream &os, const uint32_t indentValue) {
    os.iword(getClassInfoIndentStep()) += indentValue;
    return os;
}
std::ostream &ClassInfoIndentInc(std::ostream &os) {
    os.iword(getClassInfoIndent()) += 1;
    return os;
}
std::ostream &ClassInfoIndentDec(std::ostream &os) {
    os.iword(getClassInfoIndent()) -= 1;
    return os;
}

template<AnnotatedType T>
std::ostream &operator<<(std::ostream &os, const T &annotatedValue) {
    if (os.iword(getClassInfoVerbose())) {
        if constexpr (!is_array_or_vector<decltype(annotatedValue.value)>::value) {
            os << fmt::format("{:<5}  // [{}] - {}", annotatedValue.value, annotatedValue.getUnit(), annotatedValue.getDescription()); // print as number
        } else {
            os << fmt::format("{}  // [{}] - {}", annotatedValue.value, annotatedValue.getUnit(), annotatedValue.getDescription()); // print as array
        }
        return os;
    }
    os << fmt::format("{}", annotatedValue.value); // print as number
    return os;
}

template<typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<T> &v) {
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

template<typename T, size_t size>
std::ostream &operator<<(std::ostream &os, const std::array<T, size> &v) {
    os << '{';
    for (std::size_t i = 0; i < size; ++i) {
        os << v[i];
        if (i != size - 1) {
            os << ", ";
        }
    }
    os << "}";
    return os;
}

template<typename T>
std::ostream &operator<<(std::ostream &os, const std::unique_ptr<T> &v) {
    if (v) {
        return os << "unique_ptr{" << *v.get() << '}';
    } else {
        return os << "unique_ptr{nullptr}";
    }
}

template<typename T>
std::ostream &operator<<(std::ostream &os, const std::shared_ptr<T> &v) {
    if (v) {
        return os << "shared_ptr{" << *v.get() << '}';
    } else {
        return os << "shared_ptr{nullptr}";
    }
}

template<ReflectableClass T>
std::ostream &operator<<(std::ostream &os, const T &value) {
    const bool    verbose    = os.iword(getClassInfoVerbose());
    const int64_t indent     = os.iword(getClassInfoIndent());
    const int64_t indentStep = os.iword(getClassInfoIndentStep()) == 0 ? (os.iword(getClassInfoIndentStep()) = 2) : os.iword(getClassInfoIndentStep());
    os << fmt::format("{}({}", (indent == 0) ? refl::reflect(value).name.data : "", verbose ? "\n" : "");
    using ValueType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(value)))>;
    if constexpr (isReflectableClass<ValueType>()) {
        for_each(
                refl::reflect(value).members, [&](const auto member, const auto index) constexpr {
                    using MemberType          = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(value))))>;
                    const auto &typeNameShort = typeName<MemberType>();
                    //const auto& typeNameShort = refl::reflect(getAnnotatedMember(member(value))).name.data; // alt type-definition:
                    if (verbose) {
                        os << fmt::format("{:{}} {}: {:<20} {:<30}= ", "", indent * indentStep + 1, index, typeNameShort, get_debug_name(member));
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
    //TODO:: add fmt::formatter specialisation see: https://fmt.dev/latest/api.html#formatting-user-defined-types
}

template<typename T>
constexpr void diffView(std::ostream &os, const T &lhs, const T &rhs) {
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
                            const std::string fieldName(member.declarator.name + "::" + member.name);
                            os << fmt::format("{:{}} {}: {:<20} {:<30}= ", "", indent * indentStep + 1, index, typeName<MemberType>(), fieldName);
                        } else {
                            const std::string fieldName(member.name);
                            os << fmt::format("{}{}=", (index > 0) ? ", " : "", fieldName);
                        }
                        ClassInfoIndentInc(os);
                        if constexpr (isReflectableClass<MemberType>()) {
                            using UnwrappedMemberType = std::remove_reference_t<decltype(member(lhs))>;
                            if constexpr (is_smart_pointer<std::remove_reference_t<UnwrappedMemberType>>::value) {
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
            //os << fmt::format("{} vs. ", lhsValue) << rhs;
            os << fmt::format(fg(fmt::color::red), "{} vs. {}", lhsValue, rhsValue); // coloured terminal output
        }
    }
    if (indent == 0) {
        os << std::endl;
    }
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

#endif //OPENCMW_UTILS_H
