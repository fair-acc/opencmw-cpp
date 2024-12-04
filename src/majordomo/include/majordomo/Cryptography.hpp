#ifndef OPENCMW_MAJORDOMO_CRYPTOGRAPHY_H
#define OPENCMW_MAJORDOMO_CRYPTOGRAPHY_H

#include <majordomo/Rbac.hpp>
#include <sodium.h>

namespace opencmw::majordomo {

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

}

} // namespace opencmw::majordomo::cryptography

ENABLE_REFLECTION_FOR(opencmw::majordomo::cryptography::PublicKey, key)

#endif
