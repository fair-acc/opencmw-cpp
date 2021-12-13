#include <catch2/catch.hpp>

#include <majordomo/QueryParameterParser.hpp>

#include <opencmw.hpp>
#include <TimingCtx.hpp>
#include <URI.hpp>

struct TestQueryClass {
    std::string             param1;
    int                     param2;
    opencmw::MIME::MimeType mimeType = opencmw::MIME::UNKNOWN;
    // TODO public Object specialClass;
    // TODO public UnknownClass unknownClass;
    opencmw::TimingCtx ctx;
};

ENABLE_REFLECTION_FOR(TestQueryClass, param1, param2, mimeType, ctx)

TEST_CASE("map string to class", "[QueryParameterParser]") {
#if 0
    using URI = opencmw::URI<opencmw::RELAXED>; // TODO this could be <> (STRICT), but currently the parser does not allow "/"

    const auto uriString = "https://opencmw.io?param1=Hello&param2=42&mimeType=text/html&specialClass";
    try {
        const auto uri = URI(uriString);
        const auto ctx = opencmw::QueryParameterParser::parseQueryParameter<TestQueryClass>(uri.queryParamMap());
    } catch (const std::exception &e) {
        std::cerr << "exception! " << e.what() << std::endl;
    }

//    REQUIRE_NOTHROW(opencmw::URI<>(uriString));

    const auto uri = URI(uriString);
    const auto ctx = opencmw::QueryParameterParser::parseQueryParameter<TestQueryClass>(uri.queryParamMap());
    REQUIRE(ctx.param1 == "Hello");
    REQUIRE(ctx.param2 == 42);
    REQUIRE(ctx.mimeType == opencmw::MIME::HTML);

    // TODO
    // assertNotNull(ctx.specialClass);
    // assertThrows(IllegalArgumentException.class, () -> QueryParameterParser.parseQueryParameter(TestQueryClass.class, "param1=b;param2=c"));
#endif
}
