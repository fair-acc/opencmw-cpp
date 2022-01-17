#ifndef OPENCMW_RBAC_H
#define OPENCMW_RBAC_H

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <utility>

namespace opencmw::majordomo::rbac {

struct RoleAndPriority {
    std::string_view name;
    int              priority;
};

namespace detail {
constexpr int lowestPriority(const std::span<RoleAndPriority> &roles) noexcept {
    return std::max_element(roles.begin(), roles.end(), [](const auto &lhs, const auto &rhs) { return lhs.priority < rhs.priority; })->priority;
}

template<std::size_t N>
constexpr std::array<RoleAndPriority, N> normalizedPriorities(std::array<RoleAndPriority, N> roles) noexcept {
    static_assert(N > 0);
    std::sort(roles.begin(), roles.end(), [](const auto &lhs, const auto &rhs) { return lhs.priority < rhs.priority; });
    int nextPrio      = 0;
    int originalPrio  = roles[0].priority;
    roles[0].priority = nextPrio++;
    for (std::size_t i = 1; i < roles.size(); ++i) {
        if (roles[i].priority == originalPrio) {
            roles[i].priority = roles[i - 1].priority;
        } else {
            originalPrio      = roles[i].priority;
            roles[i].priority = nextPrio++;
        }
    }

    return roles;
}
} // namespace detail

template<std::size_t N>
struct RoleSet {
    static_assert(N > 0);
    std::array<RoleAndPriority, N> _roles;
    int                            defaultPriority;

    constexpr RoleSet(const std::array<RoleAndPriority, N> &roles) noexcept
        : _roles(detail::normalizedPriorities(roles)), defaultPriority(detail::lowestPriority(std::span(_roles))) {}

    constexpr std::size_t size() const noexcept {
        return N;
    }

    constexpr std::size_t priorityCount() const noexcept {
        return static_cast<std::size_t>(_roles.back().priority) + 1;
    }

    constexpr int priority(std::string_view role) const noexcept {
        const auto it = std::find_if(_roles.begin(), _roles.end(), [&role](const auto &v) { return v.name == role; });
        return it != _roles.end() ? it->priority : defaultPriority;
    }
};

template<std::size_t N>
RoleSet(const std::array<RoleAndPriority, N> &) -> RoleSet<N>;

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

} // namespace opencmw::rbac

#endif
