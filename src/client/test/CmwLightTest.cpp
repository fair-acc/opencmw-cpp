#include "MockServer.hpp"
#include "RestClientNative.hpp"
#include <catch2/catch.hpp>
#include <Client.hpp>
#include <CmwLightClient.hpp>
#include <DirectoryLightClient.hpp>
#include <print>

/**
 * Most tests in this suite require a running Test Fesa Server which is registered to a Nameserver.
 * Additionally, the following Environment Variables have to be set:
 *  - CMW_NAMESERVER=http://host:port - URL to the Nameserver to query
 *  - CMW_DEVICE_ADDRESS=tcp://host:port - URL where an instance of the GSITemplateDevice FESA class is running
 *    This could be done via nameserver lookup, but has to be set if the device needs ssh tunnelling with ssh -L local_port:device:port
 *
 * FESA Test Device:
 * - vmla017
 * - DEV nameserver
 * - Device: GSITemplateDevice
 * - servername: GSITemplate_DU.vmla017
 * - Properties:
 *   - Acquisition:
 *     - Version
 *     - ModuleStatus
 *     - Status
 *     - Acquisition (mux)
 *       - Voltage: generiert jede 2 Sekunden Zufallswerte fuer Beam Process 1 und 2
 *   - Setting:
 *     - Power
 *     - Init
 *     - Reset
 *     - Setting (mux)
 *       - Voltage: bestimmt die Acquisition-werte
 *
 * TODO:
 * - test SET requests
 */
TEST_CASE("CmwNameserver", "[Client]") {
    using namespace std::literals;
    std::string nameserverExample = R"""(rda3://9#Address:#string#18#tcp:%2F%2Fdal025:16134#ApplicationId:#string#114#app=DigitizerDU2;uid=root;host=dal025;pid=16912;os=Linux%2D3%2E10%2E101%2Drt111%2Dscu03;osArch=64bit;appArch=64bit;lang=C%2B%2B;#Language:#string#3#C%2B%2B#Name:#string#19#DigitizerDU2%2Edal025#Pid:#int#16912#ProcessName:#string#12#DigitizerDU2#StartTime:#long#1699343695922#UserName:#string#4#root#Version:#string#5#3%2E1%2E0)""";
    std::string nameserverExample2 = R"""(rda3://9#Address:#string#18#tcp:%2F%2Ffel0053:3717#ApplicationId:#string#115#app=DigitizerDU2;uid=root;host=fel0053;pid=31447;os=Linux%2D3%2E10%2E101%2Drt111%2Dscu03;osArch=64bit;appArch=64bit;lang=C%2B%2B;#Language:#string#3#C%2B%2B#Name:#string#20#DigitizerDU2%2Efel0053#Pid:#int#31447#ProcessName:#string#12#DigitizerDU2#StartTime:#long#1701529074225#UserName:#string#4#root#Version:#string#5#3%2E1%2E0)""";
    std::string nameserverReply = R"""({"resources":[{"name":"GECD001","server":{"name":"DigitizerDU2.dal018","location":{"domain":"RDA3","endpoint":"9#Address:#string#17#tcp:%2F%2Fdal018:6397#ApplicationId:#string#113#app=DigitizerDU2;uid=root;host=dal018;pid=1977;os=Linux%2D3%2E10%2E101%2Drt111%2Dscu03;osArch=64bit;appArch=64bit;lang=C%2B%2B;#Language:#string#3#C%2B%2B#Name:#string#19#DigitizerDU2%2Edal018#Pid:#int#1977#ProcessName:#string#12#DigitizerDU2#StartTime:#long#1769161872191#UserName:#string#4#root#Version:#string#5#3%2E1%2E0"}}}]})""";

    SECTION("ParseNameserverReply") {
        auto result = opencmw::client::cmwlight::DirectoryLightClient::parseNameserverReply(nameserverReply);
        REQUIRE(result.has_value());
        REQUIRE(result->second == R"""(tcp://dal018:6397)""");
    }

    SECTION("Query rda3 directory server/nameserver") {
        auto env_nameserver = std::getenv("CMW_NAMESERVER");
        if (env_nameserver == nullptr) {
            std::println("skipping BasicCmwLight example test as it relies on the availability of network infrastructure.");
            return; // skip test
        }
        std::string                    nameserver{ env_nameserver };
        const opencmw::zmq::Context                               zctx{};
        opencmw::client::cmwlight::DirectoryLightClient           nameserverClient{nameserver};
        std::optional<std::string> result = nameserverClient.lookup("GSITemplateDevice");
        REQUIRE(!result.has_value()); // check that the device is originally not in the nameserver's cache
        while (!(result = nameserverClient.lookup("GSITemplateDevice")).has_value()) {
            std::this_thread::sleep_for(1ms);
        }
        REQUIRE(result.value().starts_with("tcp://vmla"));
        // check that later lookups immediately return the cached result
        result = nameserverClient.lookup("GSITemplateDevice");
        REQUIRE(result.value().starts_with("tcp://vmla"));
    };
}

// small utility function that prints the content of a string in the classic hexedit way with address, hexadecimal and ascii representations
static std::string hexview(const std::string_view value, std::size_t bytesPerLine = 4) {
    std::string result;
    result.reserve(value.size() * 4);
    std::string alpha; // temporarily store the ascii representation
    alpha.reserve(8 * bytesPerLine);
    std::size_t i = 0;
    for (auto c : value) {
        if (i % (bytesPerLine * 8) == 0) {
            result.append(std::format("{0:#08x} - {0:04} | ", i)); // print address in hex and decimal
        }
        result.append(std::format("{:02x} ", c));
        alpha.append(std::format("{}", std::isprint(c) ? c : '.'));
        if ((i + 1) % 8 == 0) {
            result.append("   ");
            alpha.append(" ");
        }
        if ((i + 1) % (bytesPerLine * 8) == 0) {
            result.append(std::format("   {}\n", alpha));
            alpha.clear();
        }
        i++;
    }
    result.append(std::format("{:{}}   {}\n", "", 3 * (9 * bytesPerLine - alpha.size()), alpha));
    return result;
}

static bool waitFor(const std::atomic<int> &counter, const int expectedValue, const auto timeout) {
    const auto start = std::chrono::system_clock::now();
    while (counter.load() < expectedValue && std::chrono::system_clock::now() - start < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return counter.load() == expectedValue;
}
static bool waitFor(const std::atomic<int> &counter, const int expectedValue) {
    using namespace std::literals;
    return waitFor(counter, expectedValue, 1000s);
}

TEST_CASE("CmwLightClientGet", "[Client]") {
    using namespace opencmw;
    using namespace std::literals;
    const std::string DEVICE_NAME = "GSITemplateDevice";
    const std::string STATUS_PROPERTY = "/GSITemplateDevice/Status";
    const std::string ACQUISITION_PROPERTY = "/GSITemplateDevice/Acquisition";
    const std::string SELECTOR = "FAIR.SELECTOR.C=3:S=1:P=1:T=300";

    char * env_nameserver = std::getenv("CMW_NAMESERVER");
    if (env_nameserver == nullptr) {
        std::println("skipping BasicCmwLight example test as it relies on the availability of network infrastructure. Define CMW_NAMESERVER environment variable to run this test.");
        return; // skip test
    }
    std::string                    nameserver{ env_nameserver };
    const zmq::Context zctx{};
    std::vector<std::unique_ptr<client::ClientBase>> clients;
    auto cmwlightClient = std::make_unique<client::cmwlight::CmwLightClientCtx>(zctx, nameserver, 100ms, "testclient");
    std::string deviceHost{}; // "tcp://vmla017:36725" };
    if (char * env_cmw_host = std::getenv("CMW_DEVICE_HOST"); env_cmw_host != nullptr) {
        deviceHost = std::string{env_cmw_host};
        cmwlightClient->nameserverClient().addStaticLookup(DEVICE_NAME, std::string{env_cmw_host});
    } else {
        while (!deviceHost.empty()) {
            deviceHost = cmwlightClient->nameserverClient().lookup(DEVICE_NAME).value_or("");
        }
    }
    clients.emplace_back(std::move(cmwlightClient));
    client::ClientContext clientContext{ std::move(clients) };
    // send some requests
    {
        auto        endpoint = URI<>::factory(URI(deviceHost)).scheme("rda3tcp").path(STATUS_PROPERTY).build();
        std::atomic getReceived{ 0 };
        clientContext.get(endpoint, [&getReceived](const mdp::Message &message) {
            if (!message.error.empty()) {
                FAIL("get should have succeeded");
            } else {
                ++getReceived;
            }
        });
        REQUIRE(waitFor(getReceived, 1));
    }
    {
        auto        endpoint = URI<>::factory(URI(deviceHost)).scheme("rda3tcp").path(ACQUISITION_PROPERTY).addQueryParameter("ctx", SELECTOR).build();
        std::atomic getReceived{ 0 };
        clientContext.get(endpoint, [&getReceived](const mdp::Message &message) {
            if (!message.error.empty()) {
                FAIL("get should have succeeded");
            } else {
                ++getReceived;
            }
        });
        REQUIRE(waitFor(getReceived, 1));
    }
    {
        auto        endpoint = URI<>::factory(URI(deviceHost)).scheme("rda3tcp").path("/GSITemplateDevice/Setting").build();
        std::atomic getError{ 0 };
        clientContext.get(endpoint, [&getError](const mdp::Message &message) {
            if (!message.error.empty()) {
                REQUIRE(message.error.contains("Access point 'GSITemplateDevice/Setting' needs a selector"));
                ++getError;
            } else {
                FAIL("get should have failed");
            }
        });
        REQUIRE(waitFor(getError, 1));
    }
    { // SET Request

    }
    { // Unreachable device server

    }
    { // non-existent device property

    }
}

TEST_CASE("CmwLightClientSubscribe", "[Client]") {
    using namespace opencmw;
    using namespace std::literals;
    const std::string DEVICE_NAME = "GSITemplateDevice";
    const std::string STATUS_PROPERTY = "/GSITemplateDevice/Status";
    const std::string ACQUISITION_PROPERTY = "/GSITemplateDevice/Acquisition";
    const std::string SELECTOR = "FAIR.SELECTOR.P=1";

    char * env_nameserver = std::getenv("CMW_NAMESERVER");
    if (env_nameserver == nullptr) {
        std::println("skipping BasicCmwLight example test as it relies on the availability of network infrastructure. Define CMW_NAMESERVER environment variable to run this test.");
        return; // skip test
    }
    std::string                    nameserver{ env_nameserver };
    const zmq::Context zctx{};
    std::vector<std::unique_ptr<client::ClientBase>> clients;
    auto cmwlightClient = std::make_unique<client::cmwlight::CmwLightClientCtx>(zctx, nameserver, 100ms, "testclient");
    std::string deviceHost{}; // "tcp://vmla017:36725" };
    if (char * env_cmw_host = std::getenv("CMW_DEVICE_HOST"); env_cmw_host != nullptr) {
        deviceHost = std::string{env_cmw_host};
        cmwlightClient->nameserverClient().addStaticLookup(DEVICE_NAME, std::string{env_cmw_host});
    } else {
        while (!deviceHost.empty()) {
            deviceHost = cmwlightClient->nameserverClient().lookup(DEVICE_NAME).value_or("");
        }
    }
    clients.emplace_back(std::move(cmwlightClient));
    client::ClientContext clientContext{ std::move(clients) };
    // setup subscription
    { // Subscribe non-multiplexed
        std::atomic subscriptionUpdatesReceived{ 0 };
        auto subscriptionEndpoint = URI<>::factory(URI(deviceHost)).scheme("rda3tcp").path(STATUS_PROPERTY).build();
        clientContext.subscribe(subscriptionEndpoint, [&subscriptionUpdatesReceived](const mdp::Message &message) {
            if (!message.error.empty()) {
                FAIL("subscription should not notify exceptions");
            } else {
                IoBuffer buffer(message.data);
                majordomo::Empty empty{};
                auto deserialiserInfo = deserialise<CmwLight, ProtocolCheck::LENIENT>(buffer, empty); // deserialising into empty struct to get field information
                //std::println("Deserialised subscription reply:\n  fields: {}\n  fieldTypes: {}", deserialiserInfo.additionalFields | std::views::keys, deserialiserInfo.additionalFields | std::views::values);
                REQUIRE(deserialiserInfo.additionalFields.size() == 13);
                ++subscriptionUpdatesReceived;
            }
        });
        REQUIRE(waitFor(subscriptionUpdatesReceived, 2, 5s)); // property gets notified every 2 seconds
        // Check that subscription updates stop after unsubscribing
        clientContext.unsubscribe(subscriptionEndpoint);
        std::this_thread::sleep_for(200ms); // get a few subscription updates
        int subscriptionUpdatesAfterUnsubscribe = subscriptionUpdatesReceived;
        std::this_thread::sleep_for(5s); // get a few subscription updates
        REQUIRE(subscriptionUpdatesReceived == subscriptionUpdatesAfterUnsubscribe);
    }
    { // error case: subscribe to multiplexed without selector
        std::atomic subscriptionUpdateError{ 0 };
        auto subscriptionEndpoint = URI<>::factory(URI(deviceHost)).scheme("rda3tcp").path(ACQUISITION_PROPERTY).build();
        clientContext.subscribe(subscriptionEndpoint, [&subscriptionUpdateError](const mdp::Message &message) {
            if (!message.error.empty()) {
                ++subscriptionUpdateError;
                REQUIRE(message.error.contains("Access point 'GSITemplateDevice/Acquisition' needs a selector"));
            } else {
                FAIL("invalid subscription should not notify updates");
            }
        });
        REQUIRE(waitFor(subscriptionUpdateError, 1, 5s)); // property gets notified every 2 seconds
        // TODO: does this need unsubscribe or is the error enough? does it do auto retry?
    }
    { // subscribe to muliplexed property
        std::atomic subscriptionUpdatesReceived{ 0 };
        auto subscriptionEndpoint = URI<>::factory(URI(deviceHost)).scheme("rda3tcp").path(ACQUISITION_PROPERTY).addQueryParameter("ctx", SELECTOR).build();
        clientContext.subscribe(subscriptionEndpoint, [&subscriptionUpdatesReceived](const mdp::Message &message) {
            if (!message.error.empty()) {
                FAIL("subscription should not notify exceptions");
            } else {
                IoBuffer buffer(message.data);
                majordomo::Empty empty{};
                auto deserialiserInfo = deserialise<CmwLight, ProtocolCheck::LENIENT>(buffer, empty); // deserialising into empty struct to get field information
                //std::println("Deserialised subscription reply:\n  fields: {}\n  fieldTypes: {}", deserialiserInfo.additionalFields | std::views::keys, deserialiserInfo.additionalFields | std::views::values);
                REQUIRE(deserialiserInfo.additionalFields.size() == 12);
                ++subscriptionUpdatesReceived;
            }
        });
        REQUIRE(waitFor(subscriptionUpdatesReceived, 2, 5s)); // property gets notified every 2 seconds
        // Check that subscription updates stop after unsubscribing
        clientContext.unsubscribe(subscriptionEndpoint);
        std::this_thread::sleep_for(200ms); // get a few subscription updates
        int subscriptionUpdatesAfterUnsubscribe = subscriptionUpdatesReceived;
        std::this_thread::sleep_for(5s); // get a few subscription updates
        REQUIRE(subscriptionUpdatesReceived == subscriptionUpdatesAfterUnsubscribe);
    }
    // SECTION("ErrorCase unreachable device server") {}
    // SECTION("ErrorCase missing selector for multiplexed Property") {}

    // test filters?
    // filters2String = "acquisitionModeFilter=int:0&channelNameFilter=GS11MU2:Voltage_1@10Hz";
    // subscribe("r1", new URI("rda3", null, '/' + DEVICE + '/' + PROPERTY, "ctx=" + SELECTOR + "&" + filtersString, null), null);
}
