#include <majordomo/Rbac.hpp>

#include <catch2/catch.hpp>

// execute also at runtime so that it's noticed by gcov
#define STATIC_REQUIRE2(expr) \
    { \
        STATIC_REQUIRE(expr); \
        REQUIRE(expr); \
    }

TEST_CASE("RBAC parser tests", "[rbac][parsing]") {
    using namespace opencmw::majordomo::rbac;
    using namespace std::literals;

    STATIC_REQUIRE2(parse::role("").empty());
    STATIC_REQUIRE2(parse::hash("").empty());
    STATIC_REQUIRE2(parse::roleAndHash("") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(parse::role("RBAC=ADMIN").empty()); // missing comma
    STATIC_REQUIRE2(parse::hash("RBAC=ADMIN").empty());
    STATIC_REQUIRE2(parse::roleAndHash("RBAC=ADMIN") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(parse::role("ADMIN").empty()); // No "RBAC=" prefix
    STATIC_REQUIRE2(parse::hash("ADMIN").empty());
    STATIC_REQUIRE2(parse::roleAndHash("ADMIN") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(parse::role("RBAC=ADMIN,") == "ADMIN");
    STATIC_REQUIRE2(parse::hash("RBAC=ADMIN,") == "");
    STATIC_REQUIRE2(parse::roleAndHash("RBAC=ADMIN,") == std::pair("ADMIN"sv, ""sv));
    STATIC_REQUIRE2(parse::role("RBAC=ADMIN,hash") == "ADMIN");
    STATIC_REQUIRE2(parse::hash("RBAC=ADMIN,hash") == "hash");
    STATIC_REQUIRE2(parse::roleAndHash("RBAC=ADMIN,hash") == std::pair("ADMIN"sv, "hash"sv));
    STATIC_REQUIRE2(parse::hash("RBAC=ADMIN,hash,invalidHash") == "hash,invalidHash"); // TODO in java, this throws, should we throw/return "", too?
}
