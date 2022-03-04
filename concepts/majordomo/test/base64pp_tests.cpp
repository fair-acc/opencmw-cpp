// taken from https://github.com/matheusgomes28/base64pp
// Ported from gtest to catch2

#define CATCH_CONFIG_MAIN // This tells the catch header to generate a main

#include <array>
#include <base64pp/base64pp.h>
#include <cstdint>
#include <catch2/catch.hpp>
#include <string>

#define ASSERT_EQ(a, b) { REQUIRE(a == b); }

TEST_CASE("Encode empty string", "[encode]")
{
    std::string const expected{};
    std::string const actual{base64pp::encode({})};
    ASSERT_EQ(expected, actual);
}

TEST_CASE("EncodesThreeBytesZeros", "[encode]")
{
    std::array<std::uint8_t, 3> const input{0x00, 0x00, 0x00};
    auto const expected{"AAAA"};
    auto const actual{base64pp::encode({begin(input), end(input)})};
    ASSERT_EQ(expected, actual);
}

TEST_CASE("EncodesThreeBytesRandom", "[encode]")
{
    std::array<std::uint8_t, 3> const input{0xFE, 0xE9, 0x72};
    auto const expected{"/uly"};
    auto const actual{base64pp::encode({begin(input), end(input)})};
    ASSERT_EQ(expected, actual);
}

TEST_CASE("EncodesTwoBytes", "[encode]")
{
    std::array<std::uint8_t, 2> const input{0x00, 0x00};
    auto const expected{"AAA="};
    auto const actual{base64pp::encode({begin(input), end(input)})};
    ASSERT_EQ(expected, actual);
}

TEST_CASE("EncodesOneByte", "[encode]")
{
    std::array<std::uint8_t, 1> const input{0x00};
    auto const expected{"AA=="};
    auto const actual{base64pp::encode({begin(input), end(input)})};
    ASSERT_EQ(expected, actual);
}

TEST_CASE("EncodesFourBytes", "[encode]")
{
    std::array<std::uint8_t, 4> const input{0x74, 0x68, 0x65, 0x20};
    auto const expected{"dGhlIA=="};
    auto actual{base64pp::encode({begin(input), end(input)})};
    ASSERT_EQ(expected, actual);
}

TEST_CASE("EncodesFiveBytes", "[encode]")
{
    std::array<std::uint8_t, 5> const input{0x20, 0x62, 0x72, 0x6f, 0x77};
    auto const expected{"IGJyb3c="};
    auto const actual{base64pp::encode({begin(input), end(input)})};
    ASSERT_EQ(actual, expected);
}

TEST_CASE("EncodesSixBytes", "[encode]")
{
    std::array<std::uint8_t, 6> const input{0x20, 0x6a, 0x75, 0x6d, 0x70, 0x73};
    auto const expected{"IGp1bXBz"};
    auto const actual{base64pp::encode({begin(input), end(input)})};
    ASSERT_EQ(actual, expected);
}

TEST_CASE("EncodesBrownFox", "[encode]")
{
    std::array<std::uint8_t, 43> const input{0x74, 0x68, 0x65, 0x20, 0x71, 0x75,
        0x69, 0x63, 0x6b, 0x20, 0x62, 0x72, 0x6f, 0x77, 0x6e, 0x20, 0x66, 0x6f,
        0x78, 0x20, 0x6a, 0x75, 0x6d, 0x70, 0x73, 0x20, 0x6f, 0x76, 0x65, 0x72,
        0x20, 0x74, 0x68, 0x65, 0x20, 0x6c, 0x61, 0x7a, 0x79, 0x20, 0x64, 0x6f,
        0x67};

    auto const expected{
        "dGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZw=="};
    auto const actual{base64pp::encode({begin(input), end(input)})};
    ASSERT_EQ(actual, expected);
}

TEST_CASE("EncodesBrownFastFoxNullInMiddle", "[encode]")
{
    std::array<std::uint8_t, 45> const input{0x74, 0x68, 0x65, 0x20, 0x71, 0x75,
        0x69, 0x63, 0x6b, 0x21, 0x20, 0x62, 0x72, 0x6f, 0x77, 0x6e, 0x20, 0x66,
        0x6f, 0x78, 0x20, 0x6a, 0x75, 0x6d, 0x70, 0x73, 0x20, 0x6f, 0x76, 0x65,
        0x72, 0x20, 0x74, 0x68, 0x65, 0x00, 0x20, 0x6c, 0x61, 0x7a, 0x79, 0x20,
        0x64, 0x6f, 0x67};

    auto const expected{
        "dGhlIHF1aWNrISBicm93biBmb3gganVtcHMgb3ZlciB0aGUAIGxhenkgZG9n"};
    auto const actual{base64pp::encode({begin(input), end(input)})};
    ASSERT_EQ(actual, expected);
}

TEST_CASE("FailDecodeOneString", "[decode]")
{
    std::string const input{"1"};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, std::nullopt);
}

TEST_CASE("FailDecodeOneStringPadded", "[decode]")
{
    std::string const input{"1==="};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, std::nullopt);
}

TEST_CASE("FailDecodeTwoString", "[decode]")
{
    std::string const input{"12"};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, std::nullopt);
}

TEST_CASE("FailDecodeThreeString", "[decode]")
{
    std::string const input{"12a"};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, std::nullopt);
}

TEST_CASE("FailDecodeNonSize4", "[decode]")
{
    std::string const input{"something"};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, std::nullopt);
}

TEST_CASE("FailDecodeNonSize4Bigger", "[decode]")
{
    std::string const input{"SomethingEntirelyDifferent"};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, std::nullopt);
}

TEST_CASE("FailDecodeNonBase64Short", "[decode]")
{
    std::string const input{"a aa"};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, std::nullopt);
}

TEST_CASE("FailDecodeNonBase64Longer", "[decode]")
{
    std::string const input{"aaa`"};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, std::nullopt);
}

TEST_CASE("DecodesEmptyString", "[decode]")
{
    std::string const input{};
    std::vector<std::uint8_t> expected{};
    auto const actual{base64pp::decode("")};

    ASSERT_EQ(expected, actual);
}

TEST_CASE("DecodesZeroArray", "[decode]")
{
    std::string const input{"AAAA"};
    std::vector<std::uint8_t> const expected{0x00, 0x00, 0x00};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, expected);
}

TEST_CASE("DecodesZeroArrayTwice", "[decode]")
{
    std::string const input{"AAAAAAAA"};
    std::vector<std::uint8_t> const expected{
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, expected);
}

TEST_CASE("DecodesZeroArrayOneByte", "[decode]")
{
    std::string const input{"AA=="};
    std::vector<std::uint8_t> const expected{0x00};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, expected);
}

TEST_CASE("DecodesZeroArrayTwoBytes", "[decode]")
{
    std::string const input{"AAA="};
    std::vector<std::uint8_t> const expected{0x00, 0x00};
    auto const actual{base64pp::decode(input)};

    ASSERT_EQ(actual, expected);
}

TEST_CASE("DecodesQuickFox", "[decode]")
{
    std::string const input{
        "VGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZw=="};
    std::vector<std::uint8_t> const expected{0x54, 0x68, 0x65, 0x20, 0x71, 0x75,
        0x69, 0x63, 0x6b, 0x20, 0x62, 0x72, 0x6f, 0x77, 0x6e, 0x20, 0x66, 0x6f,
        0x78, 0x20, 0x6a, 0x75, 0x6d, 0x70, 0x73, 0x20, 0x6f, 0x76, 0x65, 0x72,
        0x20, 0x74, 0x68, 0x65, 0x20, 0x6c, 0x61, 0x7a, 0x79, 0x20, 0x64, 0x6f,
        0x67};
    auto const actual{base64pp::decode(input)};
    ASSERT_EQ(actual, expected);
}
