#include <majordomo/Rbac.hpp>

#include <catch2/catch.hpp>

// execute also at runtime so that it's noticed by gcov
#define STATIC_REQUIRE2(expr) \
    { \
        STATIC_REQUIRE(expr); \
        REQUIRE(expr); \
    }

TEST_CASE("RBAC parser tests", "[rbac][parsing]") {
    using namespace opencmw::majordomo;
    using namespace std::literals;

    STATIC_REQUIRE2(parse_rbac::role("").empty());
    STATIC_REQUIRE2(parse_rbac::hash("").empty());
    STATIC_REQUIRE2(parse_rbac::roleAndHash("") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(parse_rbac::role("RBAC=ADMIN").empty()); // missing comma
    STATIC_REQUIRE2(parse_rbac::hash("RBAC=ADMIN").empty());
    STATIC_REQUIRE2(parse_rbac::roleAndHash("RBAC=ADMIN") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(parse_rbac::role("ADMIN").empty()); // No "RBAC=" prefix
    STATIC_REQUIRE2(parse_rbac::hash("ADMIN").empty());
    STATIC_REQUIRE2(parse_rbac::roleAndHash("ADMIN") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(parse_rbac::role("RBAC=ADMIN,") == "ADMIN");
    STATIC_REQUIRE2(parse_rbac::hash("RBAC=ADMIN,") == "");
    STATIC_REQUIRE2(parse_rbac::roleAndHash("RBAC=ADMIN,") == std::pair("ADMIN"sv, ""sv));
    STATIC_REQUIRE2(parse_rbac::role("RBAC=ADMIN,hash") == "ADMIN");
    STATIC_REQUIRE2(parse_rbac::hash("RBAC=ADMIN,hash") == "hash");
    STATIC_REQUIRE2(parse_rbac::roleAndHash("RBAC=ADMIN,hash") == std::pair("ADMIN"sv, "hash"sv));
    STATIC_REQUIRE2(parse_rbac::hash("RBAC=ADMIN,hash,invalidHash") == "hash,invalidHash"); // TODO in java, this throws, should we throw/return "", too?
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
