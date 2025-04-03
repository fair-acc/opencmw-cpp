#include <majordomo/Broker.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/MockClient.hpp>
#include <majordomo/Worker.hpp>

#include <catch2/catch.hpp>
#include <fmt/format.h>

#include <charconv>
#include <cstdlib>
#include <optional>
#include <thread>

// Concepts and tests use common types
#include <concepts/majordomo/helpers.hpp>

using namespace opencmw;
using namespace opencmw::majordomo;
using namespace std::chrono_literals;

namespace {
mdp::Message createClientMessage(mdp::Command command) {
    mdp::Message message;
    message.protocolName = mdp::clientProtocol;
    message.command      = command;
    return message;
}

mdp::Message createWorkerMessage(mdp::Command command) {
    mdp::Message message;
    message.protocolName = mdp::workerProtocol;
    message.command      = command;
    return message;
}
} // namespace

TEST_CASE("Test mmi.dns", "[broker][mmi][mmi_dns]") {
    using opencmw::majordomo::Broker;
    using opencmw::mdp::Message;

    auto settings              = testSettings();
    settings.heartbeatInterval = std::chrono::seconds(1);

    const auto dnsAddress      = opencmw::URI<opencmw::STRICT>("mdp://127.0.0.1:22345");
    const auto brokerAddress   = opencmw::URI<opencmw::STRICT>("mdp://127.0.0.1:22346");
    Broker     dnsBroker("/dnsBroker", settings);
    REQUIRE(dnsBroker.bind(dnsAddress));
    settings.dnsAddress = dnsAddress.str();
    Broker broker("/testbroker", settings);
    REQUIRE(broker.bind(brokerAddress));

    // Add another address for DNS (REST interface)
    broker.registerDnsAddress(opencmw::URI<>("https://127.0.0.1:8080"));

    RunInThread dnsBrokerRun(dnsBroker);
    RunInThread brokerRun(broker);

    // register worker
    MessageNode worker(broker.context);
    REQUIRE(worker.connect(brokerAddress));

    {
        auto ready        = createWorkerMessage(mdp::Command::Ready);
        ready.serviceName = "/aDevice/aProperty";
        ready.data        = IoBuffer("API description");
        worker.send(std::move(ready));
    }

    REQUIRE(waitUntilServiceAvailable(broker.context, "/aDevice/aProperty"));

    // give brokers time to synchronize DNS entries
    std::this_thread::sleep_for(settings.dnsTimeout * 3);

    { // list everything from primary broker
        zmq::Context clientContext;
        MessageNode  client(clientContext);
        REQUIRE(client.connect(brokerAddress));

        auto request        = createClientMessage(mdp::Command::Set);
        request.serviceName = "/mmi.dns";
        request.data        = IoBuffer("Hello World!");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/mmi.dns");
        REQUIRE(reply->data.asString() == "[/testbroker: https://127.0.0.1:8080,https://127.0.0.1:8080/aDevice/aProperty,https://127.0.0.1:8080/mmi.dns,"
                                          "https://127.0.0.1:8080/mmi.echo,https://127.0.0.1:8080/mmi.openapi,https://127.0.0.1:8080/mmi.service,"
                                          "mdp://127.0.0.1:22346,mdp://127.0.0.1:22346/aDevice/aProperty,mdp://127.0.0.1:22346/mmi.dns,"
                                          "mdp://127.0.0.1:22346/mmi.echo,mdp://127.0.0.1:22346/mmi.openapi,mdp://127.0.0.1:22346/mmi.service]");
    }

    { // list everything from DNS broker
        zmq::Context clientContext;
        MessageNode  client(clientContext);
        REQUIRE(client.connect(dnsAddress));

        auto request        = createClientMessage(mdp::Command::Set);
        request.serviceName = "/mmi.dns";
        request.data        = IoBuffer("Hello World!");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/mmi.dns");
        REQUIRE(reply->data.asString() == "[/dnsBroker: mdp://127.0.0.1:22345,mdp://127.0.0.1:22345/mmi.dns,mdp://127.0.0.1:22345/mmi.echo,"
                                          "mdp://127.0.0.1:22345/mmi.openapi,mdp://127.0.0.1:22345/mmi.service],"
                                          "[/testbroker: https://127.0.0.1:8080,https://127.0.0.1:8080/aDevice/aProperty,https://127.0.0.1:8080/mmi.dns,"
                                          "https://127.0.0.1:8080/mmi.echo,https://127.0.0.1:8080/mmi.openapi,https://127.0.0.1:8080/mmi.service,"
                                          "mdp://127.0.0.1:22346,mdp://127.0.0.1:22346/aDevice/aProperty,mdp://127.0.0.1:22346/mmi.dns,"
                                          "mdp://127.0.0.1:22346/mmi.echo,mdp://127.0.0.1:22346/mmi.openapi,mdp://127.0.0.1:22346/mmi.service]");
    }

    { // query for specific services
        zmq::Context clientContext;
        MessageNode  client(clientContext);
        REQUIRE(client.connect(dnsAddress));

        auto request        = createClientMessage(mdp::Command::Set);
        request.serviceName = "/mmi.dns";

        // send query with some crazy whitespace
        request.data = IoBuffer(" /mmi.dns  , /aDevice/aProperty");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/mmi.dns");
        REQUIRE(reply->data.asString() == "[/mmi.dns: https://127.0.0.1:8080/mmi.dns,mdp://127.0.0.1:22345/mmi.dns,mdp://127.0.0.1:22346/mmi.dns],[/aDevice/aProperty: https://127.0.0.1:8080/aDevice/aProperty,mdp://127.0.0.1:22346/aDevice/aProperty]");
    }
}

TEST_CASE("Test mmi.service", "[broker][mmi][mmi_service]") {
    using opencmw::majordomo::Broker;

    Broker      broker("/testbroker", testSettings());
    RunInThread brokerRun(broker);

    MessageNode client(broker.context);
    REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    { // ask for not yet existing service
        auto request        = createClientMessage(mdp::Command::Get);
        request.serviceName = "/mmi.service";
        request.data        = IoBuffer("/a.service");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/mmi.service");
        REQUIRE(reply->data.asString() == "404");
    }

    // register worker as a.service
    MessageNode worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready        = createWorkerMessage(mdp::Command::Ready);
    ready.serviceName = "/a.service";
    ready.data        = IoBuffer("API description");
    ready.rbac        = IoBuffer("rbacToken");
    worker.send(std::move(ready));

    REQUIRE(waitUntilServiceAvailable(broker.context, "/a.service"));

    { // service now exists
        auto request        = createClientMessage(mdp::Command::Get);
        request.serviceName = "/mmi.service";
        request.data        = IoBuffer("/a.service");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/mmi.service");
        REQUIRE(reply->data.asString() == "200");
    }

    { // list services
        auto request        = createClientMessage(mdp::Command::Get);
        request.serviceName = "/mmi.service";
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/mmi.service");
        REQUIRE(reply->data.asString() == "/a.service,/mmi.dns,/mmi.echo,/mmi.openapi,/mmi.service");
    }
}

TEST_CASE("Test mmi.echo", "[broker][mmi][mmi_echo]") {
    using opencmw::majordomo::Broker;

    Broker      broker("/testbroker", testSettings());
    RunInThread brokerRun(broker);

    MessageNode client(broker.context);
    REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    auto request        = createClientMessage(mdp::Command::Get);
    request.serviceName = "/mmi.echo";
    request.data        = IoBuffer("Wie heisst der Buergermeister von Wesel");
    request.rbac        = IoBuffer("rbac");

    client.send(mdp::Message{ request });

    const auto reply = client.tryReadOne();

    REQUIRE(reply.has_value());
    REQUIRE(reply->protocolName == mdp::clientProtocol);
    REQUIRE(reply->command == mdp::Command::Get);
    REQUIRE(reply->serviceName == request.serviceName);
    REQUIRE(reply->clientRequestID.asString() == request.clientRequestID.asString());
    REQUIRE(reply->topic == request.topic);
    REQUIRE(reply->data.asString() == request.data.asString());
    REQUIRE(reply->error == request.error);
    REQUIRE(reply->rbac.asString() == request.rbac.asString());
}

TEST_CASE("Test mmi.openapi", "[broker][mmi][mmi_openapi]") {
    using opencmw::majordomo::Broker;

    Broker      broker("/testbroker", testSettings());
    RunInThread brokerRun(broker);

    MessageNode client(broker.context);
    REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    { // request API of not yet existing service
        auto request        = createClientMessage(mdp::Command::Get);
        request.serviceName = "/mmi.openapi";
        request.data        = IoBuffer("/a.service");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/mmi.openapi");
        REQUIRE(reply->data.asString() == "");
        REQUIRE(reply->error == "Requested invalid service '/a.service'");
    }

    // register worker as a.service
    MessageNode worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready        = createWorkerMessage(mdp::Command::Ready);
    ready.serviceName = "/a.service";
    ready.data        = IoBuffer("API description");
    ready.rbac        = IoBuffer("rbacToken");
    worker.send(std::move(ready));

    REQUIRE(waitUntilServiceAvailable(broker.context, "/a.service"));

    { // service now exists, API description is returned
        auto request        = createClientMessage(mdp::Command::Get);
        request.serviceName = "/mmi.openapi";
        request.data        = IoBuffer("/a.service");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/mmi.openapi");
        REQUIRE(reply->data.asString() == "API description");
        REQUIRE(reply->error == "");
    }
}

TEST_CASE("Request answered with unknown service", "[broker][unknown_service]") {
    using opencmw::majordomo::Broker;

    const auto address = URI<>("inproc://testrouter");

    Broker     broker("/testbroker", testSettings());

    REQUIRE(broker.bind(address, BindOption::Router));

    MessageNode client(broker.context);
    REQUIRE(client.connect(address));

    RunInThread brokerRun(broker);

    auto        request     = createClientMessage(mdp::Command::Get);
    request.serviceName     = "/no.service";
    request.clientRequestID = IoBuffer("1");
    request.topic           = mdp::Message::URI("/topic");
    request.rbac            = IoBuffer("rbacToken");
    client.send(std::move(request));

    const auto reply = client.tryReadOne();

    REQUIRE(reply.has_value());
    REQUIRE(reply->protocolName == mdp::clientProtocol);
    REQUIRE(reply->command == mdp::Command::Final);
    REQUIRE(reply->serviceName == "/no.service");
    REQUIRE(reply->clientRequestID.asString() == "1");
    REQUIRE(reply->topic.str() == "/mmi.service");
    REQUIRE(reply->data.empty());
    REQUIRE(reply->error == "unknown service (error 501): '/no.service'");
    REQUIRE(reply->rbac.asString() == "RBAC=ADMIN,abcdef12345");
}

TEST_CASE("Test toZeroMQEndpoint conversion", "[utils][toZeroMQEndpoint]") {
    REQUIRE(mdp::toZeroMQEndpoint(URI<>("mdp://127.0.0.1:12345")) == "tcp://127.0.0.1:12345");
    REQUIRE(mdp::toZeroMQEndpoint(URI<>("mds://127.0.0.1:12345")) == "tcp://127.0.0.1:12345");
    REQUIRE(mdp::toZeroMQEndpoint(URI<>("inproc://test")) == "inproc://test");
}

TEST_CASE("Test Topic class", "[mdp][topic]") {
    using mdp::Topic;
    using Params = Topic::Params;
    SECTION("Topic::fromString") {
        REQUIRE_THROWS_AS(Topic::fromString("/a/service?p1=foo", { { "p2", "foo" } }), std::invalid_argument);
        REQUIRE_THROWS_AS(Topic::fromString("/invalid%20service?p1=foo", {}), std::invalid_argument);
        REQUIRE_THROWS_AS(Topic::fromString("g!a@r#b$a%g^e", {}), std::invalid_argument);
        REQUIRE_THROWS_AS(Topic::fromString({}, {}), std::invalid_argument);
        REQUIRE_THROWS_AS(Topic::fromString({}, { { "p1", "foo" } }), std::invalid_argument);
        const auto t1 = Topic::fromString("/a/service?p1=foo", {});
        const auto t2 = Topic::fromString("/a/service", { { "p1", "foo" } });
        REQUIRE(t1 == t2);
        REQUIRE(t1.service() == "/a/service");
        REQUIRE(t1.params() == Params{ { "p1", "foo" } });
    }

    SECTION("serialization from/to ZMQ and MDP topic with params") {
        const auto t1 = Topic::fromString("/a/service?p1=foo&p2=bar", {});
        const auto t2 = Topic::fromString("/a/service", { { "p1", "foo" }, { "p2", "bar" } });
        const auto t3 = Topic::fromZmqTopic("/a/service?p1=foo&p2=bar#");
        const auto t4 = Topic::fromMdpTopic(URI<>("/a/service?p2=bar&p1=foo"));
        REQUIRE_THROWS_AS(Topic::fromMdpTopic(URI<>("")), std::invalid_argument);
        REQUIRE(t1 == t2);
        REQUIRE(t1 == t3);
        REQUIRE(t1 == t4);
        REQUIRE(t1.toZmqTopic() == "/a/service?p1=foo&p2=bar#");
        REQUIRE(t1.toZmqTopic() == t2.toZmqTopic());
        REQUIRE(t1.toZmqTopic() == t3.toZmqTopic());
        REQUIRE(t1.toZmqTopic() == t4.toZmqTopic());
        REQUIRE(t1.toMdpTopic().path() == t2.toMdpTopic().path());
        REQUIRE(t1.toMdpTopic().queryParamMap() == t2.toMdpTopic().queryParamMap());
    }

    SECTION("serialization from/to ZMQ and MDP topic without params") {
        const auto t1 = Topic::fromString("/a/service", {});
        const auto t2 = Topic::fromZmqTopic("/a/service#");
        const auto t3 = Topic::fromMdpTopic(URI<>("/a/service"));
        REQUIRE(t1 == t2);
        REQUIRE(t1 == t3);
        REQUIRE(t1.toZmqTopic() == "/a/service#");
        REQUIRE(t1.toZmqTopic() == t2.toZmqTopic());
        REQUIRE(t1.toZmqTopic() == t3.toZmqTopic());
        REQUIRE(t1.toMdpTopic().path().value_or("") == "/a/service");
        REQUIRE(t1.toMdpTopic().queryParamMap().empty());
    }

    SECTION("hash function") {
        const auto t1 = Topic::fromString("/a/service?p1=foo", {});
        const auto t2 = Topic::fromString("/a/service", { { "p1", "foo" } });
        REQUIRE(t1 == t2);
        REQUIRE(t1.hash() == t2.hash());
        const auto t3 = Topic::fromString("/a/service?p1=foo&p2=bar", {});
        const auto t4 = Topic::fromString("/a/service?p2=bar&p1=foo", {});
        const auto t5 = Topic::fromString("/a/service", { { "p1", "foo" }, { "p2", "bar" } });
        REQUIRE(t3 == t4);
        REQUIRE(t4 == t5);
        REQUIRE(t3.hash() == t4.hash());
        REQUIRE(t4.hash() == t5.hash());
        const auto t6 = Topic::fromString("/a/service?p1=", {});
        const auto t7 = Topic::fromString("/a/service", { { "p1", std::nullopt } });
        REQUIRE(t6 == t7);
        REQUIRE(t6.hash() == t7.hash());
    }
}

TEST_CASE("Bind broker to endpoints", "[broker][bind]") {
    // the tcp/mdp/mds test cases rely on the ports being free, use wildcards/search for free ports if this turns out to be a problem
    static const std::array testcases = {
        std::tuple{ URI<>("tcp://127.0.0.1:22345"), BindOption::Router, std::make_optional<URI<>>("mdp://127.0.0.1:22345") },
        std::tuple{ URI<>("mdp://127.0.0.1:22346"), BindOption::Router, std::make_optional<URI<>>("mdp://127.0.0.1:22346") },
        std::tuple{ URI<>("mdp://127.0.0.1:22347"), BindOption::DetectFromURI, std::make_optional<URI<>>("mdp://127.0.0.1:22347") },
        std::tuple{ URI<>("mdp://127.0.0.1:22348"), BindOption::Router, std::make_optional<URI<>>("mdp://127.0.0.1:22348") },
        std::tuple{ URI<>("mdp://127.0.0.1:22348"), BindOption::Router, std::optional<URI<>>{} }, // error, already bound
        std::tuple{ URI<>("mds://127.0.0.1:22349"), BindOption::DetectFromURI, std::make_optional<URI<>>("mds://127.0.0.1:22349") },
        std::tuple{ URI<>("tcp://127.0.0.1:22350"), BindOption::Pub, std::make_optional<URI<>>("mds://127.0.0.1:22350") },
        std::tuple{ URI<>("inproc://bindtest"), BindOption::Router, std::make_optional<URI<>>("inproc://bindtest") },
        std::tuple{ URI<>("inproc://bindtest_pub"), BindOption::Pub, std::make_optional<URI<>>("inproc://bindtest_pub") },
    };

    Broker broker("/testbroker", testSettings());

    for (const auto &testcase : testcases) {
        const auto endpoint = std::get<0>(testcase);
        const auto option   = std::get<1>(testcase);
        const auto expected = std::get<2>(testcase);
        REQUIRE(broker.bind(endpoint, option) == expected);
    }
}

TEST_CASE("One client/one worker roundtrip", "[broker][roundtrip]") {
    using opencmw::majordomo::Broker;

    Broker      broker("/testbroker", testSettings());

    MessageNode worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready        = createWorkerMessage(mdp::Command::Ready);
    ready.serviceName = "/a.service";
    ready.data        = IoBuffer("API description");
    ready.rbac        = IoBuffer("rbacToken");
    worker.send(std::move(ready));

    broker.processMessages();

    auto request            = createClientMessage(mdp::Command::Get);
    request.serviceName     = "/a.service";
    request.clientRequestID = IoBuffer("1");
    request.topic           = mdp::Message::URI("/a.service?what=topic");
    request.rbac            = IoBuffer("rbacToken");
    client.send(std::move(request));

    broker.processMessages();

    const auto requestAtWorker = worker.tryReadOne();
    REQUIRE(requestAtWorker.has_value());
    REQUIRE(requestAtWorker->protocolName == mdp::workerProtocol);
    REQUIRE(requestAtWorker->command == mdp::Command::Get);
    REQUIRE(!requestAtWorker->serviceName.empty()); // clientSourceID
    REQUIRE(requestAtWorker->clientRequestID.asString() == "1");
    REQUIRE(requestAtWorker->topic.str() == "/a.service?what=topic");
    REQUIRE(requestAtWorker->data.empty());
    REQUIRE(requestAtWorker->error.empty());
    REQUIRE(requestAtWorker->rbac.asString() == "rbacToken");

    auto replyFromWorker            = createWorkerMessage(mdp::Command::Final);
    replyFromWorker.serviceName     = requestAtWorker->serviceName; // clientSourceID
    replyFromWorker.clientRequestID = IoBuffer("1");
    replyFromWorker.topic           = mdp::Message::URI("/a.service?what=topic");
    replyFromWorker.data            = IoBuffer("reply body");
    replyFromWorker.rbac            = IoBuffer("rbac_worker");
    worker.send(std::move(replyFromWorker));

    broker.processMessages();

    const auto reply = client.tryReadOne();
    REQUIRE(reply.has_value());
    REQUIRE(reply->protocolName == mdp::clientProtocol);
    REQUIRE(reply->command == mdp::Command::Final);
    REQUIRE(reply->serviceName == "/a.service");
    REQUIRE(reply->clientRequestID.asString() == "1");
    REQUIRE(reply->topic.str() == "/a.service?what=topic");
    REQUIRE(reply->data.asString() == "reply body");
    REQUIRE(reply->error.empty());
    REQUIRE(reply->rbac.asString() == "rbac_worker");

    broker.cleanup();

    {
        const auto heartbeat = worker.tryReadOne();
        REQUIRE(heartbeat.has_value());
        REQUIRE(heartbeat->protocolName == mdp::workerProtocol);
        REQUIRE(heartbeat->command == mdp::Command::Heartbeat);
        REQUIRE(heartbeat->serviceName == "/a.service");
        REQUIRE(heartbeat->rbac.asString() == "RBAC=ADMIN,abcdef12345");
    }

    const auto disconnect = worker.tryReadOne();
    REQUIRE(disconnect.has_value());
    REQUIRE(disconnect->protocolName == mdp::workerProtocol);
    REQUIRE(disconnect->command == mdp::Command::Disconnect);
    REQUIRE(disconnect->serviceName == "/a.service");
    REQUIRE(disconnect->clientRequestID.empty());
    REQUIRE(disconnect->data.asString() == "broker shutdown");
    REQUIRE(disconnect->error.empty());
    REQUIRE(disconnect->rbac.asString() == "RBAC=ADMIN,abcdef12345");
}

TEST_CASE("Test service matching", "[broker][name-matcher]") {
    using opencmw::majordomo::Broker;

    Broker      broker("/testbroker", testSettings());

    MessageNode worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready        = createWorkerMessage(mdp::Command::Ready);
    ready.serviceName = "/dashboard";
    ready.data        = IoBuffer("An example worker serving different dashboards");
    ready.rbac        = IoBuffer("rbacToken");
    worker.send(std::move(ready));

    broker.processMessages();

    {
        auto request            = createClientMessage(mdp::Command::Get);
        request.serviceName     = "/dashboard";
        request.clientRequestID = IoBuffer("1");
        request.topic           = mdp::Message::URI("/dashboard");
        request.rbac            = IoBuffer("rbacToken");
        client.send(std::move(request));

        broker.processMessages();

        const auto requestAtWorker = worker.tryReadOneSkipHB(3);
        REQUIRE(requestAtWorker.has_value());
        REQUIRE(requestAtWorker->protocolName == mdp::workerProtocol);
        REQUIRE(requestAtWorker->command == mdp::Command::Get);
        REQUIRE(!requestAtWorker->serviceName.empty()); // clientSourceID
        REQUIRE(requestAtWorker->clientRequestID.asString() == "1");
        REQUIRE(requestAtWorker->topic.str() == "/dashboard");
        REQUIRE(requestAtWorker->data.empty());
        REQUIRE(requestAtWorker->error.empty());
        REQUIRE(requestAtWorker->rbac.asString() == "rbacToken");

        auto replyFromWorker            = createWorkerMessage(mdp::Command::Final);
        replyFromWorker.serviceName     = requestAtWorker->serviceName; // clientSourceID
        replyFromWorker.clientRequestID = IoBuffer("1");
        replyFromWorker.topic           = mdp::Message::URI("/dashboard/default");
        replyFromWorker.data            = IoBuffer("Testreply");
        replyFromWorker.rbac            = IoBuffer("rbac_worker");
        worker.send(std::move(replyFromWorker));

        broker.processMessages();

        const auto reply = client.tryReadOneSkipHB(3);
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/dashboard");
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->topic.str() == "/dashboard/default");
        REQUIRE(reply->data.asString() == "Testreply");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker");
    }

    {
        auto request            = createClientMessage(mdp::Command::Get);
        request.serviceName     = "dashboard/main";
        request.clientRequestID = IoBuffer("2");
        request.topic           = mdp::Message::URI("/dashboard/main?revision=12");
        request.rbac            = IoBuffer("rbacToken");
        client.send(std::move(request));

        broker.processMessages();

        const auto requestAtWorker = worker.tryReadOneSkipHB(3);
        REQUIRE(requestAtWorker.has_value());
        REQUIRE(requestAtWorker->protocolName == mdp::workerProtocol);
        REQUIRE(requestAtWorker->command == mdp::Command::Get);
        REQUIRE(!requestAtWorker->serviceName.empty()); // clientSourceID
        REQUIRE(requestAtWorker->clientRequestID.asString() == "2");
        REQUIRE(requestAtWorker->topic.str() == "/dashboard/main?revision=12");
        REQUIRE(requestAtWorker->data.empty());
        REQUIRE(requestAtWorker->error.empty());
        REQUIRE(requestAtWorker->rbac.asString() == "rbacToken");

        auto replyFromWorker            = createWorkerMessage(mdp::Command::Final);
        replyFromWorker.serviceName     = requestAtWorker->serviceName; // clientSourceID
        replyFromWorker.clientRequestID = IoBuffer("2");
        replyFromWorker.topic           = mdp::Message::URI("/dashboard/main?revision=12");
        replyFromWorker.data            = IoBuffer("Testreply");
        replyFromWorker.rbac            = IoBuffer("rbac_worker");
        worker.send(std::move(replyFromWorker));

        broker.processMessages();

        const auto reply = client.tryReadOneSkipHB(3);
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/dashboard");
        REQUIRE(reply->clientRequestID.asString() == "2");
        REQUIRE(reply->topic.str() == "/dashboard/main?revision=12");
        REQUIRE(reply->data.asString() == "Testreply");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker");
    }

    broker.cleanup();

    // verify that the broker shuts the worker down correctly
    const auto disconnect = worker.tryReadOneSkipHB(3);
    REQUIRE(disconnect.has_value());
    REQUIRE(disconnect->protocolName == mdp::workerProtocol);
    REQUIRE(disconnect->command == mdp::Command::Disconnect);
    REQUIRE(disconnect->serviceName == "/dashboard");
    REQUIRE(disconnect->clientRequestID.empty());
    REQUIRE(disconnect->data.asString() == "broker shutdown");
    REQUIRE(disconnect->error.empty());
    REQUIRE(disconnect->rbac.asString() == "RBAC=ADMIN,abcdef12345");
}

TEST_CASE("Pubsub example using SUB client/DEALER worker", "[broker][pubsub_sub_dealer]") {
    using opencmw::majordomo::BindOption;
    using opencmw::majordomo::Broker;

    const auto publisherAddress = URI<>("inproc://testpub");

    Broker     broker("/testbroker", testSettings());
    broker.addFilter<DomainFilter<std::string_view>>("topic");

    REQUIRE(broker.bind(publisherAddress, BindOption::Pub));

    BrokerMessageNode subscriber(broker.context, ZMQ_SUB);
    REQUIRE(subscriber.connect(publisherAddress, "/a.service?topic=something#"));
    REQUIRE(subscriber.subscribe("/a.service?topic=other#"));
    REQUIRE(subscriber.subscribe("/a-service?topic=something#")); // invalid, broker must ignore

    broker.processMessages();

    MessageNode publisher(broker.context); // "/a.service"
    REQUIRE(publisher.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode publisherInvalid(broker.context); // "/a-service" (invalid)
    REQUIRE(publisherInvalid.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // send invalid notification, broker must ignore
    {
        auto notify        = createWorkerMessage(mdp::Command::Notify);
        notify.serviceName = "/a-service"; // invalid service name here and in topic
        notify.topic       = mdp::Message::URI("/a-service?topic=something");
        notify.data        = IoBuffer("Notification about something");
        notify.rbac        = IoBuffer("rbac_worker");
        publisherInvalid.send(std::move(notify));
    }

    broker.processMessages();

    // notification matching subscription
    {
        auto notify        = createWorkerMessage(mdp::Command::Notify);
        notify.serviceName = "/a.service";
        notify.topic       = mdp::Message::URI("/a.service?topic=something");
        notify.data        = IoBuffer("Notification about something");
        notify.rbac        = IoBuffer("rbac_worker");
        publisher.send(std::move(notify));
    }

    broker.processMessages();

    // notification not matching subscription
    {
        auto notify        = createWorkerMessage(mdp::Command::Notify);
        notify.serviceName = "/a.service";
        notify.topic       = mdp::Message::URI("/a.service?topic=somethingelse");
        notify.data        = IoBuffer("Notification about somethingelse");
        notify.rbac        = IoBuffer("rbac_worker");
        publisher.send(std::move(notify));
    }

    broker.processMessages();

    // notification matching subscription
    {
        auto notify        = createWorkerMessage(mdp::Command::Notify);
        notify.serviceName = "/a.service";
        notify.topic       = mdp::Message::URI("/a.service?topic=other");
        notify.data        = IoBuffer("Notification about other");
        notify.rbac        = IoBuffer("rbac_worker");
        publisher.send(std::move(notify));
    }

    broker.processMessages();

    // receive the two messages matching subscriptions
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/a.service?topic=something#");
        REQUIRE(reply->serviceName == "/a.service");
        REQUIRE(reply->topic.str() == "/a.service?topic=something");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->data.asString() == "Notification about something");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker");
    }

    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/a.service?topic=other#");
        REQUIRE(reply->serviceName == "/a.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->data.asString() == "Notification about other");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker");
    }

    REQUIRE(subscriber.unsubscribe("/a.service?topic=something#"));
    REQUIRE(subscriber.unsubscribe("/a%20service?topic=something#")); // invalid, broker must ignore

    broker.processMessages();

    // Still subscribed to "other"

    // not subscribed anymore
    {
        auto notify        = createWorkerMessage(mdp::Command::Notify);
        notify.serviceName = "/a.service";
        notify.topic       = mdp::Message::URI("/a.service?topic=something");
        notify.data        = IoBuffer("Notification about something");
        notify.rbac        = IoBuffer("rbac_worker");
        publisher.send(std::move(notify));
    }

    broker.processMessages();

    // notification still matching subscription
    {
        auto notify        = createWorkerMessage(mdp::Command::Notify);
        notify.serviceName = "/a.service";
        notify.topic       = mdp::Message::URI("/a.service?topic=other");
        notify.data        = IoBuffer("Notification about other");
        notify.rbac        = IoBuffer("rbac_worker");
        publisher.send(std::move(notify));
    }

    broker.processMessages();

    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/a.service?topic=other#");
        REQUIRE(reply->serviceName == "/a.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->data.asString() == "Notification about other");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker");
    }
}

TEST_CASE("Broker sends heartbeats", "[broker][heartbeat]") {
    using opencmw::majordomo::Broker;
    using Clock                      = std::chrono::steady_clock;

    constexpr auto heartbeatInterval = 50ms;

    Settings       settings;
    settings.heartbeatInterval = heartbeatInterval;
    settings.heartbeatLiveness = 3;
    Broker      broker("/testbroker", settings);

    MessageNode worker(broker.context);

    RunInThread brokerRun(broker);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    {
        auto ready        = createWorkerMessage(mdp::Command::Ready);
        ready.serviceName = "/heartbeat.service";
        ready.data        = IoBuffer("API description");
        ready.rbac        = IoBuffer("rbac_worker");
        worker.send(std::move(ready));
    }

    const auto afterReady = Clock::now();

    std::this_thread::sleep_for(heartbeatInterval * 0.75);

    {
        auto heartbeat        = createWorkerMessage(mdp::Command::Heartbeat);
        heartbeat.serviceName = "/heartbeat.service";
        heartbeat.rbac        = IoBuffer("rbac_worker");
        worker.send(std::move(heartbeat));
    }

    const auto heartbeat = worker.tryReadOne();
    REQUIRE(heartbeat.has_value());
    REQUIRE(heartbeat->command == mdp::Command::Heartbeat);

    const auto afterHeartbeat = Clock::now();

    // Ensure that the broker sends a heartbeat after a "reasonable time"
    // (which is a bit more than two hb intervals, if the broker goes into polling
    // (1 hb interval duration) shortly before heartbeats would be due; plus some slack
    // for other delays
    REQUIRE(afterHeartbeat - afterReady < heartbeatInterval * 2.3);

    // As the worker is sending no more heartbeats, ensure that the broker also stops sending them,
    // i.e. that it purged us (silently). We allow two more heartbeats (liveness - 1).

    if (const auto maybeHeartbeat = worker.tryReadOne(heartbeatInterval * 2)) {
        REQUIRE(maybeHeartbeat->command == mdp::Command::Heartbeat);

        if (const auto maybeHeartbeat2 = worker.tryReadOne(heartbeatInterval * 2)) {
            REQUIRE(maybeHeartbeat2->command == mdp::Command::Heartbeat);
        }
    }

    REQUIRE(!worker.tryReadOne(heartbeatInterval * 2).has_value());
}

TEST_CASE("Broker disconnects on unexpected heartbeat", "[broker][unexpected_heartbeat]") {
    using opencmw::majordomo::Broker;

    constexpr auto heartbeatInterval = 50ms;

    Settings       settings;
    settings.heartbeatInterval = heartbeatInterval;
    Broker      broker("/testbroker", settings);

    MessageNode worker(broker.context);

    RunInThread brokerRun(broker);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // send heartbeat without initial ready - invalid
    auto heartbeat        = createWorkerMessage(mdp::Command::Heartbeat);
    heartbeat.serviceName = "/heartbeat.service";
    heartbeat.rbac        = IoBuffer("rbac_worker");
    worker.send(std::move(heartbeat));

    const auto disconnect = worker.tryReadOne();
    REQUIRE(disconnect.has_value());
    REQUIRE(disconnect->command == mdp::Command::Disconnect);
}

TEST_CASE("Test RBAC role priority handling", "[broker][rbac]") {
    using Broker = opencmw::majordomo::Broker<ADMIN, Role<"BOSS", Permission::RW>, Role<"USER", Permission::RW>, ANY>;
    using opencmw::majordomo::MockClient;
    using namespace std::literals;

    // Use higher heartbeat interval so ther broker doesn't bother the worker with heartbeat messages
    opencmw::majordomo::Settings settings;
    settings.heartbeatInterval = std::chrono::seconds(1);

    Broker      broker("/testbroker", settings);
    RunInThread brokerRun(broker);

    MessageNode worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    {
        auto ready        = createWorkerMessage(mdp::Command::Ready);
        ready.serviceName = "/a.service";
        ready.data        = IoBuffer("API description");
        ready.rbac        = IoBuffer("rbac_worker");
        worker.send(std::move(ready));
    }

    REQUIRE(waitUntilServiceAvailable(broker.context, "/a.service"));

    MessageNode client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    constexpr auto roles           = std::array{ "ANY", "UNKNOWN", "USER", "BOSS", "ADMIN" };

    int            clientRequestId = 0;
    for (const auto &role : roles) {
        auto       msg      = createClientMessage(mdp::Command::Get);
        const auto reqId    = std::to_string(clientRequestId++);
        msg.clientRequestID = IoBuffer(reqId.data(), reqId.size());
        msg.serviceName     = "/a.service";
        const auto rbac     = fmt::format("RBAC={},123456abcdef", role);
        msg.rbac            = IoBuffer(rbac.data(), rbac.size());
        client.send(std::move(msg));
    }

    {
        // read first message but don't reply immediately, this forces the broker to queue the following requests
        const auto msg = worker.tryReadOne();
        REQUIRE(msg.has_value());
        REQUIRE(msg->clientRequestID.asString() == "0");

        // we give the broker time to read and queue the following requests
        std::this_thread::sleep_for(settings.heartbeatInterval * 0.7);

        auto reply            = createWorkerMessage(mdp::Command::Final);
        reply.serviceName     = msg->serviceName; // clientSourceID
        reply.clientRequestID = msg->clientRequestID;
        reply.data            = IoBuffer("Hello!");
        worker.send(std::move(reply));
    }

    // the remaining messages must have been queued in the broker and thus be reordered:
    // "ADMIN", "BOSS", "USER", "UNKNOWN" (unexpected roles come last)
    std::vector<std::string> seenMessages;
    while (seenMessages.size() < roles.size() - 1) {
        const auto msg = worker.tryReadOne();
        REQUIRE(msg.has_value());
        seenMessages.push_back(std::string(msg->clientRequestID.asString()));

        auto reply            = createWorkerMessage(mdp::Command::Final);
        reply.serviceName     = msg->serviceName; // clientSourceID
        reply.clientRequestID = msg->clientRequestID;
        reply.data            = IoBuffer("Hello!");
        worker.send(std::move(reply));
    }

    REQUIRE(seenMessages == std::vector{ "4"s, "3"s, "2"s, "1"s });
}

TEST_CASE("pubsub example using router socket (DEALER client)", "[broker][pubsub_router]") {
    using opencmw::majordomo::Broker;

    Broker broker("/testbroker", testSettings());
    broker.addFilter<DomainFilter<std::string_view>>("origin");
    broker.addFilter<DomainFilter<std::string_view>>("dish");

    MessageNode subscriber(broker.context);
    REQUIRE(subscriber.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode publisherOne(broker.context);
    REQUIRE(publisherOne.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode publisherTwo(broker.context);
    REQUIRE(publisherTwo.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // subscribe client to Italian
    {
        auto subscribe        = createClientMessage(mdp::Command::Subscribe);
        subscribe.serviceName = "/first.service";
        subscribe.topic       = mdp::Message::URI("/first.service?origin=italian");
        subscribe.rbac        = IoBuffer("rbacToken");
        subscriber.send(std::move(subscribe));
    }

    // Send invalid subscription (invalid service name)
    {
        auto subscribe        = createClientMessage(mdp::Command::Subscribe);
        subscribe.serviceName = "/s-e-r-v-i-c-e";
        subscribe.topic       = mdp::Message::URI("/se-r-v-i-c-e?origin=italian");
        subscribe.rbac        = IoBuffer("rbacToken");
        subscriber.send(std::move(subscribe));
    }

    broker.processMessages();

    // subscribe client to Indian
    {
        auto subscribe        = createClientMessage(mdp::Command::Subscribe);
        subscribe.serviceName = "/second.service";
        subscribe.topic       = mdp::Message::URI("/second.service?origin=indian");
        subscribe.rbac        = IoBuffer("rbacToken");
        subscriber.send(std::move(subscribe));
    }

    broker.processMessages();

    // publisher 1 sends a notification for Italian
    {
        auto pubMsg        = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "/first.service";
        pubMsg.topic       = mdp::Message::URI("/first.service?origin=italian");
        pubMsg.data        = IoBuffer("Original carbonara recipe here!");
        pubMsg.rbac        = IoBuffer("rbac_worker_1");
        publisherOne.send(std::move(pubMsg));
    }

    broker.processMessages();

    // client receives notification for Italian
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/first.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->topic == mdp::Message::URI("/first.service?origin=italian"));
        REQUIRE(reply->data.asString() == "Original carbonara recipe here!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_1");
    }

    // publisher 2 sends a notification for Italian
    {
        auto pubMsg        = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "/second.service";
        pubMsg.topic       = mdp::Message::URI("/second.service?origin=indian");
        pubMsg.data        = IoBuffer("Try our Chicken Korma!");
        pubMsg.rbac        = IoBuffer("rbac_worker_2");
        publisherTwo.send(std::move(pubMsg));
    }

    broker.processMessages();

    // client receives notification for Indian
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/second.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->topic == mdp::Message::URI("/second.service?origin=indian"));
        REQUIRE(reply->data.asString() == "Try our Chicken Korma!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_2");
    }

    // unsubscribe client from Italian
    {
        auto unsubscribe        = createClientMessage(mdp::Command::Unsubscribe);
        unsubscribe.serviceName = "/first.service";
        unsubscribe.topic       = mdp::Message::URI("/first.service?origin=italian");
        unsubscribe.rbac        = IoBuffer("rbacToken");
        subscriber.send(std::move(unsubscribe));
    }

    broker.processMessages();

    // publisher 1 sends a notification for Italian
    {
        auto pubMsg        = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "/first.service";
        pubMsg.topic       = mdp::Message::URI("/first.service?origin=italian");
        pubMsg.data        = IoBuffer("The best Margherita in town!");
        pubMsg.rbac        = IoBuffer("rbac_worker_1");
        publisherOne.send(std::move(pubMsg));
    }

    broker.processMessages();

    // publisher 2 sends a notification for Indian
    {
        auto pubMsg        = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "/second.service";
        pubMsg.topic       = mdp::Message::URI("/second.service?origin=indian");
        pubMsg.data        = IoBuffer("Sizzling tikkas in our Restaurant!");
        pubMsg.rbac        = IoBuffer("rbac_worker_2");
        publisherTwo.send(std::move(pubMsg));
    }

    broker.processMessages();

    // verify that the client receives only the notification from publisher 2

    {
        const auto reply = subscriber.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/second.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->topic == mdp::Message::URI("/second.service?origin=indian"));
        REQUIRE(reply->data.asString() == "Sizzling tikkas in our Restaurant!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_2");
    }
}

TEST_CASE("pubsub example using PUB socket (SUB client)", "[broker][pubsub_subclient]") {
    using opencmw::majordomo::Broker;

    Broker broker("/testbroker", testSettings());
    broker.addFilter<DomainFilter<std::string_view>>("origin");
    broker.addFilter<DomainFilter<std::string_view>>("dish");

    BrokerMessageNode subscriber(broker.context, ZMQ_SUB);
    REQUIRE(subscriber.connect(opencmw::majordomo::INTERNAL_ADDRESS_PUBLISHER));

    MessageNode publisherOne(broker.context);
    REQUIRE(publisherOne.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode publisherTwo(broker.context);
    REQUIRE(publisherTwo.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    subscriber.subscribe("/first.service?origin=italian#");

    broker.processMessages();

    subscriber.subscribe("/second.service?origin=indian#");

    subscriber.subscribe("/s-e-r-v-i-c-e"); // invalid service name

    broker.processMessages();

    // publisher 1 sends a notification that nobody receives (origin=chicago not matching)
    {
        auto pubMsg        = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "/first.service";
        pubMsg.topic       = mdp::Message::URI("/first.service?origin=chicago&dish=pizza");
        pubMsg.data        = IoBuffer("Deep dish");
        pubMsg.rbac        = IoBuffer("rbac_worker_1");
        publisherOne.send(std::move(pubMsg));
    }

    // publisher 1 sends a notification for Italian pasta
    {
        auto pubMsg        = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "/first.service";
        pubMsg.topic       = mdp::Message::URI("/first.service?origin=italian&dish=pasta");
        pubMsg.data        = IoBuffer("Original carbonara recipe here!");
        pubMsg.rbac        = IoBuffer("rbac_worker_1");
        publisherOne.send(std::move(pubMsg));
    }

    broker.processMessages();

    // client receives notification for Italian (not subscribed to pasta, but still a match)
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/first.service?origin=italian#");
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/first.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->topic == mdp::Message::URI("/first.service?origin=italian&dish=pasta"));
        REQUIRE(reply->data.asString() == "Original carbonara recipe here!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_1");
    }

    // publisher 2 sends a notification for Indian chicken
    {
        auto pubMsg        = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "/second.service";
        pubMsg.topic       = mdp::Message::URI("/second.service?origin=indian&dish=chicken");
        pubMsg.data        = IoBuffer("Try our Chicken Korma!");
        pubMsg.rbac        = IoBuffer("rbac_worker_2");
        publisherTwo.send(std::move(pubMsg));
    }

    broker.processMessages();

    // client receives notification for Indian
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/second.service?origin=indian#");
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/second.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->topic == mdp::Message::URI("/second.service?origin=indian&dish=chicken"));
        REQUIRE(reply->data.asString() == "Try our Chicken Korma!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_2");
    }

    subscriber.unsubscribe("/first.service?origin=italian#");

    broker.processMessages();

    // publisher 1 sends a notification for Italian pizza
    {
        auto pubMsg        = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "/first.service";
        pubMsg.topic       = mdp::Message::URI("/first.service?origin=italian&dish=pizza");
        pubMsg.data        = IoBuffer("The best Margherita in town!");
        pubMsg.rbac        = IoBuffer("rbac_worker_1");
        publisherOne.send(std::move(pubMsg));
    }

    broker.processMessages();

    // publisher 2 sends a notification for Indian tikkas
    {
        auto pubMsg        = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "/second.service";
        pubMsg.topic       = mdp::Message::URI("/second.service?origin=indian&dish=tikkas");
        pubMsg.data        = IoBuffer("Sizzling tikkas in our Restaurant!");
        pubMsg.rbac        = IoBuffer("rbac_worker_2");
        publisherTwo.send(std::move(pubMsg));
    }

    broker.processMessages();

    // verify that the client receives only the notification from publisher 2

    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/second.service?origin=indian#");
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/second.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->topic == mdp::Message::URI("/second.service?origin=indian&dish=tikkas"));
        REQUIRE(reply->data.asString() == "Sizzling tikkas in our Restaurant!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_2");
    }
}

TEST_CASE("BasicWorker connects to non-existing broker", "[worker]") {
    const zmq::Context        context;
    BasicWorker<"/a.service"> worker(URI<>("inproc:/doesnotexist"), TestIntHandler(10), context);
    worker.run(); // returns immediately on connection failure
}

TEST_CASE("BasicWorker run loop quits when broker quits", "[worker]") {
    const zmq::Context        context;
    Broker                    broker("/testbroker", testSettings());
    BasicWorker<"/a.service"> worker(broker, TestIntHandler(10));

    RunInThread               brokerRun(broker);

    auto                      quitBroker = std::jthread([&broker]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        broker.shutdown();
    });

    worker.run(); // returns when broker disappears
    quitBroker.join();
}

TEST_CASE("BasicWorker connection basics", "[worker][basic_worker_connection]") {
    const zmq::Context context;
    BrokerMessageNode  brokerRouter(context, ZMQ_ROUTER);
    BrokerMessageNode  brokerPub(context, ZMQ_PUB);
    const auto         brokerAddress = opencmw::URI<opencmw::STRICT>("inproc://test/");
    const auto         routerAddress = opencmw::URI<opencmw::STRICT>::factory(brokerAddress).path(opencmw::majordomo::SUFFIX_ROUTER).build();
    const auto         pubAddress    = opencmw::URI<opencmw::STRICT>::factory(brokerAddress).path(opencmw::majordomo::SUFFIX_SUBSCRIBE).build();
    REQUIRE(brokerRouter.bind(routerAddress));
    REQUIRE(brokerPub.bind(pubAddress));
    Settings settings;
    settings.heartbeatInterval       = std::chrono::milliseconds(200);
    settings.heartbeatLiveness       = 2;
    settings.workerReconnectInterval = std::chrono::milliseconds(200);

    BasicWorker<"/a.service"> worker(
            routerAddress, [](RequestContext &) {}, context, settings);
    RunInThread workerRun(worker);

    std::string workerId;

    // worker sends initial READY message
    {
        const auto ready = brokerRouter.tryReadOne();
        REQUIRE(ready.has_value());
        REQUIRE(ready->command == mdp::Command::Ready);
        REQUIRE(ready->serviceName == "/a.service");
        workerId = ready->sourceId;
    }

    // worker must send a heartbeat
    {
        const auto heartbeat = brokerRouter.tryReadOne(settings.heartbeatInterval * 23 / 10);
        REQUIRE(heartbeat.has_value());
        REQUIRE(heartbeat->command == mdp::Command::Heartbeat);
        REQUIRE(heartbeat->serviceName == "/a.service");
        REQUIRE(heartbeat->sourceId == workerId);
    }

    // not receiving heartbeats, the worker reconnects and sends a new READY message
    {
        const auto ready = brokerRouter.tryReadOne(settings.heartbeatInterval * (settings.heartbeatLiveness + 1) + settings.workerReconnectInterval);
        REQUIRE(ready.has_value());
        REQUIRE(ready->command == mdp::Command::Ready);
        REQUIRE(ready->serviceName == "/a.service");
        workerId = ready->sourceId;
    }

    // send heartbeat to worker
    {
        BrokerMessage heartbeat;
        heartbeat.protocolName = mdp::workerProtocol;
        heartbeat.command      = mdp::Command::Heartbeat;
        heartbeat.sourceId     = workerId;
        brokerRouter.send(std::move(heartbeat));
    }

    worker.shutdown();

    // worker sends a DISCONNECT on shutdown
    {
        const auto disconnect = brokerRouter.tryReadOne();
        REQUIRE(disconnect.has_value());
        REQUIRE(disconnect->command == mdp::Command::Disconnect);
        REQUIRE(disconnect->serviceName == "/a.service");
        REQUIRE(disconnect->sourceId == workerId);
    }
}

TEST_CASE("SET/GET example using the BasicWorker class", "[worker][getset_basic_worker]") {
    using opencmw::majordomo::Broker;

    Broker                                                                        broker("/testbroker", testSettings());

    BasicWorker<"/a.service", opencmw::majordomo::description<"API description">> worker(broker, TestIntHandler(10));
    REQUIRE(worker.serviceDescription() == "API description");

    MessageNode client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    // until the worker's READY is processed by the broker, it will return
    // an "unknown service" error, retry until we get the expected reply
    bool replyReceived = false;
    while (!replyReceived) {
        auto request            = createClientMessage(mdp::Command::Get);
        request.serviceName     = "/a.service";
        request.clientRequestID = IoBuffer("1");
        request.topic           = mdp::Message::URI("/topic");
        request.rbac            = IoBuffer("rbacToken");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->clientRequestID.asString() == "1");

        if (!reply->error.empty()) {
            REQUIRE(reply->error.find("error 501") != std::string_view::npos);
        } else {
            REQUIRE(reply->serviceName == "/a.service");
            REQUIRE(reply->topic.str() == "/topic");
            REQUIRE(reply->data.asString() == "10");
            REQUIRE(reply->error.empty());
            REQUIRE(reply->rbac.asString() == "rbacToken");
            replyReceived = true;
        }
    }

    {
        auto request            = createClientMessage(mdp::Command::Set);
        request.serviceName     = "/a.service";
        request.clientRequestID = IoBuffer("2");
        request.topic           = mdp::Message::URI("/topic");
        request.data            = IoBuffer("42");
        request.rbac            = IoBuffer("rbacToken");

        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->clientRequestID.asString() == "2");
        REQUIRE(reply->data.asString() == "Value set. All good!");
        REQUIRE(reply->error.empty());
    }

    {
        auto request            = createClientMessage(mdp::Command::Get);
        request.serviceName     = "/a.service";
        request.clientRequestID = IoBuffer("3");
        request.topic           = mdp::Message::URI("/topic");
        request.rbac            = IoBuffer("rbacToken");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->clientRequestID.asString() == "3");
        REQUIRE(reply->topic.str() == "/topic");
        REQUIRE(reply->data.asString() == "42");
        REQUIRE(reply->error.empty());
    }
}

TEST_CASE("BasicWorker SET/GET example with RBAC permission handling", "[worker][getset_basic_worker][rbac]") {
    using WRITER = Role<"WRITER", Permission::WO>;
    using READER = Role<"READER", Permission::RO>;
    using opencmw::majordomo::description;

    Broker                                                                          broker("/testbroker", testSettings());
    BasicWorker<"/a.service", description<"API description">, rbac<WRITER, READER>> worker(broker, TestIntHandler(10));
    REQUIRE(worker.serviceDescription() == "API description");

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    MessageNode writer(broker.context);
    REQUIRE(writer.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // writer is allowed to SET
    {
        auto set            = createClientMessage(mdp::Command::Set);
        set.serviceName     = "/a.service";
        set.clientRequestID = IoBuffer("1");
        set.topic           = mdp::Message::URI("/topic");
        set.data            = IoBuffer("42");
        set.rbac            = IoBuffer("RBAC=WRITER,1234");

        writer.send(std::move(set));

        const auto reply = writer.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->data.asString() == "Value set. All good!");
        REQUIRE(reply->error.empty());
    }

    // writer is not allowed to GET
    {
        auto get            = createClientMessage(mdp::Command::Get);
        get.serviceName     = "/a.service";
        get.clientRequestID = IoBuffer("2");
        get.topic           = mdp::Message::URI("/topic");
        get.rbac            = IoBuffer("RBAC=WRITER,1234");

        writer.send(std::move(get));

        const auto reply = writer.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->clientRequestID.asString() == "2");
        REQUIRE(reply->data.empty());
        REQUIRE(reply->error == "GET access denied to role 'WRITER'");
    }

    MessageNode reader(broker.context);
    REQUIRE(reader.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // reader is not allowed to SET
    {
        auto set            = createClientMessage(mdp::Command::Set);
        set.serviceName     = "/a.service";
        set.clientRequestID = IoBuffer("1");
        set.topic           = mdp::Message::URI("/topic");
        set.data            = IoBuffer("42");
        set.rbac            = IoBuffer("RBAC=READER,1234");

        reader.send(std::move(set));

        const auto reply = reader.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->data.empty());
        REQUIRE(reply->error == "SET access denied to role 'READER'");
    }

    // reader is allowed to GET
    {
        auto get            = createClientMessage(mdp::Command::Get);
        get.serviceName     = "/a.service";
        get.clientRequestID = IoBuffer("2");
        get.topic           = mdp::Message::URI("/topic");
        get.rbac            = IoBuffer("RBAC=READER,1234");

        reader.send(std::move(get));

        const auto reply = reader.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->clientRequestID.asString() == "2");
        REQUIRE(reply->data.asString() == "42");
        REQUIRE(reply->error.empty());
    }

    MessageNode admin(broker.context);
    REQUIRE(admin.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // admin is allowed to SET
    {
        auto set            = createClientMessage(mdp::Command::Set);
        set.serviceName     = "/a.service";
        set.clientRequestID = IoBuffer("1");
        set.topic           = mdp::Message::URI("/topic");
        set.data            = IoBuffer("42");
        set.rbac            = IoBuffer("RBAC=ADMIN,1234");

        admin.send(std::move(set));

        const auto reply = admin.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->data.asString() == "Value set. All good!");
        REQUIRE(reply->error.empty());
    }

    // admin is allowed to GET
    {
        auto get            = createClientMessage(mdp::Command::Get);
        get.serviceName     = "/a.service";
        get.clientRequestID = IoBuffer("2");
        get.topic           = mdp::Message::URI("/topic");
        get.rbac            = IoBuffer("RBAC=ADMIN,1234");

        admin.send(std::move(get));

        const auto reply = admin.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->clientRequestID.asString() == "2");
        REQUIRE(reply->data.asString() == "42");
        REQUIRE(reply->error.empty());
    }
}

TEST_CASE("NOTIFY example using the BasicWorker class", "[worker][notify_basic_worker]") {
    using opencmw::majordomo::Broker;
    using namespace std::literals;

    Broker                    broker("/testbroker", testSettings());

    BasicWorker<"/beverages"> worker(broker, TestIntHandler(10));
    broker.addFilter<DomainFilter<std::string_view>>("fridge");
    broker.addFilter<DomainFilter<std::string_view>>("iwant");
    broker.addFilter<DomainFilter<std::string_view>>("origin");

    BrokerMessageNode client(broker.context, ZMQ_XSUB);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_PUBLISHER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    // send some invalid subscribe/unsubscribe messages, must be ignored
    REQUIRE(client.sendRawFrame(""));
    REQUIRE(client.sendRawFrame("\x1"));
    REQUIRE(client.sendRawFrame("\x0"s));

    REQUIRE(client.sendRawFrame("\x1/beverages?iwant=wine#"));
    REQUIRE(client.sendRawFrame("\x1/beverages?iwant=beer#"));

    bool seenNotification = false;

    // we have a potential race here: the worker might not have processed the
    // subscribe yet and thus discard the notification. Send notifications
    // in a loop until one gets through.
    while (!seenNotification) {
        {
            mdp::Message notify;
            notify.topic = mdp::Message::URI("/beverages?iwant=beer");
            notify.data  = IoBuffer("Have a beer");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            const auto notification = client.tryReadOne(std::chrono::milliseconds(20));
            if (notification && notification->serviceName != "/mmi.service") {
                seenNotification = true;
                REQUIRE(notification->protocolName == mdp::clientProtocol);
                REQUIRE(notification->command == mdp::Command::Final);
                REQUIRE(notification->sourceId == "/beverages?iwant=beer#");
                REQUIRE(notification->topic == mdp::Message::URI("/beverages?iwant=beer"));
                REQUIRE(notification->data.asString() == "Have a beer");
            }
        }
    }

    {
        mdp::Message notify;
        notify.topic = mdp::Message::URI("/beverages?iwant=beer&fridge=empty");
        notify.error = "Fridge empty!";
        REQUIRE(worker.notify(std::move(notify)));
    }

    bool seenError = false;
    while (!seenError) {
        const auto notification = client.tryReadOne(std::chrono::milliseconds(20));
        if (!notification)
            continue;

        // there might be extra messages from above, ignore them
        if (notification->topic == mdp::Message::URI("/beverages?iwant=beer")) {
            continue;
        }

        REQUIRE(notification->protocolName == mdp::clientProtocol);
        REQUIRE(notification->command == mdp::Command::Final);
        REQUIRE(notification->sourceId == "/beverages?iwant=beer#");
        REQUIRE(notification->topic == mdp::Message::URI("/beverages?iwant=beer&fridge=empty"));
        REQUIRE(notification->error == "Fridge empty!");
        seenError = true;
    }

    {
        // as the subscribe for wine was sent before the beer one, this should be
        // race-free now (as know the beer subscribe was processed by everyone)
        mdp::Message notify;
        notify.topic = mdp::Message::URI("/beverages?iwant=wine&origin=italian");
        notify.data  = IoBuffer("Try our Chianti!");
        REQUIRE(worker.notify(std::move(notify)));
    }

    {
        const auto notification = client.tryReadOne();
        REQUIRE(notification.has_value());
        REQUIRE(notification->protocolName == mdp::clientProtocol);
        REQUIRE(notification->command == mdp::Command::Final);
        REQUIRE(notification->sourceId == "/beverages?iwant=wine#");
        REQUIRE(notification->topic == mdp::Message::URI("/beverages?iwant=wine&origin=italian"));
        REQUIRE(notification->data.asString() == "Try our Chianti!");
    }

    // unsubscribe from /beer*
    REQUIRE(client.sendRawFrame("\x0/beverages?iwant=beer#"s));

    // loop until we get two consecutive messages about wine, it means that the beer unsubscribe was processed
    while (true) {
        {
            mdp::Message notify;
            notify.topic = mdp::Message::URI("/beverages?iwant=wine&origin=portuguese");
            notify.data  = IoBuffer("New Vinho Verde arrived.");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            mdp::Message notify;
            notify.topic = mdp::Message::URI("/beverages?iwant=beer");
            notify.data  = IoBuffer("Get our pilsner now!");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            mdp::Message notify;
            notify.topic = mdp::Message::URI("/beverages?iwant=wine&origin=portuguese");
            notify.data  = IoBuffer("New Vinho Verde arrived.");
            REQUIRE(worker.notify(std::move(notify)));
        }

        const auto msg1 = client.tryReadOne();
        REQUIRE(msg1.has_value());
        REQUIRE(msg1->sourceId == "/beverages?iwant=wine#");

        const auto msg2 = client.tryReadOne();
        REQUIRE(msg2.has_value());
        if (msg2->sourceId == "/beverages?iwant=wine#") {
            break;
        }

        REQUIRE(msg2->sourceId == "/beverages?iwant=beer");

        const auto msg3 = client.tryReadOne();
        REQUIRE(msg3.has_value());
        REQUIRE(msg3->sourceId == "/beverages?iwant=wine");
    }
}

TEST_CASE("NOTIFY example using the BasicWorker class (via ROUTER socket)", "[worker][notify_basic_worker_router]") {
    using opencmw::majordomo::Broker;

    Broker broker("/testbroker", testSettings());
    broker.addFilter<DomainFilter<std::string_view>>("fridge");
    broker.addFilter<DomainFilter<std::string_view>>("iwant");
    broker.addFilter<DomainFilter<std::string_view>>("origin");

    BasicWorker<"/beverages"> worker(broker, TestIntHandler(10));

    MessageNode               client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    {
        auto subscribe        = createClientMessage(mdp::Command::Subscribe);
        subscribe.serviceName = "/beverages";
        subscribe.topic       = mdp::Message::URI("/beverages?iwant=wine");
        client.send(std::move(subscribe));
    }
    {
        auto subscribe        = createClientMessage(mdp::Command::Subscribe);
        subscribe.serviceName = "/beverages";
        subscribe.topic       = mdp::Message::URI("/beverages?iwant=beer");
        client.send(std::move(subscribe));
    }

    bool seenNotification = false;

    // we have a potential race here: the worker might not have processed the
    // subscribe yet and thus discarding the notification. Send notifications
    // in a loop until one gets through.
    while (!seenNotification) {
        {
            mdp::Message notify;
            notify.topic = mdp::Message::URI("/beverages?iwant=beer");
            notify.data  = IoBuffer("Have a beer");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            const auto notification = client.tryReadOne(std::chrono::milliseconds(20));
            if (notification && notification->serviceName != "/mmi.service") {
                seenNotification = true;
                REQUIRE(notification->protocolName == mdp::clientProtocol);
                REQUIRE(notification->command == mdp::Command::Final);
                REQUIRE(notification->topic == mdp::Message::URI("/beverages?iwant=beer"));
                REQUIRE(notification->data.asString() == "Have a beer");
            }
        }
    }

    {
        // as the subscribe for /wine was sent before the /beer one, this should be
        // race-free now (as know the /beer subscribe was processed by everyone)
        mdp::Message notify;
        notify.topic = mdp::Message::URI("/beverages?iwant=wine");
        notify.data  = IoBuffer("Try our Chianti!");
        REQUIRE(worker.notify(std::move(notify)));
    }

    {
        const auto notification = client.tryReadOne();
        REQUIRE(notification.has_value());
        REQUIRE(notification->protocolName == mdp::clientProtocol);
        REQUIRE(notification->command == mdp::Command::Final);
        REQUIRE(notification->topic == mdp::Message::URI("/beverages?iwant=wine"));
        REQUIRE(notification->data.asString() == "Try our Chianti!");
    }

    // unsubscribe from /beer
    {
        auto unsubscribe        = createClientMessage(mdp::Command::Unsubscribe);
        unsubscribe.serviceName = "/beverages";
        unsubscribe.topic       = mdp::Message::URI("/beverages?iwant=beer");
        client.send(std::move(unsubscribe));
    }

    // loop until we get two consecutive messages about wine, it means that the beer unsubscribe was processed
    while (true) {
        {
            mdp::Message notify;
            notify.topic = mdp::Message::URI("/beverages?iwant=wine");
            notify.data  = IoBuffer("New Vinho Verde arrived.");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            mdp::Message notify;
            notify.topic = mdp::Message::URI("/beverages?iwant=beer");
            notify.data  = IoBuffer("Get our pilsner now!");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            mdp::Message notify;
            notify.topic = mdp::Message::URI("/beverages?iwant=wine");
            notify.data  = IoBuffer("New Vinho Verde arrived.");
            REQUIRE(worker.notify(std::move(notify)));
        }

        const auto msg1 = client.tryReadOne();
        REQUIRE(msg1.has_value());
        REQUIRE(msg1->topic == mdp::Message::URI("/beverages?iwant=wine"));

        const auto msg2 = client.tryReadOne();
        REQUIRE(msg2.has_value());
        if (msg2->topic == mdp::Message::URI("/beverages?iwant=wine")) {
            break;
        }

        REQUIRE(msg2->topic == mdp::Message::URI("/beverages?iwant=beer"));
        const auto msg3 = client.tryReadOne();
        REQUIRE(msg3.has_value());
        REQUIRE(msg3->topic == mdp::Message::URI("/beverages?iwant=wine"));
    }
}

TEST_CASE("SET/GET example using a lambda as the worker's request handler", "[worker][lambda_handler]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MockClient;

    Broker broker("/testbroker", testSettings());

    auto   handleInt = [](RequestContext &requestContext) {
        static int value = 100;

        if (requestContext.request.command == mdp::Command::Get) {
            const auto data           = std::to_string(value);
            requestContext.reply.data = IoBuffer(data.data(), data.size());
            return;
        }

        assert(requestContext.request.command == mdp::Command::Set);

        const auto request     = requestContext.request.data.asString();
        int        parsedValue = 0;
        const auto result      = std::from_chars(request.begin(), request.end(), parsedValue);

        if (result.ec == std::errc::invalid_argument) {
            requestContext.reply.error = "Not a valid int";
        } else {
            value                     = parsedValue;
            requestContext.reply.data = IoBuffer("Value set. All good!");
        }
    };

    BasicWorker<"/a.service"> worker(broker, std::move(handleInt));

    MockClient                client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    client.get("/a.service", {}, [](auto &&message) {
        REQUIRE(message.error == "");
        REQUIRE(message.data.asString() == "100");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));

    client.set("/a.service", IoBuffer("42"), [](auto &&message) {
        REQUIRE(message.error == "");
        REQUIRE(message.data.asString() == "Value set. All good!");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));

    client.get("/a.service", {}, [](auto &&message) {
        REQUIRE(message.error == "");
        REQUIRE(message.data.asString() == "42");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));
}

TEST_CASE("Worker's request handler throws an exception", "[worker][handler_exception]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MockClient;

    Broker broker("/testbroker", testSettings());

    auto   handleRequest = [](RequestContext &) {
        throw std::runtime_error("Something went wrong!");
    };

    BasicWorker<"/a.service"> worker(broker, std::move(handleRequest));

    MockClient                client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    client.get("/a.service", {}, [](auto &&message) {
        REQUIRE(message.error == "Caught exception for service '/a.service'\nrequest message: \nexception: Something went wrong!");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));
}

TEST_CASE("Worker's request handler throws an unexpected exception", "[worker][handler_unexpected_exception]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MockClient;

    Broker broker("/testbroker", testSettings());

    auto   handleRequest = [](RequestContext &) {
        throw std::string("Something went wrong!");
    };

    BasicWorker<"/a.service"> worker(broker, std::move(handleRequest));

    MockClient                client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    client.get("/a.service", {}, [](auto &&message) {
        REQUIRE(message.error == "Caught unexpected exception for service '/a.service'\nrequest message: ");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));
}
