#include <majordomo/Cryptography.hpp>

#include <catch2/catch.hpp>

// execute also at runtime so that it's noticed by gcov
#define STATIC_REQUIRE2(expr) \
    { \
        STATIC_REQUIRE(expr); \
        REQUIRE(expr); \
    }

TEST_CASE("Cryptography tests", "[rbac][cryptography]") {
    using namespace opencmw::majordomo;
    using namespace cryptography;
    opencmw::mdp::Message msg;
    msg.data               = opencmw::IoBuffer("Test Message");
    const auto [pub, priv] = generateKeyPair();
    const auto hash        = publicKeyHash(pub);
    sign(msg, priv, hash);
    REQUIRE(msg.rbac.size() > 0);
    REQUIRE(verify(msg, pub));
}
