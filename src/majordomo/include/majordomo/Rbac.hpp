#ifndef OPENCMW_MAJORDOMO_RBAC_H
#define OPENCMW_MAJORDOMO_RBAC_H

#include <opencmw.hpp>

#include <units/bits/external/fixed_string.h> // TODO use which header?

#include <algorithm>
#include <array>
#include <string>
#include <tuple>

namespace opencmw::majordomo::rbac {

enum class Permission {
    RW,
    RO,
    WO,
    NONE
};

template<units::basic_fixed_string roleName, uint8_t rolePriority, Permission accessRights>
class Role {
public:
    [[nodiscard]] static constexpr std::string_view name() { return roleName.data_; }
    [[nodiscard]] static constexpr int              priority() { return rolePriority; }
    [[nodiscard]] static constexpr Permission       rights() { return accessRights; }
    template<typename ROLE>
    [[nodiscard]] constexpr bool operator==(const ROLE &other) const noexcept {
        return std::is_same_v<ROLE, Role<roleName, rolePriority, accessRights>>;
    }
};

template<typename T>
concept role = requires {
    T::name();
    T::priority();
    T::rights();
};

struct ADMIN : Role<"ADMIN", 255, Permission::RW> {};
struct ANY : Role<"ANY", 0, Permission::RO> {};
struct NONE : Role<"NONE", 0, Permission::NONE> {};

namespace detail {

template<role... Roles>
inline constexpr std::array<std::pair<std::string_view, std::size_t>, sizeof...(Roles)> normalizedPriorities() noexcept {
    static_assert(sizeof...(Roles) > 0);
    auto roles = std::array<std::pair<std::string_view, std::size_t>, sizeof...(Roles)>{ std::pair{ Roles::name(), Roles::priority() }... };
    std::sort(roles.begin(), roles.end(), [](const auto &lhs, const auto &rhs) { return lhs.second > rhs.second; });
    std::size_t nextPrio     = 0;
    auto        originalPrio = roles[0].second;
    roles[0].second          = nextPrio++;
    for (std::size_t i = 1; i < roles.size(); ++i) {
        if (roles[i].second == originalPrio) {
            roles[i].second = roles[i - 1].second;
        } else {
            originalPrio    = roles[i].second;
            roles[i].second = nextPrio++;
        }
    }

    return { roles };
}

} // namespace detail

template<role... Roles>
inline constexpr std::size_t priorityCount() noexcept {
    if constexpr (sizeof...(Roles) == 0) {
        return 1;
    } else {
        return detail::normalizedPriorities<Roles...>().back().second + 1;
    }
}

template<role... Roles>
inline constexpr std::size_t priorityIndex(std::string_view roleName) noexcept {
    if constexpr (sizeof...(Roles) == 0) {
        return 0;
    } else {
        constexpr auto map          = opencmw::ConstExprMap{ detail::normalizedPriorities<Roles...>() };
        constexpr auto defaultValue = priorityCount<Roles...>() - 1;
        return map.at(roleName, defaultValue);
    }
}

template<role... Roles>
inline constexpr Permission permission(std::string_view roleName) noexcept {
    if constexpr (sizeof...(Roles) == 0) {
        return Permission::RW; // no roles defined => default role has all permissions; TODO correct?
    } else {
        constexpr auto map = opencmw::ConstExprMap<std::string_view, Permission, sizeof...(Roles)>{ std::pair{ Roles::name(), Roles::rights() }... };
        return map.at(roleName, Permission::NONE);
    }
}

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
