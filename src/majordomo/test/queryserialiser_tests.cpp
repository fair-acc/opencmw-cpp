#include <catch2/catch.hpp>

#include <majordomo/QuerySerialiser.hpp>
#include <opencmw.hpp>
#include <TimingCtx.hpp>
#include <URI.hpp>

#include <optional>

struct TestQueryClass {
    std::string             param1;
    int                     param2;
    bool                    param3;
    opencmw::MIME::MimeType contentType = opencmw::MIME::UNKNOWN;
    opencmw::TimingCtx      ctx;

    bool                    operator==(const TestQueryClass &) const noexcept = default;
};

ENABLE_REFLECTION_FOR(TestQueryClass, param1, param2, param3, contentType, ctx)

TEST_CASE("serialise/deserialise queries", "[QuerySerialiser][serialisation]") {
    using namespace opencmw;
    using opencmw::query::QueryMap;

    {
        const auto v = TestQueryClass{
            .param1      = "Hello",
            .param2      = 42,
            .param3      = true,
            .contentType = MIME::HTML,
            .ctx         = TimingCtx(1, 2, 3, 4)
        };

        const auto map = QueryMap{
            { "param1", "Hello" },
            { "param2", "42" },
            { "param3", std::nullopt },
            { "contentType", "text/html" },
            { "ctx", "FAIR.SELECTOR.C=1:S=2:P=3:T=4" }
        };

        const auto serialised = query::serialise(v);
        REQUIRE(serialised == map);
        const auto deserialised = query::deserialise<TestQueryClass>(map);
        REQUIRE(v == deserialised);
    }

    {
        const auto map = QueryMap{
            { "param1", "Hello" },
            { "param2", "42" },
            { "param3", "false" },
            { "contentType", "text/html" },
            { "ctx", "FAIR.SELECTOR.X=1" }
        };

        const auto v = TestQueryClass{
            .param1      = "Hello",
            .param2      = 42,
            .param3      = false,
            .contentType = MIME::HTML,
            .ctx         = TimingCtx()
        };
        REQUIRE_NOTHROW(query::deserialise<TestQueryClass>(map));
        REQUIRE(query::deserialise<TestQueryClass>(map) == v);
    }
}

struct WithOneMimeType {
    int                     x;
    opencmw::MIME::MimeType contentType = opencmw::MIME::HTML;
};

ENABLE_REFLECTION_FOR(WithOneMimeType, x, contentType)

struct WithTwoMimeTypes {
    int                     x;
    opencmw::MIME::MimeType contentType        = opencmw::MIME::CMWLIGHT;
    opencmw::MIME::MimeType anotherContentType = opencmw::MIME::JSON;
};

ENABLE_REFLECTION_FOR(WithTwoMimeTypes, x, contentType, anotherContentType)

struct WithoutMimeType {
    int x;
    int y;
};

ENABLE_REFLECTION_FOR(WithoutMimeType, x, y)

TEST_CASE("extract MIME type", "[QuerySerialiser][mimetype_extraction]") {
    using namespace opencmw;

    REQUIRE(query::getMimeType(WithOneMimeType()) == opencmw::MIME::HTML);
    REQUIRE(query::getMimeType(WithoutMimeType()) == opencmw::MIME::UNKNOWN);
    REQUIRE(query::getMimeType(WithTwoMimeTypes()) == opencmw::MIME::CMWLIGHT);
}

struct MatcherTest {
    opencmw::TimingCtx      ctx;
    opencmw::MIME::MimeType contentType = opencmw::MIME::UNKNOWN;
};

ENABLE_REFLECTION_FOR(MatcherTest, ctx, contentType)

TEST_CASE("Register filters", "[QuerySerialiser][register_filters]") {
    using URI = opencmw::URI<opencmw::RELAXED>;

    opencmw::majordomo::SubscriptionMatcher matcher;
    opencmw::query::registerTypes(MatcherTest(), matcher);

    // subset of the subscriptionfilter_tests
    REQUIRE_FALSE(matcher(URI("/property?ctx=FAIR.SELECTOR.ALL"), URI("/property?ctx=FAIR.SELECTOR.C=2")));
    REQUIRE(matcher(URI("/property?ctx=FAIR.SELECTOR.C=2"), URI("/property?ctx=FAIR.SELECTOR.ALL")));
    REQUIRE(matcher(URI("/property?ctx=FAIR.SELECTOR.C=2"), URI("/property?ctx=FAIR.SELECTOR.C=2")));
    REQUIRE_FALSE(matcher(URI("/property?ctx=FAIR.SELECTOR.ALL&contentType=text/html"), URI("/property?ctx=FAIR.SELECTOR.C=2&contentType=text/html")));
    REQUIRE_FALSE(matcher(URI("/property?ctx=FAIR.SELECTOR.ALL"), URI("/property?ctx=FAIR.SELECTOR.C=2&contentType=text/html")));
}
