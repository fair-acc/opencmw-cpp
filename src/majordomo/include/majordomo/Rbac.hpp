#ifndef OPENCMW_MAJORDOMO_RBAC_H
#define OPENCMW_MAJORDOMO_RBAC_H

#include <units/bits/external/fixed_string.h> // TODO use which header?

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace opencmw::majordomo::rbac {

enum class Permission { RW,
    RO,
    WO,
    NONE };
constexpr auto toString(Permission access) noexcept {
    switch (access) {
    case Permission::RW: return units::basic_fixed_string("RW");
    case Permission::RO: return units::basic_fixed_string("RO");
    case Permission::WO: return units::basic_fixed_string("WO");
    case Permission::NONE:
    default:
        return units::basic_fixed_string("NN");
    }
}

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

using RoleAndPriority = std::pair<std::string_view, std::size_t>;

template<role Role>
inline constexpr RoleAndPriority fromRole() {
    return { Role::name(), Role::priority() };
}

template<std::size_t N>
inline constexpr std::array<RoleAndPriority, N> normalizedPriorities(std::array<RoleAndPriority, N> roles) noexcept {
    static_assert(N > 0);
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

    return roles;
}

template<role... Roles>
inline constexpr std::array<RoleAndPriority, sizeof...(Roles)> namesAndPriorities() noexcept {
    return std::array<RoleAndPriority, sizeof...(Roles)>{ fromRole<Roles>()... };
}

} // namespace detail

template<role... Roles>
inline constexpr std::size_t priorityCount() noexcept {
    if constexpr (sizeof...(Roles) == 0) {
        return 1;
    } else {
        return detail::normalizedPriorities(detail::namesAndPriorities<Roles...>()).back().second + 1;
    }
}

template<role... Roles>
inline constexpr std::size_t priorityIndex(std::string_view roleName) noexcept {
    if constexpr (sizeof...(Roles) == 0) {
        return 0;
    } else {
        constexpr auto list = detail::normalizedPriorities(detail::namesAndPriorities<Roles...>());
        const auto     it   = std::find_if(list.begin(), list.end(), [&roleName](const auto &v) { return v.first == roleName; });
        return it != list.end() ? it->second : list.back().second;
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
