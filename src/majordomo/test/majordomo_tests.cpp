#include <majordomo/Broker.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/MockClient.hpp>
#include <majordomo/Worker.hpp>

#include <catch2/catch.hpp>
#include <fmt/format.h>

#include <charconv>
#include <cstdlib>
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
        message.command = command;
        return message;
    }

    mdp::Message createWorkerMessage(mdp::Command command) {
        mdp::Message message;
        message.protocolName = mdp::workerProtocol;
        message.command = command;
        return message;
    }
}

TEST_CASE("Test mmi.dns", "[broker][mmi][mmi_dns]") {
    using opencmw::majordomo::Broker;
    using opencmw::mdp::Message;

    auto settings              = testSettings();
    settings.heartbeatInterval = std::chrono::seconds(1);

    const auto dnsAddress      = opencmw::URI<opencmw::STRICT>("mdp://127.0.0.1:22345");
    const auto brokerAddress   = opencmw::URI<opencmw::STRICT>("mdp://127.0.0.1:22346");
    Broker     dnsBroker("dnsBroker", settings);
    REQUIRE(dnsBroker.bind(dnsAddress));
    settings.dnsAddress = dnsAddress.str();
    Broker broker("testbroker", settings);
    REQUIRE(broker.bind(brokerAddress));

    // Add another address for DNS (REST interface)
    broker.registerDnsAddress(opencmw::URI<>("https://127.0.0.1:8080"));

    RunInThread dnsBrokerRun(dnsBroker);
    RunInThread brokerRun(broker);

    // register worker
    MessageNode worker(broker.context);
    REQUIRE(worker.connect(brokerAddress));

    {
        auto ready = createWorkerMessage(mdp::Command::Ready);
        ready.serviceName = "/aDevice/aProperty";
        ready.data = IoBuffer("API description");
        worker.send(std::move(ready));
    }

    REQUIRE(waitUntilServiceAvailable(broker.context, "/aDevice/aProperty"));

    // give brokers time to synchronize DNS entries
    std::this_thread::sleep_for(settings.dnsTimeout * 3);

    { // list everything from primary broker
        zmq::Context         clientContext;
        MessageNode client(clientContext);
        REQUIRE(client.connect(brokerAddress));

        auto request = createClientMessage(mdp::Command::Set);
        request.serviceName = "mmi.dns";
        request.data = IoBuffer("Hello World!");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "mmi.dns");
        REQUIRE(reply->data.asString() == "[testbroker: https://127.0.0.1:8080,https://127.0.0.1:8080/aDevice/aProperty,https://127.0.0.1:8080/mmi.dns,"
                                 "https://127.0.0.1:8080/mmi.echo,https://127.0.0.1:8080/mmi.openapi,https://127.0.0.1:8080/mmi.service,"
                                 "mdp://127.0.0.1:22346,mdp://127.0.0.1:22346/aDevice/aProperty,mdp://127.0.0.1:22346/mmi.dns,"
                                 "mdp://127.0.0.1:22346/mmi.echo,mdp://127.0.0.1:22346/mmi.openapi,mdp://127.0.0.1:22346/mmi.service]");
    }

    { // list everything from DNS broker
        zmq::Context         clientContext;
        MessageNode client(clientContext);
        REQUIRE(client.connect(dnsAddress));

        auto request = createClientMessage(mdp::Command::Set);
        request.serviceName = "mmi.dns";
        request.data = IoBuffer("Hello World!");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "mmi.dns");
        REQUIRE(reply->data.asString() == "[dnsBroker: mdp://127.0.0.1:22345,mdp://127.0.0.1:22345/mmi.dns,mdp://127.0.0.1:22345/mmi.echo,"
                                 "mdp://127.0.0.1:22345/mmi.openapi,mdp://127.0.0.1:22345/mmi.service],"
                                 "[testbroker: https://127.0.0.1:8080,https://127.0.0.1:8080/aDevice/aProperty,https://127.0.0.1:8080/mmi.dns,"
                                 "https://127.0.0.1:8080/mmi.echo,https://127.0.0.1:8080/mmi.openapi,https://127.0.0.1:8080/mmi.service,"
                                 "mdp://127.0.0.1:22346,mdp://127.0.0.1:22346/aDevice/aProperty,mdp://127.0.0.1:22346/mmi.dns,"
                                 "mdp://127.0.0.1:22346/mmi.echo,mdp://127.0.0.1:22346/mmi.openapi,mdp://127.0.0.1:22346/mmi.service]");
    }

    { // query for specific services
        zmq::Context         clientContext;
        MessageNode client(clientContext);
        REQUIRE(client.connect(dnsAddress));

        auto request = createClientMessage(mdp::Command::Set);
        request.serviceName = "mmi.dns";

        // atm services must be prepended by "/" to form URIs that opencmw::URI can parse
        // send query with some crazy whitespace
        request.data = IoBuffer(" /mmi.dns  , /aDevice/aProperty");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "mmi.dns");
        REQUIRE(reply->data.asString() == "[/mmi.dns: https://127.0.0.1:8080/mmi.dns,mdp://127.0.0.1:22345/mmi.dns,mdp://127.0.0.1:22346/mmi.dns],[/aDevice/aProperty: https://127.0.0.1:8080/aDevice/aProperty,mdp://127.0.0.1:22346/aDevice/aProperty]");
    }
}

TEST_CASE("Test mmi.service", "[broker][mmi][mmi_service]") {
    using opencmw::majordomo::Broker;

    Broker               broker("testbroker", testSettings());
    RunInThread          brokerRun(broker);

    MessageNode client(broker.context);
    REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    { // ask for not yet existing service
        auto request = createClientMessage(mdp::Command::Get);
        request.serviceName = "mmi.service";
        request.data = IoBuffer("a.service");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "mmi.service");
        REQUIRE(reply->data.asString() == "404");
    }

    // register worker as a.service
    MessageNode worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready = createWorkerMessage(mdp::Command::Ready);
    ready.serviceName = "a.service";
    ready.data = IoBuffer("API description");
    ready.rbac = IoBuffer("rbacToken");
    worker.send(std::move(ready));

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    { // service now exists
        auto request = createClientMessage(mdp::Command::Get);
        request.serviceName = "mmi.service";
        request.data = IoBuffer("a.service");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "mmi.service");
        REQUIRE(reply->data.asString() == "200");
    }

    { // list services
        auto request = createClientMessage(mdp::Command::Get);
        request.serviceName = "mmi.service";
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "mmi.service");
        REQUIRE(reply->data.asString() == "a.service,mmi.dns,mmi.echo,mmi.openapi,mmi.service");
    }
}

TEST_CASE("Test mmi.echo", "[broker][mmi][mmi_echo]") {
    using opencmw::majordomo::Broker;

    Broker               broker("testbroker", testSettings());
    RunInThread          brokerRun(broker);

    MessageNode client(broker.context);
    REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    auto request = createClientMessage(mdp::Command::Get);
    request.serviceName = "mmi.echo";
    request.data = IoBuffer("Wie heisst der Buergermeister von Wesel");
    request.rbac = IoBuffer("rbac");

    client.send(mdp::Message{request});

    const auto reply = client.tryReadOne();

    REQUIRE(reply.has_value());
    REQUIRE(reply->protocolName == mdp::clientProtocol);
    REQUIRE(reply->command == mdp::Command::Get);
    REQUIRE(reply->serviceName == request.serviceName);
    REQUIRE(reply->clientRequestID.asString() == request.clientRequestID.asString());
    REQUIRE(reply->endpoint == request.endpoint);
    REQUIRE(reply->data.asString() == request.data.asString());
    REQUIRE(reply->error == request.error);
    REQUIRE(reply->rbac.asString() == request.rbac.asString());
}

TEST_CASE("Test mmi.openapi", "[broker][mmi][mmi_openapi]") {
    using opencmw::majordomo::Broker;

    Broker               broker("testbroker", testSettings());
    RunInThread          brokerRun(broker);

    MessageNode client(broker.context);
    REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    { // request API of not yet existing service
        auto request = createClientMessage(mdp::Command::Get);
        request.serviceName = "mmi.openapi";
        request.data = IoBuffer("a.service");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "mmi.openapi");
        REQUIRE(reply->data.asString() == "");
        REQUIRE(reply->error == "Requested invalid service 'a.service'");
    }

    // register worker as a.service
    MessageNode worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready = createWorkerMessage(mdp::Command::Ready);
    ready.serviceName = "a.service";
    ready.data = IoBuffer("API description");
    ready.rbac = IoBuffer("rbacToken");
    worker.send(std::move(ready));

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    { // service now exists, API description is returned
        auto request = createClientMessage(mdp::Command::Get);
        request.serviceName = "mmi.openapi";
        request.data = IoBuffer("a.service");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "mmi.openapi");
        REQUIRE(reply->data.asString() == "API description");
        REQUIRE(reply->error == "");
    }
}

TEST_CASE("Request answered with unknown service", "[broker][unknown_service]") {
    using opencmw::majordomo::Broker;

    const auto address = URI<>("inproc://testrouter");

    Broker     broker("testbroker", testSettings());

    REQUIRE(broker.bind(address, BindOption::Router));

    MessageNode client(broker.context);
    REQUIRE(client.connect(address));

    RunInThread brokerRun(broker);

    auto        request = createClientMessage(mdp::Command::Get);
    request.serviceName = "no.service";
    request.clientRequestID = IoBuffer("1");
    request.endpoint = mdp::Message::URI("/topic");
    request.rbac = IoBuffer("rbacToken");
    client.send(std::move(request));

    const auto reply = client.tryReadOne();

    REQUIRE(reply.has_value());
    REQUIRE(reply->protocolName == mdp::clientProtocol);
    REQUIRE(reply->command == mdp::Command::Final);
    REQUIRE(reply->serviceName == "no.service");
    REQUIRE(reply->clientRequestID.asString() == "1");
    REQUIRE(reply->endpoint.str() == "/mmi.service");
    REQUIRE(reply->data.empty());
    REQUIRE(reply->error == "unknown service (error 501): 'no.service'");
    REQUIRE(reply->rbac.asString() == "RBAC=ADMIN,abcdef12345");
}

TEST_CASE("Test toZeroMQEndpoint conversion", "[utils][toZeroMQEndpoint]") {
    REQUIRE(mdp::toZeroMQEndpoint(URI<>("mdp://127.0.0.1:12345")) == "tcp://127.0.0.1:12345");
    REQUIRE(mdp::toZeroMQEndpoint(URI<>("mds://127.0.0.1:12345")) == "tcp://127.0.0.1:12345");
    REQUIRE(mdp::toZeroMQEndpoint(URI<>("inproc://test")) == "inproc://test");
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

    Broker broker("testbroker", testSettings());

    for (const auto &testcase : testcases) {
        const auto endpoint = std::get<0>(testcase);
        const auto option   = std::get<1>(testcase);
        const auto expected = std::get<2>(testcase);
        REQUIRE(broker.bind(endpoint, option) == expected);
    }
}

TEST_CASE("One client/one worker roundtrip", "[broker][roundtrip]") {
    using opencmw::majordomo::Broker;

    Broker               broker("testbroker", testSettings());

    MessageNode worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready = createWorkerMessage(mdp::Command::Ready);
    ready.serviceName = "a.service";
    ready.data = IoBuffer("API description");
    ready.rbac = IoBuffer("rbacToken");
    worker.send(std::move(ready));

    broker.processMessages();

    auto request = createClientMessage(mdp::Command::Get);
    request.serviceName = "a.service";
    request.clientRequestID = IoBuffer("1");
    request.endpoint = mdp::Message::URI("/topic");
    request.rbac = IoBuffer("rbacToken");
    client.send(std::move(request));

    broker.processMessages();

    const auto requestAtWorker = worker.tryReadOne();
    REQUIRE(requestAtWorker.has_value());
    REQUIRE(requestAtWorker->protocolName == mdp::workerProtocol);
    REQUIRE(requestAtWorker->command == mdp::Command::Get);
    REQUIRE(!requestAtWorker->serviceName.empty()); // clientSourceID
    REQUIRE(requestAtWorker->clientRequestID.asString() == "1");
    REQUIRE(requestAtWorker->endpoint.str() == "/topic");
    REQUIRE(requestAtWorker->data.empty());
    REQUIRE(requestAtWorker->error.empty());
    REQUIRE(requestAtWorker->rbac.asString() == "rbacToken");

    auto replyFromWorker = createWorkerMessage(mdp::Command::Final);
    replyFromWorker.serviceName = requestAtWorker->serviceName; // clientSourceID
    replyFromWorker.clientRequestID = IoBuffer("1");
    replyFromWorker.endpoint = mdp::Message::URI("/topic");
    replyFromWorker.data = IoBuffer("reply body");
    replyFromWorker.rbac = IoBuffer("rbac_worker");
    worker.send(std::move(replyFromWorker));

    broker.processMessages();

    const auto reply = client.tryReadOne();
    REQUIRE(reply.has_value());
    REQUIRE(reply->protocolName == mdp::clientProtocol);
    REQUIRE(reply->command == mdp::Command::Final);
    REQUIRE(reply->serviceName == "a.service");
    REQUIRE(reply->clientRequestID.asString() == "1");
    REQUIRE(reply->endpoint.str() == "/topic");
    REQUIRE(reply->data.asString() == "reply body");
    REQUIRE(reply->error.empty());
    REQUIRE(reply->rbac.asString() == "rbac_worker");

    broker.cleanup();

    {
        const auto heartbeat = worker.tryReadOne();
        REQUIRE(heartbeat.has_value());
        REQUIRE(heartbeat->protocolName == mdp::workerProtocol);
        REQUIRE(heartbeat->command == mdp::Command::Heartbeat);
        REQUIRE(heartbeat->serviceName == "a.service");
        REQUIRE(heartbeat->rbac.asString() == "RBAC=ADMIN,abcdef12345");
    }

    const auto disconnect = worker.tryReadOne();
    REQUIRE(disconnect.has_value());
    REQUIRE(disconnect->protocolName == mdp::workerProtocol);
    REQUIRE(disconnect->command == mdp::Command::Disconnect);
    REQUIRE(disconnect->serviceName == "a.service");
    REQUIRE(disconnect->clientRequestID.empty());
    REQUIRE(disconnect->endpoint.str() == "/a.service");
    REQUIRE(disconnect->data.asString() == "broker shutdown");
    REQUIRE(disconnect->error.empty());
    REQUIRE(disconnect->rbac.asString() == "RBAC=ADMIN,abcdef12345");
}

TEST_CASE("Test service matching", "[broker][name-matcher]") {
    using opencmw::majordomo::Broker;

    Broker               broker("testbroker", testSettings());

    MessageNode worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready = createWorkerMessage(mdp::Command::Ready);
    ready.serviceName = "/DeviceA/dashboard";
    ready.data = IoBuffer("An example worker serving different dashbards");
    ready.rbac = IoBuffer("rbacToken");
    worker.send(std::move(ready));

    broker.processMessages();

    {
        auto request = createClientMessage(mdp::Command::Get);
        request.serviceName = "/DeviceA/dashboard";
        request.clientRequestID = IoBuffer("1");
        request.endpoint = mdp::Message::URI("/DeviceA/dashboard");
        request.rbac = IoBuffer("rbacToken");
        client.send(std::move(request));

        broker.processMessages();

        const auto requestAtWorker = worker.tryReadOneSkipHB(3);
        REQUIRE(requestAtWorker.has_value());
        REQUIRE(requestAtWorker->protocolName == mdp::workerProtocol);
        REQUIRE(requestAtWorker->command == mdp::Command::Get);
        REQUIRE(!requestAtWorker->serviceName.empty()); // clientSourceID
        REQUIRE(requestAtWorker->clientRequestID.asString() == "1");
        REQUIRE(requestAtWorker->endpoint.str() == "/DeviceA/dashboard");
        REQUIRE(requestAtWorker->data.empty());
        REQUIRE(requestAtWorker->error.empty());
        REQUIRE(requestAtWorker->rbac.asString() == "rbacToken");

        auto replyFromWorker = createWorkerMessage(mdp::Command::Final);
        replyFromWorker.serviceName = requestAtWorker->serviceName; // clientSourceID
        replyFromWorker.clientRequestID = IoBuffer("1");
        replyFromWorker.endpoint = mdp::Message::URI("/DeviceA/dashboard/default");
        replyFromWorker.data = IoBuffer("Testreply");
        replyFromWorker.rbac = IoBuffer("rbac_worker");
        worker.send(std::move(replyFromWorker));

        broker.processMessages();

        const auto reply = client.tryReadOneSkipHB(3);
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/DeviceA/dashboard");
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->endpoint.str() == "/DeviceA/dashboard/default");
        REQUIRE(reply->data.asString() == "Testreply");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker");
    }

    {
        auto request = createClientMessage(mdp::Command::Get);
        request.serviceName = "/DeviceA/dashboard/main";
        request.clientRequestID = IoBuffer("2");
        request.endpoint = mdp::Message::URI("/DeviceA/dashboard/main?revision=12");
        request.rbac = IoBuffer("rbacToken");
        client.send(std::move(request));

        broker.processMessages();

        const auto requestAtWorker = worker.tryReadOneSkipHB(3);
        REQUIRE(requestAtWorker.has_value());
        REQUIRE(requestAtWorker->protocolName == mdp::workerProtocol);
        REQUIRE(requestAtWorker->command == mdp::Command::Get);
        REQUIRE(!requestAtWorker->serviceName.empty()); // clientSourceID
        REQUIRE(requestAtWorker->clientRequestID.asString() == "2");
        REQUIRE(requestAtWorker->endpoint.str() == "/DeviceA/dashboard/main?revision=12");
        REQUIRE(requestAtWorker->data.empty());
        REQUIRE(requestAtWorker->error.empty());
        REQUIRE(requestAtWorker->rbac.asString() == "rbacToken");

        auto replyFromWorker = createWorkerMessage(mdp::Command::Final);
        replyFromWorker.serviceName = requestAtWorker->serviceName; // clientSourceID
        replyFromWorker.clientRequestID = IoBuffer("2");
        replyFromWorker.endpoint = mdp::Message::URI("/DeviceA/dashboard/main?revision=12");
        replyFromWorker.data = IoBuffer("Testreply");
        replyFromWorker.rbac = IoBuffer("rbac_worker");
        worker.send(std::move(replyFromWorker));

        broker.processMessages();

        const auto reply = client.tryReadOneSkipHB(3);
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "/DeviceA/dashboard");
        REQUIRE(reply->clientRequestID.asString() == "2");
        REQUIRE(reply->endpoint.str() == "/DeviceA/dashboard/main?revision=12");
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
    REQUIRE(disconnect->serviceName == "/DeviceA/dashboard");
    REQUIRE(disconnect->clientRequestID.empty());
    REQUIRE(disconnect->endpoint.str() == "//DeviceA/dashboard");
    REQUIRE(disconnect->data.asString() == "broker shutdown");
    REQUIRE(disconnect->error.empty());
    REQUIRE(disconnect->rbac.asString() == "RBAC=ADMIN,abcdef12345");
}

TEST_CASE("Pubsub example using SUB client/DEALER worker", "[broker][pubsub_sub_dealer]") {
    using opencmw::majordomo::BindOption;
    using opencmw::majordomo::Broker;

    const auto publisherAddress = URI<>("inproc://testpub");

    Broker     broker("testbroker", testSettings());

    REQUIRE(broker.bind(publisherAddress, BindOption::Pub));

    BrokerMessageNode subscriber(broker.context, ZMQ_SUB);
    REQUIRE(subscriber.connect(publisherAddress, "/a.topic"));
    REQUIRE(subscriber.subscribe("/other.*"));

    broker.processMessages();

    MessageNode publisher(broker.context);
    REQUIRE(publisher.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // send three notifications, two matching (one exact, one via wildcard), one not matching
    {
        auto notify = createWorkerMessage(mdp::Command::Notify);
        notify.serviceName = "a.service";
        notify.endpoint = mdp::Message::URI("/a.topic");
        notify.data = IoBuffer("Notification about /a.topic");
        notify.rbac = IoBuffer("rbac_worker");
        publisher.send(std::move(notify));
    }

    broker.processMessages();

    {
        auto notify = createWorkerMessage(mdp::Command::Notify);
        notify.serviceName = "a.service";
        notify.endpoint = mdp::Message::URI("/a.topic_2");
        notify.data = IoBuffer("Notification about /a.topic_2");
        notify.rbac = IoBuffer("rbac_worker");
        publisher.send(std::move(notify));
    }

    broker.processMessages();

    {
        auto notify = createWorkerMessage(mdp::Command::Notify);
        notify.serviceName = "a.service";
        notify.endpoint = mdp::Message::URI("/other.topic");
        notify.data = IoBuffer("Notification about /other.topic");
        notify.rbac = IoBuffer("rbac_worker");
        publisher.send(std::move(notify));
    }

    broker.processMessages();

    // receive only messages matching subscriptions

    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/a.topic");
        REQUIRE(reply->serviceName == "a.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->data.asString() == "Notification about /a.topic");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker");
    }

    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/other.*");
        REQUIRE(reply->serviceName == "a.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->data.asString() == "Notification about /other.topic");
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
    Broker               broker("testbroker", settings);

    MessageNode worker(broker.context);

    RunInThread          brokerRun(broker);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    {
        auto ready = createWorkerMessage(mdp::Command::Ready);
        ready.serviceName = "heartbeat.service";
        ready.data = IoBuffer("API description");
        ready.rbac = IoBuffer("rbac_worker");
        worker.send(std::move(ready));
    }

    const auto afterReady = Clock::now();

    std::this_thread::sleep_for(heartbeatInterval * 0.75);

    {
        auto heartbeat = createWorkerMessage(mdp::Command::Heartbeat);
        heartbeat.serviceName = "heartbeat.service";
        heartbeat.rbac = IoBuffer("rbac_worker");
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
    Broker               broker("testbroker", settings);

    MessageNode worker(broker.context);

    RunInThread          brokerRun(broker);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // send heartbeat without initial ready - invalid
    auto heartbeat = createWorkerMessage(mdp::Command::Heartbeat);
    heartbeat.serviceName = "heartbeat.service";
    heartbeat.rbac = IoBuffer("rbac_worker");
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

    Broker               broker("testbroker", settings);
    RunInThread          brokerRun(broker);

    MessageNode worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    {
        auto ready = createWorkerMessage(mdp::Command::Ready);
        ready.serviceName = "a.service";
        ready.data = IoBuffer("API description");
        ready.rbac = IoBuffer("rbac_worker");
        worker.send(std::move(ready));
    }

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    MessageNode client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    constexpr auto roles           = std::array{ "ANY", "UNKNOWN", "USER", "BOSS", "ADMIN" };

    int            clientRequestId = 0;
    for (const auto &role : roles) {
        auto msg = createClientMessage(mdp::Command::Get);
        const auto reqId = std::to_string(clientRequestId++);
        msg.clientRequestID = IoBuffer(reqId.data(), reqId.size());
        msg.serviceName = "a.service";
        const auto rbac = fmt::format("RBAC={},123456abcdef", role);
        msg.rbac = IoBuffer(rbac.data(), rbac.size());
        client.send(std::move(msg));
    }

    {
        // read first message but don't reply immediately, this forces the broker to queue the following requests
        const auto msg = worker.tryReadOne();
        REQUIRE(msg.has_value());
        REQUIRE(msg->clientRequestID.asString() == "0");

        // we give the broker time to read and queue the following requests
        std::this_thread::sleep_for(settings.heartbeatInterval * 0.7);

        auto reply = createWorkerMessage(mdp::Command::Final);
        reply.serviceName = msg->serviceName; // clientSourceID
        reply.clientRequestID = msg->clientRequestID;
        reply.data = IoBuffer("Hello!");
        worker.send(std::move(reply));
    }

    // the remaining messages must have been queued in the broker and thus be reordered:
    // "ADMIN", "BOSS", "USER", "UNKNOWN" (unexpected roles come last)
    std::vector<std::string> seenMessages;
    while (seenMessages.size() < roles.size() - 1) {
        const auto msg = worker.tryReadOne();
        REQUIRE(msg.has_value());
        seenMessages.push_back(std::string(msg->clientRequestID.asString()));

        auto reply = createWorkerMessage(mdp::Command::Final);
        reply.serviceName = msg->serviceName; // clientSourceID
        reply.clientRequestID = msg->clientRequestID;
        reply.data = IoBuffer("Hello!");
        worker.send(std::move(reply));
    }

    REQUIRE(seenMessages == std::vector{ "4"s, "3"s, "2"s, "1"s });
}

TEST_CASE("pubsub example using router socket (DEALER client)", "[broker][pubsub_router]") {
    using opencmw::majordomo::Broker;

    Broker               broker("testbroker", testSettings());

    MessageNode subscriber(broker.context);
    REQUIRE(subscriber.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode publisherOne(broker.context);
    REQUIRE(publisherOne.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode publisherTwo(broker.context);
    REQUIRE(publisherTwo.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // subscribe client to /cooking.italian
    {
        auto subscribe = createClientMessage(mdp::Command::Subscribe);
        subscribe.serviceName = "first.service";
        subscribe.endpoint = mdp::Message::URI("/cooking.italian");
        subscribe.rbac = IoBuffer("rbacToken");
        subscriber.send(std::move(subscribe));
    }

    broker.processMessages();

    // subscribe client to /cooking.indian
    {
        auto subscribe = createClientMessage(mdp::Command::Subscribe);
        subscribe.serviceName = "second.service";
        subscribe.endpoint = mdp::Message::URI("/cooking.indian");
        subscribe.rbac = IoBuffer("rbacToken");
        subscriber.send(std::move(subscribe));
    }

    broker.processMessages();

    // publisher 1 sends a notification for /cooking.italian
    {
        auto pubMsg = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "first.service";
        pubMsg.endpoint = mdp::Message::URI("/cooking.italian");
        pubMsg.data = IoBuffer("Original carbonara recipe here!");
        pubMsg.rbac = IoBuffer("rbac_worker_1");
        publisherOne.send(std::move(pubMsg));
    }

    broker.processMessages();

    // client receives notification for /cooking.italian
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "first.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->endpoint.str() == "/cooking.italian");
        REQUIRE(reply->data.asString() == "Original carbonara recipe here!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_1");
    }

    // publisher 2 sends a notification for /cooking.indian
    {
        auto pubMsg = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "second.service";
        pubMsg.endpoint = mdp::Message::URI("/cooking.indian");
        pubMsg.data = IoBuffer("Try our Chicken Korma!");
        pubMsg.rbac = IoBuffer("rbac_worker_2");
        publisherTwo.send(std::move(pubMsg));
    }

    broker.processMessages();

    // client receives notification for /cooking.indian
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "second.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->endpoint.str() == "/cooking.indian");
        REQUIRE(reply->data.asString() == "Try our Chicken Korma!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_2");
    }

    // unsubscribe client from /cooking.italian
    {
        auto unsubscribe = createClientMessage(mdp::Command::Unsubscribe);
        unsubscribe.serviceName = "first.service";
        unsubscribe.endpoint = mdp::Message::URI("/cooking.italian");
        unsubscribe.rbac = IoBuffer("rbacToken");
        subscriber.send(std::move(unsubscribe));
    }

    broker.processMessages();

    // publisher 1 sends a notification for /cooking.italian
    {
        auto pubMsg = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "first.service";
        pubMsg.endpoint = mdp::Message::URI("/cooking.italian");
        pubMsg.data = IoBuffer("The best Margherita in town!");
        pubMsg.rbac = IoBuffer("rbac_worker_1");
        publisherOne.send(std::move(pubMsg));
    }

    broker.processMessages();

    // publisher 2 sends a notification for /cooking.indian
    {
        auto pubMsg = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "second.service";
        pubMsg.endpoint = mdp::Message::URI("/cooking.indian");
        pubMsg.data = IoBuffer("Sizzling tikkas in our Restaurant!");
        pubMsg.rbac = IoBuffer("rbac_worker_2");
        publisherTwo.send(std::move(pubMsg));
    }

    broker.processMessages();

    // verify that the client receives only the notification from publisher 2

    {
        const auto reply = subscriber.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "second.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->endpoint.str() == "/cooking.indian");
        REQUIRE(reply->data.asString() == "Sizzling tikkas in our Restaurant!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_2");
    }
}

TEST_CASE("pubsub example using PUB socket (SUB client)", "[broker][pubsub_subclient]") {
    using opencmw::majordomo::Broker;

    Broker                  broker("testbroker", testSettings());

    BrokerMessageNode subscriber(broker.context, ZMQ_SUB);
    REQUIRE(subscriber.connect(opencmw::majordomo::INTERNAL_ADDRESS_PUBLISHER));

    MessageNode publisherOne(broker.context);
    REQUIRE(publisherOne.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    MessageNode publisherTwo(broker.context);
    REQUIRE(publisherTwo.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    subscriber.subscribe("/cooking.italian*");

    broker.processMessages();

    subscriber.subscribe("/cooking.indian*");

    broker.processMessages();

    // publisher 1 sends a notification for /cooking.italian.pasta
    {
        auto pubMsg = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "first.service";
        pubMsg.endpoint = mdp::Message::URI("/cooking.italian.pasta");
        pubMsg.data = IoBuffer("Original carbonara recipe here!");
        pubMsg.rbac = IoBuffer("rbac_worker_1");
        publisherOne.send(std::move(pubMsg));
    }

    broker.processMessages();

    // client receives notification for /cooking.italian*
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/cooking.italian*");
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "first.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->endpoint.str() == "/cooking.italian.pasta");
        REQUIRE(reply->data.asString() == "Original carbonara recipe here!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_1");
    }

    // publisher 2 sends a notification for /cooking.indian.chicken
    {
        auto pubMsg = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "second.service";
        pubMsg.endpoint = mdp::Message::URI("/cooking.indian.chicken");
        pubMsg.data = IoBuffer("Try our Chicken Korma!");
        pubMsg.rbac = IoBuffer("rbac_worker_2");
        publisherTwo.send(std::move(pubMsg));
    }

    broker.processMessages();

    // client receives notification for /cooking.indian*
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/cooking.indian*");
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "second.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->endpoint.str() == "/cooking.indian.chicken");
        REQUIRE(reply->data.asString() == "Try our Chicken Korma!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_2");
    }

    subscriber.unsubscribe("/cooking.italian*");

    broker.processMessages();

    // publisher 1 sends a notification for /cooking.italian.pizza
    {
        auto pubMsg = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "first.service";
        pubMsg.endpoint = mdp::Message::URI("/cooking.italian.pizza");
        pubMsg.data = IoBuffer("The best Margherita in town!");
        pubMsg.rbac = IoBuffer("rbac_worker_1");
        publisherOne.send(std::move(pubMsg));
    }

    broker.processMessages();

    // publisher 2 sends a notification for /cooking.indian.tikkas
    {
        auto pubMsg = createWorkerMessage(mdp::Command::Notify);
        pubMsg.serviceName = "second.service";
        pubMsg.endpoint = mdp::Message::URI("/cooking.indian.tikkas");
        pubMsg.data = IoBuffer("Sizzling tikkas in our Restaurant!");
        pubMsg.rbac = IoBuffer("rbac_worker_2");
        publisherTwo.send(std::move(pubMsg));
    }

    broker.processMessages();

    // verify that the client receives only the notification from publisher 2

    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->sourceId == "/cooking.indian*");
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->serviceName == "second.service");
        REQUIRE(reply->clientRequestID.empty());
        REQUIRE(reply->endpoint.str() == "/cooking.indian.tikkas");
        REQUIRE(reply->data.asString() == "Sizzling tikkas in our Restaurant!");
        REQUIRE(reply->error.empty());
        REQUIRE(reply->rbac.asString() == "rbac_worker_2");
    }
}

TEST_CASE("BasicWorker connects to non-existing broker", "[worker]") {
    const zmq::Context       context;
    BasicWorker<"a.service"> worker(URI<>("inproc:/doesnotexist"), TestIntHandler(10), context);
    worker.run(); // returns immediately on connection failure
}

TEST_CASE("BasicWorker run loop quits when broker quits", "[worker]") {
    const zmq::Context       context;
    Broker                   broker("testbroker", testSettings());
    BasicWorker<"a.service"> worker(broker, TestIntHandler(10));

    RunInThread              brokerRun(broker);

    auto                     quitBroker = std::jthread([&broker]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        broker.shutdown();
                        });

    worker.run(); // returns when broker disappears
    quitBroker.join();
}

TEST_CASE("BasicWorker connection basics", "[worker][basic_worker_connection]") {
    const zmq::Context      context;
    BrokerMessageNode brokerRouter(context, ZMQ_ROUTER);
    BrokerMessageNode brokerPub(context, ZMQ_PUB);
    const auto              brokerAddress = opencmw::URI<opencmw::STRICT>("inproc://test/");
    const auto              routerAddress = opencmw::URI<opencmw::STRICT>::factory(brokerAddress).path(opencmw::majordomo::SUFFIX_ROUTER).build();
    const auto              pubAddress    = opencmw::URI<opencmw::STRICT>::factory(brokerAddress).path(opencmw::majordomo::SUFFIX_SUBSCRIBE).build();
    REQUIRE(brokerRouter.bind(routerAddress));
    REQUIRE(brokerPub.bind(pubAddress));
    Settings settings;
    settings.heartbeatInterval       = std::chrono::milliseconds(200);
    settings.heartbeatLiveness       = 2;
    settings.workerReconnectInterval = std::chrono::milliseconds(200);

    BasicWorker<"a.service"> worker(
            routerAddress, [](RequestContext &) {}, context, settings);
    RunInThread workerRun(worker);

    std::string workerId;

    // worker sends initial READY message
    {
        const auto ready = brokerRouter.tryReadOne();
        REQUIRE(ready.has_value());
        REQUIRE(ready->command == mdp::Command::Ready);
        REQUIRE(ready->serviceName == "a.service");
        workerId = ready->sourceId;
    }

    // worker must send a heartbeat
    {
        const auto heartbeat = brokerRouter.tryReadOne(settings.heartbeatInterval * 23 / 10);
        REQUIRE(heartbeat.has_value());
        REQUIRE(heartbeat->command == mdp::Command::Heartbeat);
        REQUIRE(heartbeat->serviceName == "a.service");
        REQUIRE(heartbeat->sourceId == workerId);
    }

    // not receiving heartbeats, the worker reconnects and sends a new READY message
    {
        const auto ready = brokerRouter.tryReadOne(settings.heartbeatInterval * (settings.heartbeatLiveness + 1) + settings.workerReconnectInterval);
        REQUIRE(ready.has_value());
        REQUIRE(ready->command == mdp::Command::Ready);
        REQUIRE(ready->serviceName == "a.service");
        workerId = ready->sourceId;
    }

    // send heartbeat to worker
    {
        BrokerMessage heartbeat;
        heartbeat.protocolName = mdp::workerProtocol;
        heartbeat.command = mdp::Command::Heartbeat;
        heartbeat.sourceId = workerId;
        brokerRouter.send(std::move(heartbeat));
    }

    worker.shutdown();

    // worker sends a DISCONNECT on shutdown
    {
        const auto disconnect = brokerRouter.tryReadOne();
        REQUIRE(disconnect.has_value());
        REQUIRE(disconnect->command == mdp::Command::Disconnect);
        REQUIRE(disconnect->serviceName == "a.service");
        REQUIRE(disconnect->sourceId == workerId);
    }
}

TEST_CASE("SET/GET example using the BasicWorker class", "[worker][getset_basic_worker]") {
    using opencmw::majordomo::Broker;

    Broker                                                                       broker("testbroker", testSettings());

    BasicWorker<"a.service", opencmw::majordomo::description<"API description">> worker(broker, TestIntHandler(10));
    REQUIRE(worker.serviceDescription() == "API description");

    MessageNode client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    // until the worker's READY is processed by the broker, it will return
    // an "unknown service" error, retry until we get the expected reply
    bool replyReceived = false;
    while (!replyReceived) {
        auto request = createClientMessage(mdp::Command::Get);
        request.serviceName = "a.service";
        request.clientRequestID = IoBuffer("1");
        request.endpoint = mdp::Message::URI("/topic");
        request.rbac = IoBuffer("rbacToken");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->clientRequestID.asString() == "1");

        if (!reply->error.empty()) {
            REQUIRE(reply->error.find("error 501") != std::string_view::npos);
        } else {
            REQUIRE(reply->serviceName == "a.service");
            REQUIRE(reply->endpoint.str() == "/topic");
            REQUIRE(reply->data.asString() == "10");
            REQUIRE(reply->error.empty());
            REQUIRE(reply->rbac.asString() == "rbacToken");
            replyReceived = true;
        }
    }

    {
        auto request = createClientMessage(mdp::Command::Set);
        request.serviceName = "a.service";
        request.clientRequestID = IoBuffer("2");
        request.endpoint = mdp::Message::URI("/topic");
        request.data = IoBuffer("42");
        request.rbac = IoBuffer("rbacToken");

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
        auto request = createClientMessage(mdp::Command::Get);
        request.serviceName = "a.service";
        request.clientRequestID = IoBuffer("3");
        request.endpoint = mdp::Message::URI("/topic");
        request.rbac = IoBuffer("rbacToken");
        client.send(std::move(request));

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->protocolName == mdp::clientProtocol);
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->clientRequestID.asString() == "3");
        REQUIRE(reply->endpoint.str() == "/topic");
        REQUIRE(reply->data.asString() == "42");
        REQUIRE(reply->error.empty());
    }
}

TEST_CASE("BasicWorker SET/GET example with RBAC permission handling", "[worker][getset_basic_worker][rbac]") {
    using WRITER = Role<"WRITER", Permission::WO>;
    using READER = Role<"READER", Permission::RO>;
    using opencmw::majordomo::description;

    Broker                                                                          broker("testbroker", testSettings());
    BasicWorker<"/a.service", description<"API description">, rbac<WRITER, READER>> worker(broker, TestIntHandler(10));
    REQUIRE(worker.serviceDescription() == "API description");

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "/a.service"));

    MessageNode writer(broker.context);
    REQUIRE(writer.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // writer is allowed to SET
    {
        auto set = createClientMessage(mdp::Command::Set);
        set.serviceName = "/a.service";
        set.clientRequestID = IoBuffer("1");
        set.endpoint = mdp::Message::URI("/topic");
        set.data = IoBuffer("42");
        set.rbac = IoBuffer("RBAC=WRITER,1234");

        writer.send(std::move(set));

        const auto reply = writer.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->data.asString() == "Value set. All good!");
        REQUIRE(reply->error.empty());
    }

    // writer is not allowed to GET
    {
        auto get = createClientMessage(mdp::Command::Get);
        get.serviceName = "/a.service";
        get.clientRequestID = IoBuffer("2");
        get.endpoint = mdp::Message::URI("/topic");
        get.rbac = IoBuffer("RBAC=WRITER,1234");

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
        auto set = createClientMessage(mdp::Command::Set);
        set.serviceName = "/a.service";
        set.clientRequestID = IoBuffer("1");
        set.endpoint = mdp::Message::URI("/topic");
        set.data = IoBuffer("42");
        set.rbac = IoBuffer("RBAC=READER,1234");

        reader.send(std::move(set));

        const auto reply = reader.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->data.empty());
        REQUIRE(reply->error == "SET access denied to role 'READER'");
    }

    // reader is allowed to GET
    {
        auto get = createClientMessage(mdp::Command::Get);
        get.serviceName = "/a.service";
        get.clientRequestID = IoBuffer("2");
        get.endpoint = mdp::Message::URI("/topic");
        get.rbac = IoBuffer("RBAC=READER,1234");

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
        auto set = createClientMessage(mdp::Command::Set);
        set.serviceName = "/a.service";
        set.clientRequestID = IoBuffer("1");
        set.endpoint = mdp::Message::URI("/topic");
        set.data = IoBuffer("42");
        set.rbac = IoBuffer("RBAC=ADMIN,1234");

        admin.send(std::move(set));

        const auto reply = admin.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->clientRequestID.asString() == "1");
        REQUIRE(reply->data.asString() == "Value set. All good!");
        REQUIRE(reply->error.empty());
    }

    // admin is allowed to GET
    {
        auto get = createClientMessage(mdp::Command::Get);
        get.serviceName = "/a.service";
        get.clientRequestID = IoBuffer("2");
        get.endpoint = mdp::Message::URI("/topic");
        get.rbac = IoBuffer("RBAC=ADMIN,1234");

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

    Broker                   broker("testbroker", testSettings());

    BasicWorker<"beverages"> worker(broker, TestIntHandler(10));

    BrokerMessageNode  client(broker.context, ZMQ_XSUB);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_PUBLISHER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    // send some invalid subscribe/unsubscribe messages, must be ignored
    REQUIRE(client.sendRawFrame(""));
    REQUIRE(client.sendRawFrame("\x1"));
    REQUIRE(client.sendRawFrame("\x0"s));

    // subscribe to /wine* and /beer*
    REQUIRE(client.sendRawFrame("\x1/wine*"));
    REQUIRE(client.sendRawFrame("\x1/beer*"));

    bool seenNotification = false;

    // we have a potential race here: the worker might not have processed the
    // subscribe yet and thus discarding the notification. Send notifications
    // in a loop until one gets through.
    while (!seenNotification) {
        {
            mdp::Message notify;
            notify.endpoint = mdp::Message::URI("/beer.time");
            notify.data = IoBuffer("Have a beer");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            const auto notification = client.tryReadOne(std::chrono::milliseconds(20));
            if (notification && notification->serviceName != "mmi.service") {
                seenNotification = true;
                REQUIRE(notification->protocolName == mdp::clientProtocol);
                REQUIRE(notification->command == mdp::Command::Final);
                REQUIRE(notification->sourceId == "/beer*");
                REQUIRE(notification->endpoint.str() == "/beer.time");
                REQUIRE(notification->data.asString() == "Have a beer");
            }
        }
    }

    {
        mdp::Message notify;
        notify.endpoint = mdp::Message::URI("/beer.error");
        notify.error = "Fridge empty!";
        REQUIRE(worker.notify(std::move(notify)));
    }

    bool seenError = false;
    while (!seenError) {
        const auto notification = client.tryReadOne(std::chrono::milliseconds(20));
        if (!notification)
            continue;

        // there might be extra messages from above, ignore them
        if (notification->endpoint.str() == "/beer.time") {
            continue;
        }

        REQUIRE(notification->protocolName == mdp::clientProtocol);
        REQUIRE(notification->command == mdp::Command::Final);
        REQUIRE(notification->sourceId == "/beer*");
        REQUIRE(notification->endpoint.str() == "/beer.error");
        REQUIRE(notification->error == "Fridge empty!");
        seenError = true;
    }

    {
        // as the subscribe for wine* was sent before the beer* one, this should be
        // race-free now (as know the beer* subscribe was processed by everyone)
        mdp::Message notify;
        notify.endpoint = mdp::Message::URI("/wine.italian");
        notify.data = IoBuffer("Try our Chianti!");
        REQUIRE(worker.notify(std::move(notify)));
    }

    {
        const auto notification = client.tryReadOne();
        REQUIRE(notification.has_value());
        REQUIRE(notification->protocolName == mdp::clientProtocol);
        REQUIRE(notification->command == mdp::Command::Final);
        REQUIRE(notification->sourceId == "/wine*");
        REQUIRE(notification->endpoint.str() == "/wine.italian");
        REQUIRE(notification->data.asString() == "Try our Chianti!");
    }

    // unsubscribe from /beer*
    REQUIRE(client.sendRawFrame("\x0/beer*"s));

    // loop until we get two consecutive messages about wine, it means that the beer unsubscribe was processed
    while (true) {
        {
            mdp::Message notify;
            notify.endpoint = mdp::Message::URI("/wine.portuguese");
            notify.data = IoBuffer("New Vinho Verde arrived.");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            mdp::Message notify;
            notify.endpoint = mdp::Message::URI("/beer.offer");
            notify.data = IoBuffer("Get our pilsner now!");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            mdp::Message notify;
            notify.endpoint = mdp::Message::URI("/wine.portuguese");
            notify.data = IoBuffer("New Vinho Verde arrived.");
            REQUIRE(worker.notify(std::move(notify)));
        }

        const auto msg1 = client.tryReadOne();
        REQUIRE(msg1.has_value());
        REQUIRE(msg1->sourceId == "/wine*");

        const auto msg2 = client.tryReadOne();
        REQUIRE(msg2.has_value());
        if (msg2->sourceId == "/wine*") {
            break;
        }

        REQUIRE(msg2->sourceId == "/beer*");

        const auto msg3 = client.tryReadOne();
        REQUIRE(msg3.has_value());
        REQUIRE(msg3->sourceId == "/wine*");
    }
}

TEST_CASE("NOTIFY example using the BasicWorker class (via ROUTER socket)", "[worker][notify_basic_worker_router]") {
    using opencmw::majordomo::Broker;

    Broker                   broker("testbroker", testSettings());

    BasicWorker<"beverages"> worker(broker, TestIntHandler(10));

    MessageNode     client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    {
        auto subscribe = createClientMessage(mdp::Command::Subscribe);
        subscribe.serviceName = "beverages";
        subscribe.endpoint = mdp::Message::URI("/wine");
        client.send(std::move(subscribe));
    }
    {
        auto subscribe = createClientMessage(mdp::Command::Subscribe);
        subscribe.serviceName = "beverages";
        subscribe.endpoint = mdp::Message::URI("/beer");
        client.send(std::move(subscribe));
    }

    bool seenNotification = false;

    // we have a potential race here: the worker might not have processed the
    // subscribe yet and thus discarding the notification. Send notifications
    // in a loop until one gets through.
    while (!seenNotification) {
        {
            mdp::Message notify;
            notify.endpoint = mdp::Message::URI("/beer");
            notify.data = IoBuffer("Have a beer");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            const auto notification = client.tryReadOne(std::chrono::milliseconds(20));
            if (notification && notification->serviceName != "mmi.service") {
                seenNotification = true;
                REQUIRE(notification->protocolName == mdp::clientProtocol);
                REQUIRE(notification->command == mdp::Command::Final);
                REQUIRE(notification->endpoint.str() == "/beer");
                REQUIRE(notification->data.asString() == "Have a beer");
            }
        }
    }

    {
        // as the subscribe for /wine was sent before the /beer one, this should be
        // race-free now (as know the /beer subscribe was processed by everyone)
        mdp::Message notify;
        notify.endpoint = mdp::Message::URI("/wine");
        notify.data = IoBuffer("Try our Chianti!");
        REQUIRE(worker.notify(std::move(notify)));
    }

    {
        const auto notification = client.tryReadOne();
        REQUIRE(notification.has_value());
        REQUIRE(notification->protocolName == mdp::clientProtocol);
        REQUIRE(notification->command == mdp::Command::Final);
        REQUIRE(notification->endpoint.str() == "/wine");
        REQUIRE(notification->data.asString() == "Try our Chianti!");
    }

    // unsubscribe from /beer
    {
        auto unsubscribe = createClientMessage(mdp::Command::Unsubscribe);
        unsubscribe.serviceName = "beverages";
        unsubscribe.endpoint = mdp::Message::URI("/beer");
        client.send(std::move(unsubscribe));
    }

    // loop until we get two consecutive messages about wine, it means that the beer unsubscribe was processed
    while (true) {
        {
            mdp::Message notify;
            notify.endpoint = mdp::Message::URI("/wine");
            notify.data = IoBuffer("New Vinho Verde arrived.");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            mdp::Message notify;
            notify.endpoint = mdp::Message::URI("/beer");
            notify.data = IoBuffer("Get our pilsner now!");
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            mdp::Message notify;
            notify.endpoint = mdp::Message::URI("/wine");
            notify.data = IoBuffer("New Vinho Verde arrived.");
            REQUIRE(worker.notify(std::move(notify)));
        }

        const auto msg1 = client.tryReadOne();
        REQUIRE(msg1.has_value());
        REQUIRE(msg1->endpoint.str() == "/wine");

        const auto msg2 = client.tryReadOne();
        REQUIRE(msg2.has_value());
        if (msg2->endpoint.str() == "/wine") {
            break;
        }

        REQUIRE(msg2->endpoint.str() == "/beer");
        const auto msg3 = client.tryReadOne();
        REQUIRE(msg3.has_value());
        REQUIRE(msg3->endpoint.str() == "/wine");
    }
}

TEST_CASE("SET/GET example using a lambda as the worker's request handler", "[worker][lambda_handler]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MockClient;

    Broker broker("testbroker", testSettings());

    auto   handleInt = [](RequestContext &requestContext) {
        static int value = 100;

        if (requestContext.request.command == mdp::Command::Get) {
            const auto data = std::to_string(value);
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
            value = parsedValue;
            requestContext.reply.data = IoBuffer("Value set. All good!");
        }
    };

    BasicWorker<"a.service"> worker(broker, std::move(handleInt));

    MockClient               client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    client.get("a.service", {}, [](auto &&message) {
        REQUIRE(message.error == "");
        REQUIRE(message.data.asString() == "100");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));

    client.set("a.service", IoBuffer("42"), [](auto &&message) {
        REQUIRE(message.error == "");
        REQUIRE(message.data.asString() == "Value set. All good!");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));

    client.get("a.service", {}, [](auto &&message) {
        REQUIRE(message.error == "");
        REQUIRE(message.data.asString() == "42");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));
}

TEST_CASE("Worker's request handler throws an exception", "[worker][handler_exception]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MockClient;

    Broker broker("testbroker", testSettings());

    auto   handleRequest = [](RequestContext &) {
        throw std::runtime_error("Something went wrong!");
    };

    BasicWorker<"a.service"> worker(broker, std::move(handleRequest));

    MockClient               client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    client.get("a.service", {}, [](auto &&message) {
        REQUIRE(message.error == "Caught exception for service 'a.service'\nrequest message: \nexception: Something went wrong!");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));
}

TEST_CASE("Worker's request handler throws an unexpected exception", "[worker][handler_unexpected_exception]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MockClient;

    Broker broker("testbroker", testSettings());

    auto   handleRequest = [](RequestContext &) {
        throw std::string("Something went wrong!");
    };

    BasicWorker<"a.service"> worker(broker, std::move(handleRequest));

    MockClient               client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    client.get("a.service", {}, [](auto &&message) {
        REQUIRE(message.error == "Caught unexpected exception for service 'a.service'\nrequest message: ");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));
}
