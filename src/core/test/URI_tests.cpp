#pragma clang diagnostic push
#include <catch2/catch.hpp>
#include <Debug.hpp>
#include <iostream>
#include <string_view>
#include <URI.hpp>

opencmw::URI<> getUri() {
    return opencmw::URI<>(std::string{ "mdp://User:notSoSecret@localhost.com:20/path/file.ext?queryString#cFrag" });
}

TEST_CASE("basic constructor", "[URI]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("URI<>() - basic constructor", 40);

    REQUIRE_NOTHROW(opencmw::URI<>(""));
    opencmw::URI<> emptyURI("");
    REQUIRE(emptyURI.empty());
    REQUIRE(!emptyURI.scheme());
    REQUIRE(!emptyURI.authority());
    REQUIRE(!emptyURI.user());
    REQUIRE(!emptyURI.password());
    REQUIRE(!emptyURI.hostName());
    REQUIRE(!emptyURI.port());
    REQUIRE(!emptyURI.path());
    REQUIRE(!emptyURI.queryParam());
    REQUIRE(!emptyURI.fragment());
    REQUIRE(emptyURI.queryParamMap().empty());

    constexpr auto testURL = "http://User:notSoSecretPwd@localhost.com:20/path1/path2/path3/file.ext?k0;k1=v1;k2=v2&k3&k4=#cFrag";
    REQUIRE_NOTHROW(opencmw::URI<>(testURL));
    // basic tests
    opencmw::URI test(testURL);

    REQUIRE(!test.empty());
    REQUIRE(test.scheme() == "http");
    REQUIRE(test.authority() == "User:notSoSecretPwd@localhost.com:20");
    REQUIRE(test.user() == "User");
    REQUIRE(test.password() == "notSoSecretPwd");
    REQUIRE(test.hostName() == "localhost.com");
    REQUIRE(test.port() == 20);
    REQUIRE(test.path() == "/path1/path2/path3/file.ext");
    REQUIRE(test.queryParam() == "k0;k1=v1;k2=v2&k3&k4=");
    REQUIRE(test.fragment() == "cFrag");
    // test parameter map interface
    REQUIRE_NOTHROW(test.queryParamMap());
    auto parameterMap = test.queryParamMap();
    REQUIRE(parameterMap.size() == 5);
    REQUIRE(parameterMap["k0"] == std::nullopt);
    REQUIRE(parameterMap["k1"] == "v1");
    REQUIRE(parameterMap["k2"] == "v2");
    REQUIRE(parameterMap["k3"] == std::nullopt);
    REQUIRE(parameterMap["k4"] == std::nullopt);

    REQUIRE(getUri().authority().value() == "User:notSoSecret@localhost.com:20");

    REQUIRE(opencmw::URI(testURL) == test);

    // ensure copy stays valid when original is destroyed
    auto source = std::make_unique<opencmw::URI<>>(testURL);
    auto copy   = *source;
    source.reset();

    REQUIRE(copy == test);
    REQUIRE(copy.scheme() == test.scheme());
    REQUIRE(copy.authority() == test.authority());
    REQUIRE(copy.user() == test.user());
    REQUIRE(copy.password() == test.password());
    REQUIRE(copy.hostName() == test.hostName());
    REQUIRE(copy.port() == test.port());
    REQUIRE(copy.path() == test.path());
    REQUIRE(copy.fragment() == test.fragment());
    REQUIRE(copy.queryParam() == test.queryParam());
    REQUIRE(copy.queryParamMap() == test.queryParamMap());
}

TEST_CASE("query parsing", "[URI][query_parsing]") {
    using TestCase                    = std::pair<std::string, std::unordered_map<std::string, std::optional<std::string>>>;

    static const std::array testCases = {
        TestCase{ "scheme:/host/property", {} },
        TestCase{ "scheme:/host/property?testKey1=42", { { "testKey1", "42" } } },
        TestCase{ "scheme:/host/property?testKey1=42&testKey2=24", { { "testKey1", "42" }, { "testKey2", "24" } } },
        TestCase{ "scheme:/host/property?k0;k1=v1;k2=v2&k3&k4=", { { "k0", std::nullopt }, { "k1", "v1" }, { "k2", "v2" }, { "k3", std::nullopt }, { "k4", std::nullopt } } }
    };

    for (const auto &testCase : testCases) {
        REQUIRE_NOTHROW(opencmw::URI<>(testCase.first));
        REQUIRE(opencmw::URI<>(testCase.first).queryParamMap() == testCase.second);
    }
}

static const std::array validURIs{
    "",
    "http://User:notSoSecretPwd@localhost.com:20/path1/path2/path3/file.ext?k0&k1=v1;k2=v2&k3#cFrag",
    "mdp://user@www.fair-acc.io/service/path/resource.format?queryString#frag",
    "mdp://www.fair-acc.io/service/path/resource.format",
    "http://www.fair-acc.io:8080/service/path/resource.format?queryString#frag",
    "https://www.fair-acc.io:8080/service/path/resource.format?queryString#frag",
    "mdp://www.fair-acc.io:8080/service/path/resource.format?queryString#frag",
    "rda3://www.fair-acc.io/service/path/resource.format?queryString#fragRda3",
    "mdp://www.fair-acc.io/service/path/resource.format?queryString#frag",
    "//www.fair-acc.io/service/path/resource.format?queryString#frag",
    "mdp://user@www.fair-acc.io/service/path/resource.format?queryString",
    "mdp://user@www.fair-acc.io/service/path/#onlyFrag",
    "mdp://user@www.fair-acc.io#frag",
    "mdp://www.fair-acc.io?query",
    "mdp://www.fair-acc.io?query#frag",
    "mdp://user:pwd@www.fair-acc.io/service/path/resource.format?format=mp4&height=360;a=2#20",
    "mdp://www.fair-acc.io",
    "?queryOnly",
    "#fagmentOnly",
};

TEST_CASE("Show current issues", "[URI]") {
    using QueryMap = std::unordered_map<std::string, std::optional<std::string>>;

    // parsing is confused by ":" in the query param
    // TODO fix or insist on percent-escaped ":"
    // CHECK(opencmw::URI<opencmw::RELAXED>("/property?ctx=FAIR.SELECTOR.C=2:P=1").path() == "/property"); // path() == "P=1"

    // /, = (and others) are dropped when decoding query param values
    CHECK(opencmw::URI<opencmw::RELAXED>("/property?ctx=FAIR.SELECTOR.C=2").queryParamMap().at("ctx") == "FAIR.SELECTOR.C=2"); // FAIR.SELECTOR.C2
    CHECK(opencmw::URI<opencmw::RELAXED>("/property?contentType=text/html").queryParamMap().at("contentType") == "text/html"); // "texthtml"

    // STRICT parsing throws on these (valid?) URIs
    // TODO fix or assert that the throwing is wanted
    // CHECK_NOTHROW(opencmw::URI<opencmw::STRICT>("/property?ctx=FAIR.SELECTOR.C=2:P=1"));
    // CHECK_NOTHROW(opencmw::URI<opencmw::STRICT>("/property?contentType=text/html").queryParamMap());

    // last query item is dropped
    CHECK(opencmw::URI<opencmw::STRICT>("scheme:/foo/bar.txt?k0=v0;k1=").queryParamMap() == QueryMap{ { "k0", "v0" }, { "k1", {} } }); // "k1" is missing
}

TEST_CASE("builder-parser identity", "[URI]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("URI<>() - builder-parser identity", 40);

    for (auto uri : validURIs) {
        REQUIRE_NOTHROW(opencmw::URI<>(uri));

        // check identity
        const auto src = opencmw::URI<>(uri);
        const auto dst = opencmw::URI<>::factory(src).toString();
        REQUIRE(src == opencmw::URI<>(dst));
    }
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
    REQUIRE(URI<>::factory(opencmw::URI<>(validURIs[13])).queryParam("").addQueryParameter("keyOnly").addQueryParameter("key", "value").toString() == "mdp://www.fair-acc.io?key=value&keyOnly");
    REQUIRE(URI<>::factory(opencmw::URI<>(validURIs[13])).addQueryParameter("keyOnly").addQueryParameter("key", "value").toString() == "mdp://www.fair-acc.io?query&key=value&keyOnly");
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
