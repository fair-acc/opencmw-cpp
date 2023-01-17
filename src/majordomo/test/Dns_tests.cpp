#include <fmt/format.h>

#include <catch2/catch.hpp>
#include <charconv>
#include <cstdlib>
#include <majordomo/Broker.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/Dns.hpp>
#include <majordomo/MockClient.hpp>
#include <majordomo/Utils.hpp>
#include <majordomo/Worker.hpp>
#include <thread>

#include "helpers.hpp"

using namespace opencmw::majordomo;
using namespace opencmw::DNS;
// using namespace opencmw::client;
using namespace std::chrono_literals;
using opencmw::majordomo::Worker;
using URI = opencmw::URI<>;

TEST_CASE("Test dns", "DNS") {
    auto settings              = testSettings();
    settings.heartbeatInterval = std::chrono::seconds(1);

    const auto brokerAddress   = opencmw::URI<opencmw::STRICT>("mdp://127.0.0.1:22346");
    Broker     broker("testbroker", settings);
    REQUIRE(broker.bind(brokerAddress));
    Dns<"DnsService"> DnsWorker(broker);
    DnsWorker.registerDnsAddress(opencmw::URI<>("https://127.0.0.1:8080"));

    RunInThread dnsWorkerRun(DnsWorker);
    RunInThread brokerRun(broker);
    REQUIRE(waitUntilServiceAvailable(broker.context, "DnsService"));
    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));
    {
        using opencmw::majordomo::Command;
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("DnsService", static_tag);

        request.setBody("{ \"brokerName\": \"testbroker\" }", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "DnsService");
        REQUIRE(reply->body() == "{\n\"uris\": [\"https://127.0.0.1:8080/DnsService\"]\n}");
        // REQUIRE(reply->body().empty());
    }
    {
        using opencmw::majordomo::Command;
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("DnsService", static_tag);

        request.setBody("{ \"brokerName\": \"testbroker\", \"serviceName\": \"DnsService\" }", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "DnsService");
        REQUIRE(reply->body() == "{\n\"uris\": [\"https://127.0.0.1:8080/DnsService\"]\n}");
        // REQUIRE(reply->body().empty());
    }
}