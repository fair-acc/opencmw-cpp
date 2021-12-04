#include <catch2/catch.hpp>

#include <majordomo/Debug.hpp>
#include <majordomo/SubscriptionMatcher.hpp>

#include <URI.hpp>

TEST_CASE("Test path-only topics", "[subscription_matcher][path_only]") {
    using URI = opencmw::SubscriptionMatcher::URI;
    opencmw::SubscriptionMatcher matcher;

    REQUIRE(matcher(URI("/property"), URI("/property")));
    REQUIRE_FALSE(matcher(URI("/property/A"), URI("/property")));
    REQUIRE(matcher(URI("/property"), URI("")));
    REQUIRE(matcher(URI("/property/A"), URI("")));
    REQUIRE(matcher(URI("/property/A"), URI("/property*")));
    REQUIRE(matcher(URI("/property/A/B"), URI("/property*")));
    REQUIRE_FALSE(matcher(URI("/property"), URI("/property2")));
    REQUIRE_FALSE(matcher(URI("/property"), URI("/property2")));
    REQUIRE(matcher(URI("/property?testQuery"), URI("/property")));
    REQUIRE(matcher(URI("/property?testQuery"), URI("/property*")));

    // no filter configuration -> ignores query and matches only path
    REQUIRE(matcher(URI("/property?testQuery"), URI("/property?testQuery")));
    REQUIRE(matcher(URI("/property?testQuery"), URI("/property*?testQuery")));
    REQUIRE(matcher(URI("/property/A?testQuery"), URI("/property*?testQuery")));
    REQUIRE(matcher(URI("/property"), URI("/property?testQuery")));
    REQUIRE(matcher(URI("/property"), URI("/property*?testQuery")));
}

#if 0 // TODO enable when we do query handling
TEST_CASE("Test path and query", "[subscription_matcher][path_and_query]") {
    opencmw::SubscriptionMatcher<TestFilter1, TestFilter2> matcher;

    REQUIRE_FALSE(matcher(URI("/property1?testKey1"), URI("/property2?testKey1")));
    REQUIRE(matcher(URI("/property?testKey1"), URI("/property?testKey1")));
    REQUIRE(matcher(URI("/property?testKey1&testKey2"), URI("/property?testKey1&testKey2")));
    REQUIRE(matcher(URI("/property?testKey1&testKey2"), URI("/property?testKey2&testKey1")));
    REQUIRE(matcher(URI("/property?testKey1=42&testKey2=24"), URI("/property?testKey2=24&testKey1=42")));
    REQUIRE_FALSE(matcher(URI("/property?testKey1=41"), URI("/property*?testKey1=4711")));
    REQUIRE(matcher(URI("/property/A?testKey1=41"), URI("/property*?testKey1=41")));
    REQUIRE(matcher(URI("/property"), URI("/property?testQuery"))); // ignore unknown ctx filter on subscription side
    REQUIRE(matcher(URI("/property"), URI("/property*?testQuery"))); // ignore unknown ctx filter on subscription side
    REQUIRE(matcher(URI("/property?testKey1"), URI("/property*")));
    REQUIRE(matcher(URI("/property?testKey1"), URI("/property?TestKey1"))); // N.B. key is case sensitive
    REQUIRE_FALSE(matcher(URI("/property?testKey1"), URI("/property?testKey1=42")));
    REQUIRE_FALSE(matcher(URI("/property"), URI("/property?testKey1=42")));
}
#endif
