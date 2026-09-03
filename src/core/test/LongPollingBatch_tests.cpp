#include <LongPollingBatch.hpp>

#include <catch2/catch.hpp>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using namespace opencmw;

TEST_CASE("long_polling::parseUnsigned", "[core][rest][long-polling]") {
    for (const auto value : { std::string_view{}, std::string_view("-1"), std::string_view("1x"), std::string_view("18446744073709551616") }) {
        CAPTURE(value);
        CHECK_FALSE(long_polling::parseUnsigned(value, long_polling::kIndexParameter).has_value());
    }
    for (const std::uint64_t value : { 0, 5 }) {
        const auto parsed = long_polling::parseUnsigned(std::to_string(value), long_polling::kIndexParameter);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
}

TEST_CASE("Batch encoding and decoding", "[core][rest][long-polling]") {
    SECTION("Binary, empty and text payloads") {
        const std::string                          payload = std::string(1, '\0') + std::format("\r\n--{}--\r\n", long_polling::kBoundary);
        const std::vector<long_polling::BatchPart> original{
            { 42, "/batch", "/batch", payload },
            { 43, "/batch?sample=43", "/batch-service-43", "" },
            { 44, "/batch?sample=44", "/batch-service-44", "next message" }
        };
        const auto encoded = long_polling::encodeBatch(original);
        REQUIRE(encoded.has_value());

        const auto decoded = long_polling::decodeBatch(*encoded);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->size() == original.size());
        for (std::size_t i = 0; i < original.size(); ++i) {
            CHECK((*decoded)[i].index == original[i].index);
            CHECK((*decoded)[i].topic == original[i].topic);
            CHECK((*decoded)[i].serviceName == original[i].serviceName);
            CHECK((*decoded)[i].payload == original[i].payload);
        }
    }

    SECTION("Boundary ending in --") {
        constexpr std::string_view body = "--batch--\r\n"
                                          "x-opencmw-long-polling-idx: 42\r\n"
                                          "x-opencmw-topic: /batch\r\n"
                                          "x-opencmw-service-name: /batch\r\n"
                                          "content-length: 5\r\n\r\n"
                                          "hello\r\n"
                                          "--batch----\r\n";
        const auto decoded = long_polling::decodeBatch(body);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->size() == 1);
        CHECK(decoded->front().index == 42);
        CHECK(decoded->front().payload == "hello");
    }

    SECTION("Empty batch, index gaps and duplicates") {
        for (const auto &parts : std::vector<std::vector<long_polling::BatchPart>>{
                     {},
                     { { 4, "/batch", "/batch", "a" }, { 6, "/batch", "/batch", "b" } },
                     { { 4, "/batch", "/batch", "a" }, { 4, "/batch", "/batch", "b" } } }) {
            const auto encoded = long_polling::encodeBatch(parts);
            REQUIRE(encoded.has_value());
            CHECK_FALSE(long_polling::decodeBatch(*encoded).has_value());
        }
    }

    SECTION("Incomplete multipart bodies") {
        const auto encoded = long_polling::encodeBatch({ { 4, "/batch", "/batch", "data" } });
        REQUIRE(encoded.has_value());
        auto              missingTopic  = *encoded;
        const std::string topicHeader   = "x-opencmw-topic: /batch\r\n";
        const auto        topicPosition = missingTopic.find(topicHeader);
        REQUIRE(topicPosition != std::string::npos);
        missingTopic.erase(topicPosition, topicHeader.size());
        for (const auto &body : {
                     encoded->substr(0, encoded->find("\r\n\r\n") + 4 + 3),
                     encoded->substr(0, encoded->rfind(std::format("--{}--\r\n", long_polling::kBoundary))),
                     missingTopic }) {
            CHECK_FALSE(long_polling::decodeBatch(body).has_value());
        }
    }

    SECTION("Line breaks in topic and service name") {
        for (const std::string_view invalid : { "/batch\rvalue", "/batch\nvalue", "/batch\r\nx-opencmw-topic: /other" }) {
            CHECK_FALSE(long_polling::encodeBatch({ { 0, invalid, "/batch", "data" } }).has_value());
            CHECK_FALSE(long_polling::encodeBatch({ { 0, "/batch", invalid, "data" } }).has_value());
        }
    }
}
