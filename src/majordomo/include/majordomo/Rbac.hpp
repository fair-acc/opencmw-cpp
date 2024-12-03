#ifndef OPENCMW_MAJORDOMO_RBAC_H
#define OPENCMW_MAJORDOMO_RBAC_H

#include <opencmw.hpp>

#include <algorithm>
#include <array>
#include <string>

#include <MdpMessage.hpp>
#include <sodium.h>

namespace opencmw::majordomo {

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

template<role... Values>
struct rbac : std::type_identity<std::tuple<Values...>> {
    using type2 = std::tuple<Values...>;
};

namespace detail {
template<typename Item>
constexpr auto find_roles_helper() {
    if constexpr (is_role<Item>) {
        return std::tuple<Item>();
    } else if constexpr (is_instance_of_v<Item, rbac>) {
        return to_instance<std::tuple>(Item());
    } else {
        return std::tuple<>();
    }
}
} // namespace detail

template<typename... Items>
using find_roles = decltype(std::tuple_cat(detail::find_roles_helper<Items>()...));

namespace parse_rbac {

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

} // namespace parse_rbac

namespace cryptography {

class PublicKey {
public:
    unsigned char key[crypto_sign_PUBLICKEYBYTES];
};

class PrivateKey {
public:
    unsigned char key[crypto_sign_SECRETKEYBYTES];
};

class KeyHash {
public:
    unsigned char hash[crypto_generichash_KEYBYTES];
};

std::pair<PublicKey, PrivateKey> generateKeyPair() {
    std::pair<PublicKey, PrivateKey> result;
    crypto_sign_keypair(result.first.key, result.second.key);
    return result;
}

KeyHash publicKeyHash(const PublicKey &key) {
    KeyHash result;
    crypto_generichash(result.hash, sizeof result.hash, key.key, crypto_sign_PUBLICKEYBYTES, nullptr, 0);
    return result;
}

std::string messageHash(const mdp::Message &msg) {
    unsigned char buf[crypto_generichash_KEYBYTES];
    for (size_t i = 0; i < crypto_generichash_KEYBYTES; ++i) {
        buf[i] = msg.rbac[i];
    }
    return std::string(buf, buf + crypto_generichash_KEYBYTES);
}

void sign(mdp::Message &msg, const PrivateKey &key, const KeyHash &hash) {
    // write key hash followed by signature
    unsigned char buf[crypto_generichash_KEYBYTES + crypto_sign_BYTES];
    std::copy(hash.hash, hash.hash + crypto_generichash_KEYBYTES, buf);
    auto data = msg.data;
    crypto_sign_detached(buf + crypto_generichash_KEYBYTES, nullptr, data.data(), data.size(), key.key);
    IoBuffer rbac{ buf };
    msg.rbac = rbac;
}

bool verify(mdp::Message &msg, const PublicKey &key) {
    auto          data = msg.data;
    unsigned char sig[crypto_sign_BYTES];
    for (size_t i = 0; i < crypto_sign_BYTES; ++i) {
        // signature is offset due to the hash being written first
        sig[i] = msg.rbac[crypto_generichash_KEYBYTES + i];
    }
    return !crypto_sign_verify_detached(sig, data.data(), data.size(), key.key);
}

} // namespace cryptography

} // namespace opencmw::majordomo

ENABLE_REFLECTION_FOR(opencmw::majordomo::cryptography::PublicKey, key)

#endif
