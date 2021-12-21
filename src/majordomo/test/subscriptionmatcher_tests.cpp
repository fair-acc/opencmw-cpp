#include <catch2/catch.hpp>

#include <majordomo/Filters.hpp>
#include <majordomo/SubscriptionMatcher.hpp>

#include <TimingCtx.hpp>
#include <URI.hpp>

#include <charconv>

using opencmw::majordomo::DomainFilter;
using opencmw::majordomo::SubscriptionMatcher;
using URI = SubscriptionMatcher::URI;

TEST_CASE("Test path-only topics", "[subscription_matcher][path_only]") {
    SubscriptionMatcher matcher;

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

struct Int {
    int value = 0;

    explicit Int(std::string_view s) {
        if (s.empty()) {
            return;
        }
        const auto asInt = std::from_chars(s.begin(), s.end(), value);

        if (asInt.ec == std::errc::invalid_argument) {
            throw std::invalid_argument(fmt::format("'{}' is not a valid integer", s));
        }
    }

    bool operator==(const Int &) const = default;
};

using TestFilter1 = DomainFilter<Int>;
using TestFilter2 = DomainFilter<std::string_view>;

struct LessThan {
    bool operator()(std::string_view lhs, std::string_view rhs) const { return lhs < rhs; }
};

using TestFilter3 = DomainFilter<std::string_view, LessThan>;

TEST_CASE("Test path and query", "[subscription_matcher][path_and_query]") {
    SubscriptionMatcher matcher;
    matcher.addFilter<TestFilter1>("testKey1");
    matcher.addFilter<TestFilter2>("testKey2");
    matcher.addFilter<TestFilter3>("testKey3");

    REQUIRE_FALSE(matcher(URI("/property1?testKey1"), URI("/property2?testKey1")));
    REQUIRE(matcher(URI("/property?testKey1"), URI("/property?testKey1")));
    REQUIRE(matcher(URI("/property?testKey1&testKey2"), URI("/property?testKey1&testKey2")));
    REQUIRE(matcher(URI("/property?testKey1&testKey2"), URI("/property?testKey2&testKey1")));
    REQUIRE(matcher(URI("/property?testKey1=42&testKey2=24"), URI("/property?testKey2=24&testKey1=42")));
    REQUIRE_FALSE(matcher(URI("/property?testKey1=41"), URI("/property*?testKey1=4711")));
    REQUIRE(matcher(URI("/property/A?testKey1=41"), URI("/property*?testKey1=41")));
    REQUIRE(matcher(URI("/property"), URI("/property?testQuery")));  // ignore unknown ctx filter on subscription side
    REQUIRE(matcher(URI("/property"), URI("/property*?testQuery"))); // ignore unknown ctx filter on subscription side
    REQUIRE(matcher(URI("/property?testKey1"), URI("/property*")));
    REQUIRE(matcher(URI("/property?testKey1"), URI("/property?TestKey1"))); // N.B. key is case sensitive
    REQUIRE_FALSE(matcher(URI("/property?testKey1"), URI("/property?testKey1=42")));
    REQUIRE_FALSE(matcher(URI("/property"), URI("/property?testKey1=42")));
    REQUIRE(matcher(URI("/property?testKey3=abc"), URI("/property?testKey3=bcd")));
    REQUIRE_FALSE(matcher(URI("/property?testKey3=bcd"), URI("/property?testKey3=abc")));
}

TEST_CASE("Test timing and context type matching", "[subscription_matcher]") {
    SubscriptionMatcher matcher;
    matcher.addFilter<opencmw::majordomo::TimingCtxFilter>("ctx");
    matcher.addFilter<opencmw::majordomo::ContentTypeFilter>("contentType");

    REQUIRE_FALSE(matcher(URI("/property?ctx=FAIR.SELECTOR.ALL"), URI("/property?ctx=FAIR.SELECTOR.C=2")));
    REQUIRE(matcher(URI("/property?ctx=FAIR.SELECTOR.C=2"), URI("/property?ctx=FAIR.SELECTOR.ALL")));
    REQUIRE(matcher(URI("/property?ctx=FAIR.SELECTOR.C=2"), URI("/property?ctx=FAIR.SELECTOR.C=2")));
    REQUIRE(matcher(URI("/property?ctx=FAIR.SELECTOR.C=2:P=1"), URI("/property?ctx=FAIR.SELECTOR.C=2")));
    REQUIRE(matcher(URI("/property?ctx=FAIR.SELECTOR.C=2:P=1"), URI("/property?ctx=FAIR.SELECTOR.C=2:P=1")));
    REQUIRE_FALSE(matcher(URI("/property?ctx=FAIR.SELECTOR.C=2"), URI("/property?ctx=FAIR.SELECTOR.C=2:P=1"))); // notify not specific enough (missing 'P=1')
    REQUIRE_FALSE(matcher(URI("/property?ctx=FAIR.SELECTOR.ALL&contentType=text/html"), URI("/property?ctx=FAIR.SELECTOR.C=2&contentType=text/html")));
    REQUIRE_FALSE(matcher(URI("/property?ctx=FAIR.SELECTOR.ALL"), URI("/property?ctx=FAIR.SELECTOR.C=2&contentType=text/html")));
}
