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

    STATIC_REQUIRE2(rbac::role("").empty());
    STATIC_REQUIRE2(rbac::hash("").empty());
    STATIC_REQUIRE2(rbac::roleAndHash("") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(rbac::role("RBAC=ADMIN").empty()); // missing comma
    STATIC_REQUIRE2(rbac::hash("RBAC=ADMIN").empty());
    STATIC_REQUIRE2(rbac::roleAndHash("RBAC=ADMIN") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(rbac::role("ADMIN").empty()); // No "RBAC=" prefix
    STATIC_REQUIRE2(rbac::hash("ADMIN").empty());
    STATIC_REQUIRE2(rbac::roleAndHash("ADMIN") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(rbac::role("RBAC=ADMIN,") == "ADMIN");
    STATIC_REQUIRE2(rbac::hash("RBAC=ADMIN,") == "");
    STATIC_REQUIRE2(rbac::roleAndHash("RBAC=ADMIN,") == std::pair("ADMIN"sv, ""sv));
    STATIC_REQUIRE2(rbac::role("RBAC=ADMIN,hash") == "ADMIN");
    STATIC_REQUIRE2(rbac::hash("RBAC=ADMIN,hash") == "hash");
    STATIC_REQUIRE2(rbac::roleAndHash("RBAC=ADMIN,hash") == std::pair("ADMIN"sv, "hash"sv));
    STATIC_REQUIRE2(rbac::hash("RBAC=ADMIN,hash,invalidHash") == "hash,invalidHash"); // TODO in java, this throws, should we throw/return "", too?
}

TEST_CASE("RBAC role handling", "[rbac][role_handling]") {
    using namespace opencmw::majordomo;
    using namespace std::literals;

    constexpr std::array<rbac::RoleAndPriority, 4> roleList = { { { "ADMIN"sv, 100 }, { "USER"sv, 200 }, { "ROOT"sv, 100 }, { "OTHER"sv, 300 } } };
    constexpr auto                                 roleSet  = rbac::RoleSet(roleList);
    STATIC_REQUIRE2(roleSet.priorityCount() == 3);
    STATIC_REQUIRE2(roleSet.size() == 4);
    STATIC_REQUIRE2(roleSet.priority("ADMIN") == 0);
    STATIC_REQUIRE2(roleSet.priority("ROOT") == 0);
    STATIC_REQUIRE2(roleSet.priority("USER") == 1);
    STATIC_REQUIRE2(roleSet.priority("OTHER") == 2);
    STATIC_REQUIRE2(roleSet.priority("FOO") == 2);
    STATIC_REQUIRE2(roleSet.priority("") == 2);
}
