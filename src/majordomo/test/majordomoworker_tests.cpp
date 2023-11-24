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

// Concepts and tests use common types
#include <concepts/majordomo/helpers.hpp>

using opencmw::IoBuffer;
using opencmw::majordomo::Broker;
using opencmw::majordomo::BrokerMessage;
using opencmw::majordomo::Settings;
using opencmw::majordomo::Worker;
using opencmw::mdp::Topic;

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
 * In the following, somewhat nonsensical, example, the ContextType is TestContext, the InputType is AddressQueryRequest, and the OutputType is AddressEntry.
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
struct AddressQueryRequest {
    int id;
};

ENABLE_REFLECTION_FOR(AddressQueryRequest, id)

/**
 * The handler implementing the MajordomoHandler concept, here holding a single hardcoded address entry.
 */
struct TestHandler {
    std::unordered_map<int, AddressEntry> _entries;

    TestHandler() {
        _entries[42] = AddressEntry{ "Santa Claus", "Elf Road", 123, "88888", "North Pole", false };
    }

    /**
     * The handler function that the handler is required to implement.
     */
    void operator()(opencmw::majordomo::RequestContext & /*rawCtx*/, const TestContext & /*requestContext*/, const AddressQueryRequest &request, TestContext & /*replyContext*/, AddressEntry &output) {
        // we just use the request to look up the address, return it if found, or throw an exception if not.
        // MajordomoWorker/BasicMdpWorker translate any exception from the handler or the deserialisation into an error reply (message.data empty, error message in message.error)

        // the default replyContext is just a copy of the requstContext, we ignore them here.

        const auto it = _entries.find(request.id);
        if (it == _entries.end()) {
            throw std::invalid_argument(fmt::format("Address entry with ID '{}' not found", request.id));
        }
        output = it->second;
    }
};

namespace {
mdp::Message createClientMessage(mdp::Command command) {
    mdp::Message message;
    message.protocolName = mdp::clientProtocol;
    message.command      = command;
    return message;
}
} // namespace

TEST_CASE("Simple MajordomoWorker example showing its usage", "[majordomo][majordomoworker][simple_example]") {
    // We run both broker and worker inproc
    Broker broker("/TestBroker", testSettings());

    // For subscription matching, it is necessary that broker knows how to handle the query params "ctx" and "contentType".
    // ("ctx" needs to use the TimingCtxFilter, and "contentType" compare the mime types (currently simply a string comparison))
    // Here we register the members of TestContext as query params, with the member names being the keys, and using the member types
    // for correct matching.
    //
    // Note that the worker uses the same settings for matching, but as it knows about TestContext, it does this registration automatically.
    opencmw::query::registerTypes(TestContext(), broker);

    // Create MajordomoWorker with our domain objects, and our TestHandler.
    Worker<"/addressbook", TestContext, AddressQueryRequest, AddressEntry, opencmw::majordomo::description<"An Addressbook service">> worker(broker, TestHandler());

    // Run worker and broker in separate threads
    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    // The client used here is a simple test client, operating on raw messages.
    // Later, a client class analog to MajordomoWorker, sending AddressQueryRequest, and receiving AddressEntry, could be used.
    MessageNode client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    { // Make sure the API description is returned
        auto request        = createClientMessage(mdp::Command::Get);
        request.serviceName = "/mmi.openapi";
        request.data        = IoBuffer("/addressbook");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/mmi.openapi");
        REQUIRE(reply->data.asString() == "An Addressbook service");
        REQUIRE(reply->error == "");
    }

    {
        // Send a request for address with ID 42
        auto request            = createClientMessage(mdp::Command::Get);
        request.serviceName     = "/addressbook";
        request.clientRequestID = IoBuffer("1");
        request.topic           = mdp::Message::URI("/addressbook?ctx=FAIR.SELECTOR.ALL;contentType=application/json");
        request.data            = IoBuffer("{ \"id\": 42 }");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());

        // Assert that the correct reply is received, containing the serialised Address Entry return by TestHandler.
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/addressbook");
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->error == "");
        REQUIRE(reply->topic == mdp::Message::URI("/addressbook?contentType=application%2Fjson&ctx=FAIR.SELECTOR.ALL"));
        REQUIRE(reply->data.asString() == "{\n\"name\": \"Santa Claus\",\n\"street\": \"Elf Road\",\n\"streetNumber\": 123,\n\"postalCode\": \"88888\",\n\"city\": \"North Pole\",\n\"isCurrent\": false\n}");
    }
}

TEST_CASE("MajordomoWorker test using raw messages", "[majordomo][majordomoworker][plain_client][rbac]") {
    using namespace opencmw::majordomo;
    Broker<ADMIN, ANY> broker("/TestBroker", testSettings());
    opencmw::query::registerTypes(TestContext(), broker);

    Worker<"/addressbook", TestContext, AddressQueryRequest, AddressEntry, rbac<ADMIN, NONE>, description<"API description">> worker(broker, TestHandler());
    REQUIRE(worker.serviceDescription() == "API description");

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    MessageNode       client(broker.context);
    BrokerMessageNode subClient(broker.context, ZMQ_SUB);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));
    REQUIRE(subClient.connect(opencmw::majordomo::INTERNAL_ADDRESS_PUBLISHER));
    REQUIRE(subClient.subscribe("/addressbook?ctx=FAIR.SELECTOR.C%3D1"));

    {
        auto request            = createClientMessage(mdp::Command::Get);
        request.serviceName     = "/addressbook";
        request.clientRequestID = IoBuffer("1");
        request.topic           = mdp::Message::URI("/addressbook?ctx=FAIR.SELECTOR.ALL;contentType=application/json");
        request.data            = IoBuffer("{ \"id\": 42 }");
        request.rbac            = IoBuffer("RBAC=ADMIN,1234");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/addressbook");
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->error == "");
        REQUIRE(reply->topic == mdp::Message::URI("/addressbook?contentType=application%2Fjson&ctx=FAIR.SELECTOR.ALL"));
        REQUIRE(reply->data.asString() == "{\n\"name\": \"Santa Claus\",\n\"street\": \"Elf Road\",\n\"streetNumber\": 123,\n\"postalCode\": \"88888\",\n\"city\": \"North Pole\",\n\"isCurrent\": false\n}");
    }

    // GET with unknown role or empty role fails
    for (const auto &role : { "UNKNOWN", "" }) {
        auto request            = createClientMessage(mdp::Command::Get);
        request.serviceName     = "/addressbook";
        request.clientRequestID = IoBuffer("1");
        request.topic           = mdp::Message::URI("/addressbook?ctx=FAIR.SELECTOR.ALL;contentType=application/json");
        request.data            = IoBuffer("{ \"id\": 42 }");
        const auto body         = fmt::format("RBAC={},1234", role);
        request.rbac            = IoBuffer(body.data(), body.size());
        client.send(std::move(request));

        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/addressbook");
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->error == fmt::format("GET access denied to role '{}'", role));
        REQUIRE(reply->data.asString() == "");
    }

    // request non-existing entry
    {
        auto request            = createClientMessage(mdp::Command::Get);
        request.serviceName     = "/addressbook";
        request.clientRequestID = IoBuffer("2");
        request.topic           = mdp::Message::URI("/addressbook?ctx=FAIR.SELECTOR.ALL;contentType=application/json");
        request.data            = IoBuffer("{ \"id\": 4711 }");
        request.rbac            = IoBuffer("RBAC=ADMIN,1234");
        client.send(std::move(request));
    }

    {
        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->clientRequestID.asString() == "2");
        REQUIRE(reply->data.empty());
        REQUIRE(reply->error.find("Address entry with ID '4711' not found") != std::string::npos);
    }

    // send empty request
    {
        auto request            = createClientMessage(mdp::Command::Get);
        request.serviceName     = "/addressbook";
        request.clientRequestID = IoBuffer("3");
        request.topic           = mdp::Message::URI("/addressbook?ctx=FAIR.SELECTOR.ALL;contentType=application/json");
        request.data            = IoBuffer("");
        request.rbac            = IoBuffer("RBAC=ADMIN,1234");
        client.send(std::move(request));
    }

    {
        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->clientRequestID.asString() == "3");
        REQUIRE(reply->data.empty());
        REQUIRE(!reply->error.empty());
    }

    // send request with invalid JSON
    {
        auto request            = createClientMessage(mdp::Command::Get);
        request.serviceName     = "/addressbook";
        request.clientRequestID = IoBuffer("4");
        request.topic           = mdp::Message::URI("/addressbook?ctx=FAIR.SELECTOR.ALL;contentType=application/json");
        request.data            = IoBuffer("{ \"id\": 42 ]");
        request.rbac            = IoBuffer("RBAC=ADMIN,1234");
        client.send(std::move(request));
    }

    {
        const auto reply = client.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->clientRequestID.asString() == "4");
        REQUIRE(reply->data.empty());
        REQUIRE(!reply->error.empty());
    }

    {
        // send a notification that's not received by the client due to the non-matching context
        const auto entry = AddressEntry{
            .name         = "Sandman",
            .street       = "Some Dune",
            .streetNumber = 123,
            .postalCode   = "88888",
            .city         = "Sahara",
            .isCurrent    = true
        };
        REQUIRE(worker.notify(TestContext{ .ctx = opencmw::TimingCtx(-1, 1, -1, -1), .contentType = opencmw::MIME::JSON }, entry));
    }

    {
        // send a notification that's received (context matches)
        const auto entry = AddressEntry{
            .name         = "Easter Bunny",
            .street       = "Carrot Road",
            .streetNumber = 123,
            .postalCode   = "88888",
            .city         = "Easter Island",
            .isCurrent    = true
        };
        REQUIRE(worker.notify(TestContext{ .ctx = opencmw::TimingCtx(1), .contentType = opencmw::MIME::JSON }, entry));
        const auto activeSubscriptions = worker.activeSubscriptions();
        REQUIRE(activeSubscriptions.size() == 1);
        REQUIRE(activeSubscriptions.begin()->service() == "/addressbook");
        REQUIRE(activeSubscriptions.begin()->params() == Topic::Params{ { "ctx", "FAIR.SELECTOR.C=1" } });
    }

    {
        const auto notify = subClient.tryReadOne();
        REQUIRE(notify.has_value());
        REQUIRE(notify->command == mdp::Command::Final);
        REQUIRE(notify->sourceId == "/addressbook?ctx=FAIR.SELECTOR.C%3D1");
        REQUIRE(notify->topic == mdp::Message::URI("/addressbook?contentType=application%2Fjson&ctx=FAIR.SELECTOR.C%3D1"));
        REQUIRE(notify->error.empty());
        REQUIRE(notify->data.asString() == "{\n\"name\": \"Easter Bunny\",\n\"street\": \"Carrot Road\",\n\"streetNumber\": 123,\n\"postalCode\": \"88888\",\n\"city\": \"Easter Island\",\n\"isCurrent\": true\n}");
    }
}
