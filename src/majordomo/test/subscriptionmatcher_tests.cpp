#include <catch2/catch.hpp>

#include <Filters.hpp>
#include <majordomo/SubscriptionMatcher.hpp>
#include <TimingCtx.hpp>
#include <QuerySerialiser.hpp>

#include <URI.hpp>

#include <charconv>

using opencmw::majordomo::SubscriptionMatcher;
using opencmw::mdp::Topic;

struct SubscriptionStringMatcher : SubscriptionMatcher {
    bool operator()(const auto &notified, const auto &subscriber) const {
        return SubscriptionMatcher::operator()(Topic::fromString(notified), Topic::fromString(subscriber));
    }
};

TEST_CASE("Test path-only topics", "[subscription_matcher][path_only]") {
    SubscriptionStringMatcher matcher;

    REQUIRE(matcher("/service", "/service"));
    REQUIRE_FALSE(matcher("/service/A", "/service"));
    REQUIRE_FALSE(matcher("/service", "/service2"));
    REQUIRE_FALSE(matcher("/service", "/service2"));
    REQUIRE(matcher("/service?testQuery", "/service"));

    // no filter configuration -> ignores query and matches only path
    REQUIRE(matcher("/service?testQuery", "/service?testQuery"));
    REQUIRE(matcher("/service/A?testQuery", "/service/A?testQuery"));
    REQUIRE(matcher("/service", "/service?testQuery"));
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

TEST_CASE("Test path and query", "[subscription_matcher][path_and_query]") {
    struct LessThan {
        bool operator()(std::string_view lhs, std::string_view rhs) const { return lhs < rhs; }
    };
    using opencmw::DomainFilter;
    using TestFilter1 = DomainFilter<Int>;
    using TestFilter2 = DomainFilter<std::string_view>;
    using TestFilter3 = DomainFilter<std::string_view, LessThan>;
    using TestFilter4 = opencmw::NumberFilter<float>;
    SubscriptionStringMatcher matcher;
    matcher.addFilter<TestFilter1>("testKey1");
    matcher.addFilter<TestFilter2>("testKey2");
    matcher.addFilter<TestFilter3>("testKey3");
    matcher.addFilter<TestFilter4>("testKey4");
    matcher.addFilter<TestFilter2>("testFlag1");
    matcher.addFilter<TestFilter2>("testFlag2");

    REQUIRE_FALSE(matcher("/service1?testKey1", "/service2?testKey1"));
    REQUIRE(matcher("/service?testKey1", "/service?testKey1"));
    REQUIRE(matcher("/service?testKey1&testKey2", "/service?testKey1&testKey2"));
    REQUIRE(matcher("/service?testKey1&testKey2", "/service?testKey2&testKey1"));
    REQUIRE(matcher("/service?testKey1=42&testKey2=24", "/service?testKey2=24&testKey1=42"));
    REQUIRE_FALSE(matcher("/service?testKey1=41", "/service?testKey1=4711"));
    REQUIRE(matcher("/service/A?testKey1=41", "/service/A?testKey1=41"));
    REQUIRE(matcher("/service", "/service?testQuery")); // ignore unknown ctx filter on subscription side
    REQUIRE(matcher("/service?testKey1", "/service"));
    REQUIRE(matcher("/service?testKey1", "/service?TestKey1")); // N.B. key is case sensitive
    REQUIRE_FALSE(matcher("/service?testKey1", "/service?testKey1=42"));
    REQUIRE_FALSE(matcher("/service", "/service?testKey1=42"));
    REQUIRE(matcher("/service?testKey3=abc", "/service?testKey3=bcd"));
    REQUIRE_FALSE(matcher("/service?testKey3=bcd", "/service?testKey3=abc"));
    REQUIRE(matcher("/service?testKey4=3.33", "/service?testKey4=3.33"));
    REQUIRE_FALSE(matcher("/service?testKey4=3.33", "/service?testKey4=3.44"));
    REQUIRE_FALSE(matcher("/service?testFlag1", "/service?testFlag1&testFlag2"));
}

TEST_CASE("Test timing and context type matching", "[subscription_matcher]") {
    SubscriptionStringMatcher matcher;
    matcher.addFilter<opencmw::TimingCtxFilter>("ctx");
    matcher.addFilter<opencmw::ContentTypeFilter>("contentType");

    REQUIRE_FALSE(matcher("/service?ctx=FAIR.SELECTOR.ALL", "/service?ctx=FAIR.SELECTOR.C=2"));
    REQUIRE(matcher("/service?ctx=FAIR.SELECTOR.C=2", "/service?ctx=FAIR.SELECTOR.ALL"));
    REQUIRE(matcher("/service?ctx=FAIR.SELECTOR.C=2", "/service?ctx=FAIR.SELECTOR.C=2"));
    REQUIRE(matcher("/service?ctx=FAIR.SELECTOR.C=2:P=1", "/service?ctx=FAIR.SELECTOR.C=2"));
    REQUIRE(matcher("/service?ctx=FAIR.SELECTOR.C=2:P=1", "/service?ctx=FAIR.SELECTOR.C=2:P=1"));
    REQUIRE_FALSE(matcher("/service?ctx=FAIR.SELECTOR.C=2", "/service?ctx=FAIR.SELECTOR.C=2:P=1")); // notify not specific enough (missing 'P=1')
    REQUIRE_FALSE(matcher("/service?ctx=FAIR.SELECTOR.ALL&contentType=text/html", "/service?ctx=FAIR.SELECTOR.C=2&contentType=text/html"));
    REQUIRE_FALSE(matcher("/service?ctx=FAIR.SELECTOR.ALL", "/service?ctx=FAIR.SELECTOR.C=2&contentType=text/html"));
}

struct MatcherTest {
    opencmw::TimingCtx      ctx;
    opencmw::MIME::MimeType contentType = opencmw::MIME::UNKNOWN;
};
ENABLE_REFLECTION_FOR(MatcherTest, ctx, contentType)

TEST_CASE("Register filters", "[QuerySerialiser][register_filters]") {
    SubscriptionStringMatcher matcher;
    opencmw::query::registerTypes(MatcherTest(), matcher);

    // subset of the subscriptionfilter_tests
    REQUIRE_FALSE(matcher("/service?ctx=FAIR.SELECTOR.ALL", "/service?ctx=FAIR.SELECTOR.C=2"));
    REQUIRE(matcher("/service?ctx=FAIR.SELECTOR.C=2", "/service?ctx=FAIR.SELECTOR.ALL"));
    REQUIRE(matcher("/service?ctx=FAIR.SELECTOR.C=2", "/service?ctx=FAIR.SELECTOR.C=2"));
    REQUIRE_FALSE(matcher("/service?ctx=FAIR.SELECTOR.ALL&contentType=text/html", "/service?ctx=FAIR.SELECTOR.C=2&contentType=text/html"));
    REQUIRE_FALSE(matcher("/service?ctx=FAIR.SELECTOR.ALL", "/service?ctx=FAIR.SELECTOR.C=2&contentType=text/html"));
}
