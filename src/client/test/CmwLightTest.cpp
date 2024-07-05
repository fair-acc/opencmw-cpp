#include <catch2/catch.hpp>
#include <Client.hpp>
#include <CmwLightClient.hpp>
#include <DirectoryLightClient.hpp>
#include <fmt/core.h>

TEST_CASE("RDA3", "[Client]") {
    std::string nameserverExample = R"""(GSCD025 DigitizerDU2.dal025 rda3://9#Address:#string#18#tcp:%2F%2Fdal025:16134#ApplicationId:#string#114#app=DigitizerDU2;uid=root;host=dal025;pid=16912;os=Linux%2D3%2E10%2E101%2Drt111%2Dscu03;osArch=64bit;appArch=64bit;lang=C%2B%2B;#Language:#string#3#C%2B%2B#Name:#string#19#DigitizerDU2%2Edal025#Pid:#int#16912#ProcessName:#string#12#DigitizerDU2#StartTime:#long#1699343695922#UserName:#string#4#root#Version:#string#5#3%2E1%2E0
GSCD023 DigitizerDU2.fel0053 rda3://9#Address:#string#18#tcp:%2F%2Ffel0053:3717#ApplicationId:#string#115#app=DigitizerDU2;uid=root;host=fel0053;pid=31447;os=Linux%2D3%2E10%2E101%2Drt111%2Dscu03;osArch=64bit;appArch=64bit;lang=C%2B%2B;#Language:#string#3#C%2B%2B#Name:#string#20#DigitizerDU2%2Efel0053#Pid:#int#31447#ProcessName:#string#12#DigitizerDU2#StartTime:#long#1701529074225#UserName:#string#4#root#Version:#string#5#3%2E1%2E0
FantasyDevice3000 *UNKNOWN* *UNKNOWN*)""";
    std::string nameserver        = "tcp://cmwpro00a.acc.gsi.de:5021";

    SECTION("ParseNameserverReply") {
        std::map<std::string, std::map<std::string, std::map<std::string, std::variant<std::string, int, long>>>> devices = parse(nameserverExample);
        REQUIRE(!devices["GSCD023"].empty());
        REQUIRE(!devices["GSCD025"].empty());
        REQUIRE(devices["FantasyDevice3000"].empty());
        REQUIRE(std::get<std::string>(devices["GSCD025"]["rda3://"]["Address"]) == "tcp://dal025:16134");
    }

    SECTION("Query rda3 directory server/nameserver") {
        opencmw::zmq::Context ctx{};
        auto                  result = resolveDirectoryLight({ "GSCD025", "GSCD023", "FantasyDevice3000" }, nameserver, ctx, 100ms);
        REQUIRE(!result.empty());
        REQUIRE(result == nameserverExample);
    };
}

// small utility function that prints the content of a string in the classic hexedit way with address, hexadecimal and ascii representations
static std::string hexview(const std::string_view value, std::size_t bytesPerLine = 4) {
    std::string result;
    result.reserve(value.size() * 4);
    std::string alpha; // temporarily store the ascii representation
    alpha.reserve(8 * bytesPerLine);
    for (auto [i, c] : std::ranges::views::enumerate(value)) {
        if (i % (bytesPerLine * 8) == 0) {
            result.append(fmt::format("{0:#08x} - {0:04} | ", i)); // print address in hex and decimal
        }
        result.append(fmt::format("{:02x} ", c));
        alpha.append(fmt::format("{}", std::isprint(c) ? c : '.'));
        if ((i + 1) % 8 == 0) {
            result.append("   ");
            alpha.append(" ");
        }
        if ((i + 1) % (bytesPerLine * 8) == 0) {
            result.append(fmt::format("   {}\n", alpha));
            alpha.clear();
        }
    }
    result.append(fmt::format("{:{}}   {}\n", "", 3 * (9 * bytesPerLine - alpha.size()), alpha));
    return result;
};

TEST_CASE("BasicCmwLight example", "[Client]") {
    const std::string digitizerAddress{ "tcp://dal007:2620" };
    // filters2String = "acquisitionModeFilter=int:0&channelNameFilter=GS11MU2:Voltage_1@10Hz";
    // subscribe("r1", new URI("rda3", null, '/' + DEVICE + '/' + PROPERTY, "ctx=" + SELECTOR + "&" + filtersString, null), null);
    // DEVICE = "GSCD002";
    // PROPERTY = "AcquisitionDAQ";
    // SELECTOR = "FAIR.SELECTOR.ALL";
    using namespace opencmw;
    const zmq::Context                                        zctx{};
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::cmwlight::CmwLightClientCtx>(zctx, 20ms, "testMajordomoClient"));
    opencmw::client::ClientContext clientContext{ std::move(clients) };
    // send some requests
    auto             endpoint = URI<STRICT>::factory(URI<STRICT>(digitizerAddress)).scheme("rda3tcp").path("/GSCD002/Version").build();

    std::atomic<int> received{ 0 };
    clientContext.get(endpoint, [&received](const mdp::Message &message) {
        fmt::print("{}", hexview(message.data.asString()));
        received++;
    });

    auto subscriptionEndpoint = URI<STRICT>::factory(URI<STRICT>(digitizerAddress)).scheme("rda3tcp").path("/GSCD002/AcquisitionDAQ").addQueryParameter("ctx", "FAIR.SELECTOR.ALL").build();
    clientContext.subscribe(endpoint, [&received](const mdp::Message &message) {
        fmt::print("{}", hexview(message.data.asString()));
        received++;
    });

    std::this_thread::sleep_for(8000ms); // allow the request to reach the server
    REQUIRE(received == 1);
}
