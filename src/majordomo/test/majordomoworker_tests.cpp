#include "helpers.hpp"

#include <majordomo/Broker.hpp>
#include <majordomo/MajordomoWorker.hpp>
#include <majordomo/Settings.hpp>

#include <MIME.hpp>
#include <opencmw.hpp>
#include <TimingCtx.hpp>

#include <catch2/catch.hpp>
#include <fmt/format.h>
#include <refl.hpp>

#include <exception>
#include <unordered_map>

using opencmw::majordomo::Broker;
using opencmw::majordomo::BrokerMessage;
using opencmw::majordomo::Command;
using opencmw::majordomo::MajordomoWorker;
using opencmw::majordomo::MdpMessage;
using opencmw::majordomo::MessageFrame;
using opencmw::majordomo::Settings;

struct TestContext {
    opencmw::TimingCtx      ctx;
    opencmw::MIME::MimeType contentType = opencmw::MIME::UNKNOWN;
};

ENABLE_REFLECTION_FOR(TestContext, ctx, contentType)

struct AddressRequest {
    int id;
};

ENABLE_REFLECTION_FOR(AddressRequest, id)

struct AddressEntry {
    int         id;
    std::string name;
    std::string street;
    int         streetNumber;
    std::string postalCode;
    std::string city;
};

ENABLE_REFLECTION_FOR(AddressEntry, name, street, streetNumber, postalCode, city)

struct TestHandler {
    std::unordered_map<int, AddressEntry> _entries;

    TestHandler() {
        _entries.emplace(42, AddressEntry{ 42, "Santa Claus", "Elf Road", 123, "88888", "North Pole" });
    }

    AddressEntry handle(opencmw::majordomo::RequestContext &, const TestContext &, const AddressRequest &request, TestContext &) {
        const auto it = _entries.find(request.id);
        if (it == _entries.end()) {
            throw std::invalid_argument(fmt::format("Address entry with ID '{}' not found", request.id));
        }
        return it->second;
    }
};

constexpr auto static_tag = MessageFrame::static_bytes_tag{};

TEST_CASE("Simple MajordomoWorker test using raw messages", "[majordomo][majordomoworker][simple_plain_client") {
    Broker broker("TestBroker", testSettings());
    opencmw::query::registerTypes(TestContext(), broker);

    MajordomoWorker<TestContext, AddressRequest, AddressEntry, TestHandler> worker("addressbook", broker, TestHandler());

    RunInThread                                                             brokerRun(broker);
    RunInThread                                                             workerRun(worker);

    TestNode<MdpMessage>                                                    client(broker.context);
    TestNode<BrokerMessage>                                                 subClient(broker.context, ZMQ_SUB);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));
    REQUIRE(subClient.connect(opencmw::majordomo::INTERNAL_ADDRESS_PUBLISHER));
    REQUIRE(subClient.subscribe("/newAddress?ctx=FAIR.SELECTOR.C=1"));

    bool seenReply = false;
    while (!seenReply) {
        {
            auto request = MdpMessage::createClientMessage(Command::Get);
            request.setServiceName("addressbook", static_tag);
            request.setClientRequestId("1", static_tag);
            request.setTopic("/addresses?ctx=FAIR.SELECTOR.ALL;contentType=application/json", static_tag);
            request.setBody("{ \"id\": 42 }", static_tag);
            client.send(request);
        }

        const auto reply = client.readOne();
        if (reply.topic() == "mmi.service") {
            continue;
        }

        REQUIRE(reply.isValid());
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.serviceName() == "addressbook");
        REQUIRE(reply.clientRequestId() == "1");
        REQUIRE(reply.error() == "");
        REQUIRE(reply.topic() == "/addresses?contentType=application%2Fjson&ctx=FAIR.SELECTOR.ALL");
        REQUIRE(reply.body() == "\"AddressEntry\": {\n\"name\": \"Santa Claus\",\n\"street\": \"Elf Road\",\n\"streetNumber\": 123,\n\"postalCode\": \"88888\",\n\"city\": \"North Pole\",\n}");
        seenReply = true;
    }

    // request non-existing entry
    {
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("addressbook", static_tag);
        request.setClientRequestId("2", static_tag);
        request.setTopic("/addresses?ctx=FAIR.SELECTOR.ALL;contentType=application/json", static_tag);
        request.setBody("{ \"id\": 4711 }", static_tag);
        client.send(request);
    }

    {
        const auto reply = client.readOne();
        REQUIRE(reply.isValid());
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.clientRequestId() == "2");
        REQUIRE(reply.body().empty());
        REQUIRE(reply.error().find("Address entry with ID '4711' not found") != std::string::npos);
    }

    // send empty request
    {
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("addressbook", static_tag);
        request.setClientRequestId("3", static_tag);
        request.setTopic("/addresses?ctx=FAIR.SELECTOR.ALL;contentType=application/json", static_tag);
        request.setBody("", static_tag);
        client.send(request);
    }

    {
        const auto reply = client.readOne();
        REQUIRE(reply.isValid());
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.clientRequestId() == "3");
        REQUIRE(reply.body().empty());
        REQUIRE(!reply.error().empty());
    }

    // send request with invalid JSON
    {
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("addressbook", static_tag);
        request.setClientRequestId("4", static_tag);
        request.setTopic("/addresses?ctx=FAIR.SELECTOR.ALL;contentType=application/json", static_tag);
        request.setBody("{ \"id\": 42 ]", static_tag);
        client.send(request);
    }

    {
        const auto reply = client.readOne();
        REQUIRE(reply.isValid());
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.clientRequestId() == "4");
        REQUIRE(reply.body().empty());
        REQUIRE(!reply.error().empty());
    }

    {
        // send a notification that's not received by the client due to the non-matching context
        const auto entry = AddressEntry{
            .id           = 1,
            .name         = "Sandman",
            .street       = "Some Dune",
            .streetNumber = 123,
            .postalCode   = "88888",
            .city         = "Sahara"
        };
        REQUIRE(worker.notify("/newAddress", TestContext{ .ctx = opencmw::TimingCtx({}, 1, {}, {}), .contentType = opencmw::MIME::JSON }, entry));
    }

    {
        // send a notification that's received (context matches)
        const auto entry = AddressEntry{
            .id           = 1,
            .name         = "Easter Bunny",
            .street       = "Carrot Road",
            .streetNumber = 123,
            .postalCode   = "88888",
            .city         = "Easter Island"
        };
        REQUIRE(worker.notify("/newAddress", TestContext{ .ctx = opencmw::TimingCtx(1, {}, {}, {}), .contentType = opencmw::MIME::JSON }, entry));
    }

    {
        const auto notify = subClient.readOne();
        REQUIRE(notify.isValid());
        REQUIRE(notify.command() == Command::Final);
        REQUIRE(notify.sourceId() == "/newAddress?ctx=FAIR.SELECTOR.C=1");
        REQUIRE(notify.topic() == "/newAddress?contentType=application%2Fjson&ctx=FAIR.SELECTOR.C%3D1");
        REQUIRE(notify.error().empty());
        REQUIRE(notify.body() == "\"AddressEntry\": {\n\"name\": \"Easter Bunny\",\n\"street\": \"Carrot Road\",\n\"streetNumber\": 123,\n\"postalCode\": \"88888\",\n\"city\": \"Easter Island\",\n}");
    }
}
