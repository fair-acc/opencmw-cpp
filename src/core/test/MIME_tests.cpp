#include <catch2/catch.hpp>
#include <Debug.hpp>
#include <iostream>
#include <MIME.hpp>
#include <sstream>
#include <string_view>

TEST_CASE("basic access", "[MIME]") {
    using namespace opencmw;
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("MIME - basic constructor", 40);

    // simple tests
    REQUIRE(MIME::getType(MIME::TEXT.typeName()) == MIME::TEXT);
    REQUIRE(MIME::getType("text/plain") == MIME::TEXT);
    REQUIRE(MIME::getType("text/PLAIN") == MIME::TEXT); // checks upper/lower case insensitivity
    REQUIRE(MIME::getTypeByFileName("readme.txt") == MIME::TEXT);
    REQUIRE(MIME::getTypeByFileName("README.TXT") == MIME::TEXT);

    // test constexpr evaluation
    static_assert(MIME::getType("text/plain") == MIME::TEXT);
    static_assert(MIME::getTypeByFileName("TEST.TXT") == MIME::TEXT);

    for (auto &type : MIME::ALL) {
        REQUIRE_MESSAGE(MIME::getType(type.typeName()) == type, std::format("error for type '{}'", type.typeName()));
        for (auto fileExt : type.fileExtensions()) {
            REQUIRE_MESSAGE(MIME::getTypeByFileName(std::format("FileName{}", fileExt)) == type, std::format("error for type '{}' and ext '{}'", type.typeName(), fileExt));
        }

        // test print handler
        std::ostringstream dummyStream;
        auto               resetStream = [&dummyStream]() { dummyStream.str(""); dummyStream.clear(); REQUIRE(dummyStream.str().size() == 0); };
        dummyStream << std::format("MIME::MimeType std::print: '{}'\n", type);
        REQUIRE(dummyStream.str().size() != 0);
        resetStream();
        dummyStream << "std::cout MIME::MimeType print: " << type << std::endl;
        REQUIRE(dummyStream.str().size() != 0);
        resetStream();
    }

    // test error cases
    REQUIRE(MIME::getType("") == MIME::UNKNOWN);
    REQUIRE(MIME::getType("unknown/MIME_TYPE") == MIME::UNKNOWN);
    REQUIRE(MIME::getTypeByFileName("") == MIME::UNKNOWN);
    REQUIRE(MIME::getTypeByFileName("FileName.unknown") == MIME::UNKNOWN);

    const char *_typeN = MIME::TEXT;
    REQUIRE(_typeN == std::string_view("text/plain"));

    std::vector<opencmw::MIME::MimeType> v{ MIME::TEXT, MIME::JAR };
    std::cout << v;
}
