#ifndef OPENCMW_MAJORDOMO_RBAC_H
#define OPENCMW_MAJORDOMO_RBAC_H

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

template<units::basic_fixed_string roleName = "role", uint8_t rolePriority = 0U, Permission accessRights = Permission::NONE>
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

using RoleInfo = std::tuple<std::string_view, std::size_t, Permission>;

template<role Role>
inline constexpr RoleInfo fromRole() noexcept {
    return { Role::name(), Role::priority(), Role::rights() };
}

template<std::size_t N>
inline constexpr std::array<RoleInfo, N> normalizedPriorities(std::array<RoleInfo, N> roles) noexcept {
    static_assert(N > 0);
    std::sort(roles.begin(), roles.end(), [](const auto &lhs, const auto &rhs) { return std::get<1>(lhs) > std::get<1>(rhs); });
    std::size_t nextPrio     = 0;
    auto        originalPrio = std::get<1>(roles[0]);
    std::get<1>(roles[0])    = nextPrio++;
    for (std::size_t i = 1; i < roles.size(); ++i) {
        if (std::get<1>(roles[i]) == originalPrio) {
            std::get<1>(roles[i]) = std::get<1>(roles[i - 1]);
        } else {
            originalPrio          = std::get<1>(roles[i]);
            std::get<1>(roles[i]) = nextPrio++;
        }
    }

    return roles;
}

template<role... Roles>
inline constexpr std::array<RoleInfo, sizeof...(Roles)> roleInfos() noexcept {
    return std::array<RoleInfo, sizeof...(Roles)>{ fromRole<Roles>()... };
}

} // namespace detail

template<role... Roles>
inline constexpr std::size_t priorityCount() noexcept {
    if constexpr (sizeof...(Roles) == 0) {
        return 1;
    } else {
        return std::get<1>(detail::normalizedPriorities(detail::roleInfos<Roles...>()).back()) + 1;
    }
}

template<role... Roles>
inline constexpr std::size_t priorityIndex(std::string_view roleName) noexcept {
    if constexpr (sizeof...(Roles) == 0) {
        return 0;
    } else {
        constexpr auto list = detail::normalizedPriorities(detail::roleInfos<Roles...>());
        const auto     it   = std::find_if(list.begin(), list.end(), [&roleName](const auto &v) { return std::get<0>(v) == roleName; });
        return std::get<1>(it != list.end() ? *it : list.back());
    }
}

template<role... Roles>
inline constexpr Permission permission(std::string_view roleName) noexcept {
    if constexpr (sizeof...(Roles) == 0) {
        return Permission::RW; // no roles defined => default role has all permissions; TODO correct?
    } else {
        constexpr auto list = detail::roleInfos<Roles...>();
        const auto     it   = std::find_if(list.begin(), list.end(), [&roleName](const auto &v) { return std::get<0>(v) == roleName; });
        return it != list.end() ? std::get<2>(*it) : Permission::NONE;
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
