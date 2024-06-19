#include <catch2/catch.hpp>
#include <fmt/core.h>
#include <DirectoryLightClient.hpp>

TEST_CASE("RDA3", "[Client]") {
    std::string                                               nameserverExample = R"""(GSCD025 DigitizerDU2.dal025 rda3://9#Address:#string#18#tcp:%2F%2Fdal025:16134#ApplicationId:#string#114#app=DigitizerDU2;uid=root;host=dal025;pid=16912;os=Linux%2D3%2E10%2E101%2Drt111%2Dscu03;osArch=64bit;appArch=64bit;lang=C%2B%2B;#Language:#string#3#C%2B%2B#Name:#string#19#DigitizerDU2%2Edal025#Pid:#int#16912#ProcessName:#string#12#DigitizerDU2#StartTime:#long#1699343695922#UserName:#string#4#root#Version:#string#5#3%2E1%2E0
GSCD023 DigitizerDU2.fel0053 rda3://9#Address:#string#18#tcp:%2F%2Ffel0053:3717#ApplicationId:#string#115#app=DigitizerDU2;uid=root;host=fel0053;pid=31447;os=Linux%2D3%2E10%2E101%2Drt111%2Dscu03;osArch=64bit;appArch=64bit;lang=C%2B%2B;#Language:#string#3#C%2B%2B#Name:#string#20#DigitizerDU2%2Efel0053#Pid:#int#31447#ProcessName:#string#12#DigitizerDU2#StartTime:#long#1701529074225#UserName:#string#4#root#Version:#string#5#3%2E1%2E0
FantasyDevice3000 *UNKNOWN* *UNKNOWN*)""";
    std::string nameserver = "tcp://cmwpro00a.acc.gsi.de:5021";

    SECTION("ParseNameserverReply") {
        std::map<std::string, std::map<std::string, std::map<std::string, std::variant<std::string, int, long>>>> devices = parse(nameserverExample);
        REQUIRE(!devices["GSCD023"].empty());
        REQUIRE(!devices["GSCD025"].empty());
        REQUIRE(devices["FantasyDevice3000"].empty());
        REQUIRE(std::get<std::string>(devices["GSCD025"]["rda3://"]["Address"]) == "tcp://dal025:16134");
    }

    SECTION("Connect to nameserver with zmq native pattern (ZMQ_STREAM)") {
        opencmw::zmq::Context ctx{};
        auto result = resolveDirectoryLight({"GSCD025", "GSCD023", "FantasyDevice3000"}, nameserver, ctx, 100ms);
        REQUIRE(!result.empty());
        REQUIRE(result == nameserverExample);
    };
}
