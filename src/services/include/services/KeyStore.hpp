#ifndef OPENCMW_CPP_KEYSTORE_HPP
#define OPENCMW_CPP_KEYSTORE_HPP

#include <majordomo/Worker.hpp>

namespace opencmw {

struct KeystoreContext {
    unsigned char dummy;
};
struct KeystoreInput {
    std::string keyHash;
};

struct KeystoreOutput {
    std::string role;
    std::string key;
    // we have to use int64_t, as std::chrono is not supported over the wire
    int64_t expiryDate;
};

} // namespace opencmw

ENABLE_REFLECTION_FOR(opencmw::KeystoreContext, dummy)
ENABLE_REFLECTION_FOR(opencmw::KeystoreInput, keyHash)
ENABLE_REFLECTION_FOR(opencmw::KeystoreOutput, role, key, expiryDate)

namespace opencmw {

using RoleKey = KeystoreOutput;

namespace {
using KeystoreWorkerType = majordomo::Worker<"/keystore", KeystoreContext, KeystoreInput, KeystoreOutput, majordomo::description<"Public Key storage">>;
};

class KeystoreWorker : public KeystoreWorkerType {
private:
    std::map<std::string, RoleKey> _keys;

public:
    explicit KeystoreWorker(URI<uri_check::STRICT> brokerAddress, const zmq::Context &context, majordomo::Settings settings = {})
        : KeystoreWorkerType(brokerAddress, {}, context, settings) { init(); };
    template<typename BrokerType>
    explicit KeystoreWorker(const BrokerType &broker)
        : KeystoreWorkerType(broker, {}) { init(); };

    static std::string keyHash(const std::string &key) {
        majordomo::cryptography::PublicKey k;
        std::copy(key.begin(), key.end(), k.key);
        const auto res = majordomo::cryptography::publicKeyHash(k);
        return std::string(res.hash, res.hash + crypto_generichash_KEYBYTES);
    }

    void addKey(const RoleKey &roleKey) {
        _keys[keyHash(roleKey.key)] = roleKey;
    }

private:
    void init() {
        KeystoreWorkerType::setCallback([this](const majordomo::RequestContext &rawCtx, const KeystoreContext &context, const KeystoreInput &in, KeystoreContext &replyContext, KeystoreOutput &out) {
            if (_keys.contains(in.keyHash)) {
                out = _keys.at(in.keyHash);
            }
        });
    }
};

} // namespace opencmw

#endif
