#pragma clang diagnostic push
#include <Debug.hpp>
#include <URI.hpp>
#include <catch2/catch.hpp>
#include <iostream>
#include <string_view>

struct TestCase {
    std::string                                                 uri;
    std::optional<std::string>                                  scheme        = {};
    std::optional<std::string>                                  authority     = {};
    std::optional<std::string>                                  user          = {};
    std::optional<std::string>                                  password      = {};
    std::optional<std::string>                                  hostname      = {};
    std::optional<uint16_t>                                     port          = {};
    std::optional<std::string>                                  path          = {};
    std::optional<std::string>                                  queryParam    = {};
    std::unordered_map<std::string, std::optional<std::string>> queryParamMap = {};
    std::optional<std::string>                                  fragment      = {};
};

static const std::array validURIs{
    TestCase{ .uri = {} },
    TestCase{ .uri = "" },
    TestCase{
            .uri           = "http://User:notSoSecretPwd@localhost.com:20/path1/path2/path3/file.ext?k0&k1=v1;k2=v2&k3#cFrag",
            .scheme        = "http",
            .authority     = "User:notSoSecretPwd@localhost.com:20",
            .user          = "User",
            .password      = "notSoSecretPwd",
            .hostname      = "localhost.com",
            .port          = 20,
            .path          = "/path1/path2/path3/file.ext",
            .queryParam    = "k0&k1=v1;k2=v2&k3",
            .queryParamMap = { { "k0", std::nullopt }, { "k1", "v1" }, { "k2", "v2" }, { "k3", std::nullopt } },
            .fragment      = "cFrag" },
    TestCase{ .uri         = "mdp://user@www.fair-acc.io/service/path/resource.format?queryString#frag",
            .scheme        = "mdp",
            .authority     = "user@www.fair-acc.io",
            .user          = "user",
            .hostname      = "www.fair-acc.io",
            .path          = "/service/path/resource.format",
            .queryParam    = "queryString",
            .queryParamMap = { { "queryString", std::nullopt } },
            .fragment      = "frag" },
    TestCase{ .uri         = "rda3://user@www.fair-acc.io/service/path/resource.format?queryString#frag",
            .scheme        = "rda3",
            .authority     = "user@www.fair-acc.io",
            .user          = "user",
            .hostname      = "www.fair-acc.io",
            .path          = "/service/path/resource.format",
            .queryParam    = "queryString",
            .queryParamMap = { { "queryString", std::nullopt } },
            .fragment      = "frag" },
    TestCase{
            .uri       = "mdp://www.fair-acc.io/service/path/resource.format",
            .scheme    = "mdp",
            .authority = "www.fair-acc.io",
            .hostname  = "www.fair-acc.io",
            .path      = "/service/path/resource.format" },
    TestCase{
            .uri           = "http://www.fair-acc.io:8080/service/path/resource.format?queryString#frag",
            .scheme        = "http",
            .authority     = "www.fair-acc.io:8080",
            .hostname      = "www.fair-acc.io",
            .port          = 8080,
            .path          = "/service/path/resource.format",
            .queryParam    = "queryString",
            .queryParamMap = { { "queryString", std::nullopt } },
            .fragment      = "frag" },
    TestCase{
            .uri           = "//www.fair-acc.io/service/path/resource.format?queryString#frag",
            .authority     = "www.fair-acc.io",
            .hostname      = "www.fair-acc.io",
            .path          = "/service/path/resource.format",
            .queryParam    = "queryString",
            .queryParamMap = { { "queryString", std::nullopt } },
            .fragment      = "frag" },
    TestCase{
            .uri           = "mdp://user@www.fair-acc.io/service/path/resource.format?queryString",
            .scheme        = "mdp",
            .authority     = "user@www.fair-acc.io",
            .user          = "user",
            .hostname      = "www.fair-acc.io",
            .path          = "/service/path/resource.format",
            .queryParam    = "queryString",
            .queryParamMap = { { "queryString", std::nullopt } },
    },
    TestCase{
            .uri       = "mdp://user@www.fair-acc.io/service/path/#onlyFrag",
            .scheme    = "mdp",
            .authority = "user@www.fair-acc.io",
            .user      = "user",
            .hostname  = "www.fair-acc.io",
            .path      = "/service/path/",
            .fragment  = "onlyFrag" },
    TestCase{
            .uri       = "mdp://user@www.fair-acc.io#frag",
            .scheme    = "mdp",
            .authority = "user@www.fair-acc.io",
            .user      = "user",
            .hostname  = "www.fair-acc.io",
            .fragment  = "frag" },
    TestCase{
            .uri           = "mdp://www.fair-acc.io?query",
            .scheme        = "mdp",
            .authority     = "www.fair-acc.io",
            .hostname      = "www.fair-acc.io",
            .queryParam    = "query",
            .queryParamMap = { { "query", std::nullopt } },
    },
    TestCase{
            .uri           = "mdp://www.fair-acc.io?query#frag",
            .scheme        = "mdp",
            .authority     = "www.fair-acc.io",
            .hostname      = "www.fair-acc.io",
            .queryParam    = "query",
            .queryParamMap = { { "query", std::nullopt } },
            .fragment      = "frag" },
    TestCase{
            .uri           = "mdp://user:pwd@www.fair-acc.io/service/path/resource.format?format=mp4&height=360;a=2#20",
            .scheme        = "mdp",
            .authority     = "user:pwd@www.fair-acc.io",
            .user          = "user",
            .password      = "pwd",
            .hostname      = "www.fair-acc.io",
            .path          = "/service/path/resource.format",
            .queryParam    = "format=mp4&height=360;a=2",
            .queryParamMap = { { "format", "mp4" }, { "height", "360" }, { "a", "2" } },
            .fragment      = "20" },
    TestCase{
            .uri       = "mdp://www.fair-acc.io",
            .scheme    = "mdp",
            .authority = "www.fair-acc.io",
            .hostname  = "www.fair-acc.io",
    },
    TestCase{
            .uri           = "?queryOnly",
            .queryParam    = "queryOnly",
            .queryParamMap = { { "queryOnly", std::nullopt } },
    },
    TestCase{
            .uri           = "?k0&k1",
            .queryParam    = "k0&k1",
            .queryParamMap = { { "k0", std::nullopt }, { "k1", std::nullopt } },
    },
    TestCase{
            .uri           = "?k0&k1=v1;k2=v2&k3=",
            .queryParam    = "k0&k1=v1;k2=v2&k3=",
            .queryParamMap = { { "k0", std::nullopt }, { "k1", "v1" }, { "k2", "v2" }, { "k3", std::nullopt } },
    },
    TestCase{
            .uri      = "#fragmentOnly",
            .fragment = "fragmentOnly" }
};

TEST_CASE("parsing and builder-parser identity", "[URI]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("URI<>() - parsing and builder-parser identity", 40);

    for (const auto &testCase : validURIs) {
        REQUIRE_NOTHROW(opencmw::URI<>(testCase.uri));
        const auto uri = opencmw::URI<>(testCase.uri);

        REQUIRE(uri.empty() == testCase.uri.empty());
        REQUIRE(uri.scheme() == testCase.scheme);
        REQUIRE(uri.authority() == testCase.authority);
        REQUIRE(uri.user() == testCase.user);
        REQUIRE(uri.password() == testCase.password);
        REQUIRE(uri.hostName() == testCase.hostname);
        REQUIRE(uri.port() == testCase.port);
        REQUIRE(uri.path() == testCase.path);
        REQUIRE(uri.queryParam() == testCase.queryParam);
        REQUIRE(uri.queryParamMap() == testCase.queryParamMap);
        REQUIRE(uri.fragment() == testCase.fragment);

        // ensure operators works correctly comparing a fully parsed and a freshly created URI
        const auto uriFresh = opencmw::URI<>(testCase.uri);
        REQUIRE(uri == uriFresh);
        REQUIRE_FALSE(uri != uriFresh);
        REQUIRE((uri <=> uriFresh) == std::strong_ordering::equivalent);
        REQUIRE_FALSE(uri < uriFresh);
        REQUIRE_FALSE(uri > uriFresh);

        // check builder-parser identity
        const auto dst = opencmw::URI<>::factory(uri).toString();
        REQUIRE(uri == opencmw::URI<>(dst));

        // ensure copy stays valid when original is destroyed
        auto source = std::make_unique<opencmw::URI<>>(testCase.uri);
        auto copy   = *source;
        source.reset();

        REQUIRE(copy == uri);
        REQUIRE((copy <=> uri) == std::strong_ordering::equivalent);
        REQUIRE(copy.scheme() == uri.scheme());
        REQUIRE(copy.authority() == uri.authority());
        REQUIRE(copy.user() == uri.user());
        REQUIRE(copy.password() == uri.password());
        REQUIRE(copy.hostName() == uri.hostName());
        REQUIRE(copy.port() == uri.port());
        REQUIRE(copy.path() == uri.path());
        REQUIRE(copy.fragment() == uri.fragment());
        REQUIRE(copy.queryParam() == uri.queryParam());
        REQUIRE(copy.queryParamMap() == uri.queryParamMap());
    }

    // invalid URIs that throw on construction
    REQUIRE_THROWS_AS(opencmw::URI<>(" "), opencmw::URISyntaxException);
    REQUIRE_THROWS_AS(opencmw::URI<>("invalid}scheme://host"), opencmw::URISyntaxException);
    REQUIRE_THROWS_AS(opencmw::URI<>("http://invalid{host"), opencmw::URISyntaxException);
    REQUIRE_THROWS_AS(opencmw::URI<>("http://invalid/path%zz"), opencmw::URISyntaxException);

    // invalid query only throws when parsed
    opencmw::URI<> invalidQuery("/path?invalidQuery=*");
    REQUIRE(invalidQuery.queryParam() == "invalidQuery=*");
    REQUIRE_THROWS_AS(invalidQuery.queryParamMap(), std::exception);

    opencmw::URI<> invalidQuery2("/path?invalidQuery=%zz");
    REQUIRE_THROWS_AS(invalidQuery2.queryParamMap(), opencmw::URISyntaxException);
}

TEST_CASE("factory-builder API", "[URI]") {
    using namespace opencmw;
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("URI<>() - factory-builder API", 40);

    REQUIRE(URI<>::factory().toString() == "");
    REQUIRE(URI<>::factory().scheme("mdp").authority("authority").toString() == "mdp://authority");
    REQUIRE(URI<>::factory().scheme("mdp").authority("authority").path("path").toString() == "mdp://authority/path");
    REQUIRE(URI<>::factory().scheme("file").path("path").toString() == "file:path");
    REQUIRE(URI<>::factory().scheme("mdp").hostName("localhost").port(8080).path("path").toString() == "mdp://localhost:8080/path");
    REQUIRE(URI<>::factory().scheme("mdp").hostName("localhost").port(8080).path("path").queryParam("key=value").fragment("fragment").toString() == "mdp://localhost:8080/path?key=value#fragment");
    REQUIRE(URI<>::factory().scheme("mdp").hostName("localhost").port(8080).path("path").fragment("fragment").toString() == "mdp://localhost:8080/path#fragment");
    REQUIRE(URI<>::factory().scheme("mdp").user("user").hostName("localhost").port(8080).path("path").queryParam("key=value").fragment("fragment").toString() == "mdp://user@localhost:8080/path?key=value#fragment");
    REQUIRE(URI<>::factory().scheme("mdp").user("user").password("pwd").hostName("localhost").port(8080).path("path").queryParam("key=value").fragment("fragment").toString() == "mdp://user:pwd@localhost:8080/path?key=value#fragment");
    REQUIRE(URI<>::factory().scheme("mdp").password("pwd").hostName("localhost").port(8080).path("path").queryParam("key=value").fragment("fragment").toString() == "mdp://localhost:8080/path?key=value#fragment");
    REQUIRE(URI<>::factory().queryParam("queryOnly").toString() == "?queryOnly");
    REQUIRE(URI<>::factory().fragment("fragmentOnly").toString() == "#fragmentOnly");

    REQUIRE(URI<>::factory().scheme("mdp").authority("authority").build().authority() == "authority");

    // parameter handling
    REQUIRE(URI<>::factory(opencmw::URI<>(validURIs[11].uri)).queryParam("").addQueryParameter("keyOnly").addQueryParameter("key", "value").toString() == "mdp://www.fair-acc.io?key=value&keyOnly");
    REQUIRE(URI<>::factory(opencmw::URI<>(validURIs[11].uri)).addQueryParameter("keyOnly").addQueryParameter("key", "value").toString() == "mdp://www.fair-acc.io?query&key=value&keyOnly");
}

TEST_CASE("helper methods", "[URI]") {
    using namespace opencmw;
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("URI<>() - helper methods", 40);
    constexpr auto        validURICharacters = std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~:/?#[]@!$&'()*+,;=");

    for (auto c : validURICharacters) {
        // implicitly tests URI<>::isUnreserved(c) for whole URI
        REQUIRE_NOTHROW_MESSAGE(URI<STRICT>(fmt::format("a{}", c)), fmt::format("test character in whole URI: '{}'", c));
    }
    for (char c = 0; c < 127; c++) {
        if (validURICharacters.find(c, 0) != std::string_view::npos) {
            continue;
        }
        std::string test("abc4");
        test[3] = c;
        // implicitly tests URI<>::isUnreserved(c)/invalid characters for whole URI
        REQUIRE_THROWS_AS_MESSAGE(URI<STRICT>(test), std::ios_base::failure, fmt::format("test character in URL: '{:c}'", c));
    }

    for (auto c : std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")) {
        // implicitly tests URI<>::isUnreserved(c) for scheme
        REQUIRE_NOTHROW_MESSAGE(URI<STRICT>(fmt::format("aa{}:", c)), fmt::format("test character in scheme: '{}'", c));
    }
    for (auto c : std::string("-._~/?#[]@!$&'()*+,;=")) { // N.B. special case for delimiter ':' -- "::" failure case not covered
        // implicitly tests URI<>::isUnreserved(c)/invalid characters  for scheme
        REQUIRE_THROWS_AS_MESSAGE(URI<STRICT>(fmt::format("aa{}:", c)), std::ios_base::failure, fmt::format("test character in scheme: '{}'", c));
    }

    for (auto c : std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._:")) {
        // implicitly tests URI<>::isUnreserved(c) for authority
        REQUIRE_NOTHROW_MESSAGE(URI<STRICT>(fmt::format("mdp://{}", c)), fmt::format("test character in authority: '{}'", c));
    }
    for (auto c : std::string("~[]!$&'()*+,;=")) { // N.B. special case for delimiter '/' -- "///" failure case not covered
        // implicitly tests URI<>::isUnreserved(c)/invalid characters for authority
        REQUIRE_THROWS_AS_MESSAGE(URI<STRICT>(fmt::format("mdp://host{}", c)), std::ios_base::failure, fmt::format("test character in authority: '{}'", c));
    }
    for (auto c : std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._/")) {
        // implicitly tests URI<>::isUnreserved(c) for authority
        REQUIRE_NOTHROW_MESSAGE(URI<STRICT>(fmt::format("mdp://auth/a{}", c)), fmt::format("test character in path: '{}'", c));
    }
    for (auto c : std::string("~[]!$&'()*+,;=")) {
        // implicitly tests URI<>::isUnreserved(c)/invalid characters for authority
        REQUIRE_THROWS_AS_MESSAGE(URI<STRICT>(fmt::format("mdp://auth/a{}", c)), std::ios_base::failure, fmt::format("test character in path: '{}'", c));
    }

    REQUIRE(URI<>::encode("ASCIIString") == "ASCIIString");
    REQUIRE(URI<>::encode("Weird\"String\"with%,{}aa+") == "Weird%22String%22with%25%2C%7B%7Daa%2B");
    REQUIRE(URI<>::decode(opencmw::URI<>::encode("Weird\"String\"with%,{}")) == "Weird\"String\"with%,{}");

    for (char c = -128; c < 127; c++) {
        std::string test("abc4");
        test[3] = c;
        REQUIRE_MESSAGE(URI<>::decode(opencmw::URI<>::encode(test)) == test, std::string("failed for: '") + test + "'");
    }

    // print handler
    std::ostringstream dummyStream;
    auto               resetStream = [&dummyStream]() { dummyStream.str(""); dummyStream.clear(); REQUIRE(dummyStream.str().size() == 0); };
    dummyStream << fmt::format("URI fmt::print: '{}'\n", URI<>("mdp://auth/path"));
    REQUIRE(dummyStream.str().size() != 0);
    resetStream();
    dummyStream << "std::cout URI print: " << URI<>("mdp://auth/path") << std::endl;
    REQUIRE(dummyStream.str().size() != 0);
    resetStream();

    // optional handler
    std::optional<std::string> optional("test");
    dummyStream << fmt::format("test std::optional fmt::print: '{}'\n", optional);
    REQUIRE(dummyStream.str().size() != 0);
    resetStream();
    dummyStream << "std::cout std::optional print: " << optional << std::endl;
    REQUIRE(dummyStream.str().size() != 0);
    resetStream();
}

TEST_CASE("lifetime", "[URI]") {
    using namespace opencmw;
    auto make_uri = [](std::string scheme) {
        struct kill_nrvo {
            URI<> uri;
        };
        auto uri = URI<>::factory().scheme(scheme).authority("authority").build();
        return kill_nrvo{ uri }.uri;
    };

    auto uri    = make_uri("mdp");
    std::ignore = make_uri("http");

    REQUIRE(uri.scheme().value() == "mdp");
}

#pragma clang diagnostic pop
