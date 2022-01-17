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

template<opencmw::majordomo::rbac::role... Roles>
struct PriorityHelper {
    static constexpr std::size_t priorityCount() {
        return opencmw::majordomo::rbac::priorityCount<Roles...>();
    }

    static constexpr std::size_t priorityIndex(std::string_view roleName) {
        return opencmw::majordomo::rbac::priorityIndex<Roles...>(roleName);
    }
};

TEST_CASE("RBAC role handling", "[rbac][role_handling]") {
    using namespace opencmw::majordomo;
    using namespace std::literals;

    using Helper = PriorityHelper<rbac::ADMIN, rbac::ANY, rbac::Role<"ROOT", 255, rbac::Permission::RW>, rbac::Role<"USER", 100, rbac::Permission::RO>>;

    STATIC_REQUIRE2(Helper::priorityCount() == 3);
    STATIC_REQUIRE2(Helper::priorityIndex("ADMIN") == 0);
    STATIC_REQUIRE2(Helper::priorityIndex("ROOT") == 0);
    STATIC_REQUIRE2(Helper::priorityIndex("USER") == 1);
    STATIC_REQUIRE2(Helper::priorityIndex("OTHER") == 2);
    STATIC_REQUIRE2(Helper::priorityIndex("FOO") == 2);
    STATIC_REQUIRE2(Helper::priorityIndex("") == 2);

    using Empty = PriorityHelper<>;

    STATIC_REQUIRE2(Empty::priorityCount() == 1);
    STATIC_REQUIRE2(Empty::priorityIndex("ADMIN") == 0);
    STATIC_REQUIRE2(Empty::priorityIndex("ROOT") == 0);
    STATIC_REQUIRE2(Empty::priorityIndex("USER") == 0);
    STATIC_REQUIRE2(Empty::priorityIndex("OTHER") == 0);
    STATIC_REQUIRE2(Empty::priorityIndex("FOO") == 0);
    STATIC_REQUIRE2(Empty::priorityIndex("") == 0);
}
