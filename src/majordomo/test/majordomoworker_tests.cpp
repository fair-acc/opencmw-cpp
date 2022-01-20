#include "helpers.hpp"

#include <majordomo/Broker.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>

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
using opencmw::majordomo::MdpMessage;
using opencmw::majordomo::MessageFrame;
using opencmw::majordomo::Settings;
using opencmw::majordomo::Worker;

/*
 * This test serves as example on how MajordomoWorker is to be used.
 *
 * MajordomoWorker takes care of deserialising the received request into a domain object,
 * passes it to a user-defined handler, and serialises the handler's result back into a MDP
 * message.
 *
 * Its template arguments are:
 *  - ContextType: Type representing the query params of the topic URI, used with both request and reply.
 *  - InputType: The type of the deserialised request.
 *  - OutputType: The type of the reply returned from the handler.
 *
 * InputType and OutputType must be reflectable, with members supported by the YaS, CMWLight and JSON serialisers.
 * ContentType also must be reflectable, with members that can be serialised to a URI query (see QuerySerialiser.hpp; more types might have to be added)
 *
 * The handler must have a "handle" function taking the raw RequestContext, the input and output context objects (of type ContextType), and the input and output objects,
 * where the raw context and the output context and output object can be modified by the handler.
 *
 * In the following, somewhat nonsensical, example, the ContextType is TestContext, the InputType is AddressRequest, and the OutputType is AddressEntry.
 */

/**
 * Dummy context testing the query serialisation (which is used for mimetype detection, and subscription matching).
 * Other types supported would be std::string, int, bool.
 *
 * If MajordomoWorker finds a MimeType member, it is used for deserialising the request and serialising the result.
 * If there's no such member, a binary format (YaS) will be used. If there's more than one, the first one found is used.
 */
struct TestContext {
    opencmw::TimingCtx      ctx;                                  ///< doesn't make much sense here, but it is a good opportunity to test this class
    opencmw::MIME::MimeType contentType = opencmw::MIME::UNKNOWN; ///< Used for de-/serialisation
};

// Mandatory: Enables reflection, all members should be listed
ENABLE_REFLECTION_FOR(TestContext, ctx, contentType)

/**
 * Request type, here simply taking the ID of the object that is wanted
 */
struct AddressRequest {
    int id;
};

ENABLE_REFLECTION_FOR(AddressRequest, id)

/**
 * Reply type/
 *
 * This is a simplistic example, it could also be a nested structure with reflectable members, or contain vectors, etc.
 */
struct AddressEntry {
    int         id;
    std::string name;
    std::string street;
    int         streetNumber;
    std::string postalCode;
    std::string city;
};

ENABLE_REFLECTION_FOR(AddressEntry, name, street, streetNumber, postalCode, city)

/**
 * The handler implementing the MajordomoHandler concept, here holding a single hardcoded address entry.
 */
struct TestHandler {
    std::unordered_map<int, AddressEntry> _entries;

    TestHandler() {
        _entries.emplace(42, AddressEntry{ 42, "Santa Claus", "Elf Road", 123, "88888", "North Pole" });
    }

    /**
     * The handler function that the handler is required to implement.
     */
    void operator()(opencmw::majordomo::RequestContext & /*rawCtx*/, const TestContext & /*requestContext*/, const AddressRequest &request, TestContext & /*replyContext*/, AddressEntry &output) {
        // we just use the request to look up the address, return it if found, or throw an exception if not.
        // MajordomoWorker/BasicMdpWorker translate any exception from the handler or the deserialisation into an error reply (message.body() empty, error message in message.error())

        // the default replyContext is just a copy of the requstContext, we ignore them here.

        const auto it = _entries.find(request.id);
        if (it == _entries.end()) {
            throw std::invalid_argument(fmt::format("Address entry with ID '{}' not found", request.id));
        }
        output = it->second;
    }
};

constexpr auto static_tag = MessageFrame::static_bytes_tag{};

TEST_CASE("Simple MajordomoWorker example showing its usage", "[majordomo][majordomoworker][simple_example]") {
    // We run both broker and worker inproc
    Broker broker("TestBroker", testSettings());

    // For subscription matching, it is necessary that broker knows how to handle the query params "ctx" and "contentType".
    // ("ctx" needs to use the TimingCtxFilter, and "contentType" compare the mime types (currently simply a string comparison))
    // Here we register the members of TestContext as query params, with the member names being the keys, and using the member types
    // for correct matching.
    //
    // Note that the worker uses the same settings for matching, but as it knows about TestContext, it does this registration automatically.
    opencmw::query::registerTypes(TestContext(), broker);

    // Create MajordomoWorker with our domain objects, and our TestHandler.
    Worker<"addressbook", TestContext, AddressRequest, AddressEntry> worker(broker, TestHandler());

    // Run worker and broker in separate threads
    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "addressbook"));

    // The client used here is a simple test client, operating on raw messages.
    // Later, a client class analog to MajordomoWorker, sending AddressRequest, and receiving AddressEntry, could be used.
    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    {
        // Send a request for address with ID 42
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("addressbook", static_tag);
        request.setClientRequestId("1", static_tag);
        request.setTopic("/addresses?ctx=FAIR.SELECTOR.ALL;contentType=application/json", static_tag);
        request.setBody("{ \"id\": 42 }", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());

        // Assert that the correct reply is received, containing the serialised Address Entry return by TestHandler.
        REQUIRE(reply->isValid());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "addressbook");
        REQUIRE(reply->clientRequestId() == "1");
        REQUIRE(reply->error() == "");
        REQUIRE(reply->topic() == "/addresses?contentType=application%2Fjson&ctx=FAIR.SELECTOR.ALL");
        REQUIRE(reply->body() == "\"AddressEntry\": {\n\"name\": \"Santa Claus\",\n\"street\": \"Elf Road\",\n\"streetNumber\": 123,\n\"postalCode\": \"88888\",\n\"city\": \"North Pole\",\n}");
    }
}

TEST_CASE("MajordomoWorker test using raw messages", "[majordomo][majordomoworker][plain_client][rbac]") {
    using namespace opencmw::majordomo;
    Broker<rbac::ADMIN, rbac::ANY> broker("TestBroker", testSettings());
    opencmw::query::registerTypes(TestContext(), broker);

    // custom ANY without any permissions
    using ANY = rbac::Role<"ANY", rbac::Permission::NONE>;

    Worker<"addressbook", TestContext, AddressRequest, AddressEntry, rbac::ADMIN, description<"API description">, ANY> worker(broker, TestHandler());
    REQUIRE(worker.serviceDescription() == "API description");

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "addressbook"));

    TestNode<MdpMessage>    client(broker.context);
    TestNode<BrokerMessage> subClient(broker.context, ZMQ_SUB);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));
    REQUIRE(subClient.connect(opencmw::majordomo::INTERNAL_ADDRESS_PUBLISHER));
    REQUIRE(subClient.subscribe("/newAddress?ctx=FAIR.SELECTOR.C=1"));

    {
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("addressbook", static_tag);
        request.setClientRequestId("1", static_tag);
        request.setTopic("/addresses?ctx=FAIR.SELECTOR.ALL;contentType=application/json", static_tag);
        request.setBody("{ \"id\": 42 }", static_tag);
        request.setRbacToken("RBAC=ADMIN,1234", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "addressbook");
        REQUIRE(reply->clientRequestId() == "1");
        REQUIRE(reply->error() == "");
        REQUIRE(reply->topic() == "/addresses?contentType=application%2Fjson&ctx=FAIR.SELECTOR.ALL");
        REQUIRE(reply->body() == "\"AddressEntry\": {\n\"name\": \"Santa Claus\",\n\"street\": \"Elf Road\",\n\"streetNumber\": 123,\n\"postalCode\": \"88888\",\n\"city\": \"North Pole\",\n}");
    }

    { // GET with unknown role fails
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("addressbook", static_tag);
        request.setClientRequestId("1", static_tag);
        request.setTopic("/addresses?ctx=FAIR.SELECTOR.ALL;contentType=application/json", static_tag);
        request.setBody("{ \"id\": 42 }", static_tag);
        request.setRbacToken("RBAC=NOBODY,1234", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "addressbook");
        REQUIRE(reply->clientRequestId() == "1");
        REQUIRE(reply->error() == "GET access denied to role 'NOBODY'");
        REQUIRE(reply->body() == "");
    }

    // request non-existing entry
    {
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("addressbook", static_tag);
        request.setClientRequestId("2", static_tag);
        request.setTopic("/addresses?ctx=FAIR.SELECTOR.ALL;contentType=application/json", static_tag);
        request.setBody("{ \"id\": 4711 }", static_tag);
        request.setRbacToken("RBAC=ADMIN,1234", static_tag);
        client.send(request);
    }

    {
        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->clientRequestId() == "2");
        REQUIRE(reply->body().empty());
        REQUIRE(reply->error().find("Address entry with ID '4711' not found") != std::string::npos);
    }

    // send empty request
    {
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("addressbook", static_tag);
        request.setClientRequestId("3", static_tag);
        request.setTopic("/addresses?ctx=FAIR.SELECTOR.ALL;contentType=application/json", static_tag);
        request.setBody("", static_tag);
        request.setRbacToken("RBAC=ADMIN,1234", static_tag);
        client.send(request);
    }

    {
        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->clientRequestId() == "3");
        REQUIRE(reply->body().empty());
        REQUIRE(!reply->error().empty());
    }

    // send request with invalid JSON
    {
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("addressbook", static_tag);
        request.setClientRequestId("4", static_tag);
        request.setTopic("/addresses?ctx=FAIR.SELECTOR.ALL;contentType=application/json", static_tag);
        request.setBody("{ \"id\": 42 ]", static_tag);
        request.setRbacToken("RBAC=ADMIN,1234", static_tag);
        client.send(request);
    }

    {
        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->clientRequestId() == "4");
        REQUIRE(reply->body().empty());
        REQUIRE(!reply->error().empty());
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
        const auto notify = subClient.tryReadOne();
        REQUIRE(notify.has_value());
        REQUIRE(notify->isValid());
        REQUIRE(notify->command() == Command::Final);
        REQUIRE(notify->sourceId() == "/newAddress?ctx=FAIR.SELECTOR.C=1");
        REQUIRE(notify->topic() == "/newAddress?contentType=application%2Fjson&ctx=FAIR.SELECTOR.C%3D1");
        REQUIRE(notify->error().empty());
        REQUIRE(notify->body() == "\"AddressEntry\": {\n\"name\": \"Easter Bunny\",\n\"street\": \"Carrot Road\",\n\"streetNumber\": 123,\n\"postalCode\": \"88888\",\n\"city\": \"Easter Island\",\n}");
    }
}
