#ifndef OPENCMW_MAJORDOMO_RBAC_H
#define OPENCMW_MAJORDOMO_RBAC_H

#include <opencmw.hpp>

#include <units/bits/external/fixed_string.h> // TODO use which header?

#include <algorithm>
#include <array>
#include <string>

namespace opencmw::majordomo::rbac {

enum class Permission {
    RW,
    RO,
    WO,
    NONE
};

template<units::basic_fixed_string roleName, Permission accessRights>
class Role {
public:
    [[nodiscard]] static constexpr std::string_view name() { return roleName.data_; }
    [[nodiscard]] static constexpr Permission       rights() { return accessRights; }
    template<typename ROLE>
    [[nodiscard]] constexpr bool operator==(const ROLE &other) const noexcept {
        return std::is_same_v<ROLE, Role<roleName, accessRights>>;
    }
};

template<typename T>
constexpr const bool is_role = requires {
    T::name();
    T::rights();
};

template<typename T>
concept role = is_role<T>;

struct ADMIN : Role<"ADMIN", Permission::RW> {};
struct ANY : Role<"ANY", Permission::RW> {};
struct ANY_RO : Role<"ANY", Permission::RO> {};
struct NONE : Role<"NONE", Permission::NONE> {};

template<rbac::role... Values>
struct roles : std::type_identity<std::tuple<Values...>> {
    using type2 = std::tuple<Values...>;
};

namespace detail {
template<typename Item>
constexpr auto find_roles_helper() {
    if constexpr (rbac::is_role<Item>) {
        return std::tuple<Item>();
    } else if constexpr (is_instance_of_v<Item, roles>) {
        return to_instance<std::tuple>(Item());
    } else {
        return std::tuple<>();
    }
}
} // namespace detail

template<typename... Items>
using find_roles = decltype(std::tuple_cat(detail::find_roles_helper<Items>()...));

namespace parse {

constexpr auto                    RBAC_PREFIX = std::string_view("RBAC=");

inline constexpr std::string_view role(std::string_view token) noexcept {
    if (!token.starts_with(RBAC_PREFIX)) {
        return {};
    }

    token.remove_prefix(RBAC_PREFIX.size());

    const auto commaPos = token.find(',');
    if (commaPos == std::string_view::npos) {
        return {};
    }

    return token.substr(0, commaPos);
}

inline constexpr std::string_view hash(std::string_view token) noexcept {
    if (!token.starts_with(RBAC_PREFIX)) {
        return {};
    }

    const auto commaPos = token.find(',');
    if (commaPos == std::string_view::npos) {
        return {};
    }

    token.remove_prefix(commaPos + 1);
    return token;
}

inline constexpr std::pair<std::string_view, std::string_view> roleAndHash(std::string_view token) noexcept {
    if (!token.starts_with(RBAC_PREFIX)) {
        return {};
    }

    token.remove_prefix(RBAC_PREFIX.size());

    const auto commaPos = token.find(',');
    if (commaPos == std::string_view::npos) {
        return {};
    }

    return { token.substr(0, commaPos), token.substr(commaPos + 1) };
}

} // namespace parse

} // namespace opencmw::majordomo::rbac

#endif
