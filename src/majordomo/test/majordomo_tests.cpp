#include "helpers.hpp"

#include <majordomo/BasicMdpWorker.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Client.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/Utils.hpp>

#include <catch2/catch.hpp>
#include <fmt/format.h>

#include <charconv>
#include <cstdlib>
#include <thread>

using namespace opencmw::majordomo;
using URI = opencmw::URI<>;

TEST_CASE("OpenCMW::Frame cloning", "[frame][cloning]") {
    {
        MessageFrame frame;
        auto         clone = frame.clone();
        REQUIRE(clone.data() == frame.data());
        REQUIRE(clone.data() == "");
    }
    {
        MessageFrame frame{ "Hello", MessageFrame::static_bytes_tag{} };
        auto         clone = frame.clone();
        REQUIRE(clone.data() == frame.data());
        REQUIRE(clone.data() == "Hello");
    }
    {
        MessageFrame frame{ std::make_unique<std::string>("Hello").release(), MessageFrame::dynamic_bytes_tag{} };
        auto         clone = frame.clone();
        REQUIRE(clone.data() == frame.data());
        REQUIRE(clone.data() == "Hello");
    }
    {
        MessageFrame frame{ "Hello", MessageFrame::dynamic_bytes_tag{} };
        auto         clone = frame.clone();
        REQUIRE(frame.data() == clone.data());
    }
}

constexpr auto static_tag  = MessageFrame::static_bytes_tag{};
constexpr auto dynamic_tag = MessageFrame::dynamic_bytes_tag{};

TEST_CASE("OpenCMW::Message basics", "[message]") {
    {
        auto msg = BrokerMessage::createClientMessage(Command::Final);
        REQUIRE(msg.isClientMessage());
        REQUIRE(msg.command() == Command::Final);

        auto tag = MessageFrame::static_bytes_tag{};
        msg.setTopic("/iamatopic", tag);
        msg.setServiceName("service://abc", tag);
        msg.setClientRequestId("request 1", tag);
        msg.setBody("test body test body test body test body test body test body test body", tag);
        msg.setError("fail!", tag);
        msg.setRbacToken("password", tag);

        REQUIRE(msg.isClientMessage());
        REQUIRE(msg.command() == Command::Final);
        REQUIRE(msg.topic() == "/iamatopic");
        REQUIRE(msg.serviceName() == "service://abc");
        REQUIRE(msg.clientRequestId() == "request 1");
        REQUIRE(msg.body() == "test body test body test body test body test body test body test body");
        REQUIRE(msg.error() == "fail!");
        REQUIRE(msg.rbacToken() == "password");

        REQUIRE(msg.isValid());
        REQUIRE(msg.availableFrameCount() == 9);
        REQUIRE(msg.frameAt(0).data() == "");
        REQUIRE(msg.frameAt(1).data() == "MDPC03");
        REQUIRE(msg.frameAt(2).data() == "\x4");
        REQUIRE(msg.frameAt(3).data() == "service://abc");
        REQUIRE(msg.frameAt(4).data() == "request 1");
        REQUIRE(msg.frameAt(5).data() == "/iamatopic");
        REQUIRE(msg.frameAt(6).data() == "test body test body test body test body test body test body test body");
        REQUIRE(msg.frameAt(7).data() == "fail!");
        REQUIRE(msg.frameAt(8).data() == "password");

        // Test command encoding
        REQUIRE(BrokerMessage::createClientMessage(Command::Get).frameAt(2).data() == "\x01");
        REQUIRE(BrokerMessage::createClientMessage(Command::Set).frameAt(2).data() == "\x02");
        REQUIRE(BrokerMessage::createClientMessage(Command::Partial).frameAt(2).data() == "\x03");
        REQUIRE(BrokerMessage::createClientMessage(Command::Final).frameAt(2).data() == "\x04");
        REQUIRE(BrokerMessage::createClientMessage(Command::Ready).frameAt(2).data() == "\x05");
        REQUIRE(BrokerMessage::createClientMessage(Command::Disconnect).frameAt(2).data() == "\x06");
        REQUIRE(BrokerMessage::createClientMessage(Command::Subscribe).frameAt(2).data() == "\x07");
        REQUIRE(BrokerMessage::createClientMessage(Command::Unsubscribe).frameAt(2).data() == "\x08");
        REQUIRE(BrokerMessage::createWorkerMessage(Command::Notify).frameAt(2).data() == "\x09");
        REQUIRE(BrokerMessage::createWorkerMessage(Command::Heartbeat).frameAt(2).data() == "\x0a");

        // make sure isValid detects command/protocol mismatches
        REQUIRE(!BrokerMessage::createClientMessage(Command::Notify).isValid());
        REQUIRE(!BrokerMessage::createWorkerMessage(Command::Subscribe).isValid());
        REQUIRE(!BrokerMessage::createWorkerMessage(Command::Unsubscribe).isValid());
    }

    {
        auto msg = MdpMessage::createClientMessage(Command::Final);
        REQUIRE(msg.isClientMessage());
        REQUIRE(msg.command() == Command::Final);

        auto tag = MessageFrame::static_bytes_tag{};
        msg.setTopic("/iamatopic", tag);
        msg.setServiceName("service://abc", tag);
        msg.setClientRequestId("request 1", tag);
        msg.setBody("test body test body test body test body test body test body test body", tag);
        msg.setError("fail!", tag);
        msg.setRbacToken("password", tag);

        REQUIRE(msg.isClientMessage());
        REQUIRE(msg.command() == Command::Final);
        REQUIRE(msg.topic() == "/iamatopic");
        REQUIRE(msg.serviceName() == "service://abc");
        REQUIRE(msg.clientRequestId() == "request 1");
        REQUIRE(msg.body() == "test body test body test body test body test body test body test body");
        REQUIRE(msg.error() == "fail!");
        REQUIRE(msg.rbacToken() == "password");

        REQUIRE(msg.isValid());
        REQUIRE(msg.availableFrameCount() == 8);
        REQUIRE(msg.frameAt(0).data() == "MDPC03");
        REQUIRE(msg.frameAt(1).data() == "\x4");
        REQUIRE(msg.frameAt(2).data() == "service://abc");
        REQUIRE(msg.frameAt(3).data() == "request 1");
        REQUIRE(msg.frameAt(4).data() == "/iamatopic");
        REQUIRE(msg.frameAt(5).data() == "test body test body test body test body test body test body test body");
        REQUIRE(msg.frameAt(6).data() == "fail!");
        REQUIRE(msg.frameAt(7).data() == "password");

        // make sure isValid detects command/protocol mismatches
        REQUIRE(!MdpMessage::createClientMessage(Command::Notify).isValid());
        REQUIRE(!MdpMessage::createWorkerMessage(Command::Subscribe).isValid());
        REQUIRE(!MdpMessage::createWorkerMessage(Command::Unsubscribe).isValid());
        {
            MdpMessage invalidCmd;
            invalidCmd.setFrames({ std::make_unique<std::string>("MDPC03"),
                    std::make_unique<std::string>("\x20"), // invalid
                    std::make_unique<std::string>(),
                    std::make_unique<std::string>(),
                    std::make_unique<std::string>(),
                    std::make_unique<std::string>(),
                    std::make_unique<std::string>(),
                    std::make_unique<std::string>() });
            REQUIRE(!invalidCmd.isValid());

            MdpMessage invalidProtocol;
            invalidProtocol.setFrames({ std::make_unique<std::string>("MDPC666"),
                    std::make_unique<std::string>("\x1"),
                    std::make_unique<std::string>(),
                    std::make_unique<std::string>(),
                    std::make_unique<std::string>(),
                    std::make_unique<std::string>(),
                    std::make_unique<std::string>(),
                    std::make_unique<std::string>() });
            REQUIRE(!invalidProtocol.isValid());
        }
    }
}

TEST_CASE("Test mmi.dns", "[broker][mmi][mmi_dns]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    Broker               broker("testbroker", testSettings());
    REQUIRE(broker.bind(opencmw::URI<opencmw::STRICT>("mds://127.0.0.1:22345")));

    RunInThread          brokerRun(broker);

    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    // register worker as a.service
    TestNode<MdpMessage> worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready = MdpMessage::createWorkerMessage(Command::Ready);
    ready.setServiceName("a.service", static_tag);
    ready.setTopic("http://a.service", static_tag);
    ready.setBody("API description", static_tag);
    ready.setRbacToken("rbacToken", static_tag);
    worker.send(ready);

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    { // list all entries
        auto request = MdpMessage::createClientMessage(Command::Set);
        request.setServiceName("mmi.dns", static_tag);
        request.setBody("Hello World!", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "mmi.dns");
        REQUIRE(reply->body() == "[testbroker: http://a.service,mds://127.0.0.1:22345,mds://127.0.0.1:22345/a.service,mds://127.0.0.1:22345/mmi.dns,mds://127.0.0.1:22345/mmi.echo,mds://127.0.0.1:22345/mmi.openapi,mds://127.0.0.1:22345/mmi.service]");
    }

    {
        auto request = MdpMessage::createClientMessage(Command::Set);
        request.setServiceName("mmi.dns", static_tag);

        // atm services must be prepended by "/" to form URIs that opencmw::URI can parse
        // send query with some crazy whitespace
        request.setBody(" /mmi.dns  , /a.service", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "mmi.dns");
        REQUIRE(reply->body() == "[/mmi.dns: mds://127.0.0.1:22345/mmi.dns],[/a.service: mds://127.0.0.1:22345/a.service]");
    }
}

TEST_CASE("Test mmi.service", "[broker][mmi][mmi_service]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    Broker               broker("testbroker", testSettings());
    RunInThread          brokerRun(broker);

    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    { // ask for not yet existing service
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("mmi.service", static_tag);
        request.setBody("a.service", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "mmi.service");
        REQUIRE(reply->body() == "404");
    }

    // register worker as a.service
    TestNode<MdpMessage> worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready = MdpMessage::createWorkerMessage(Command::Ready);
    ready.setServiceName("a.service", static_tag);
    ready.setBody("API description", static_tag);
    ready.setRbacToken("rbacToken", static_tag);
    worker.send(ready);

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    { // service now exists
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("mmi.service", static_tag);
        request.setBody("a.service", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "mmi.service");
        REQUIRE(reply->body() == "200");
    }

    { // list services
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("mmi.service", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "mmi.service");
        REQUIRE(reply->body() == "a.service,mmi.dns,mmi.echo,mmi.openapi,mmi.service");
    }
}

TEST_CASE("Test mmi.echo", "[broker][mmi][mmi_echo]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    Broker               broker("testbroker", testSettings());
    RunInThread          brokerRun(broker);

    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    auto request = MdpMessage::createClientMessage(Command::Get);
    request.setServiceName("mmi.echo", static_tag);
    request.setBody("Wie heisst der Buergermeister von Wesel", static_tag);
    request.setRbacToken("rbac", static_tag);

    auto toSend = request.clone();
    client.send(toSend);

    const auto reply = client.tryReadOne();

    REQUIRE(reply.has_value());
    REQUIRE(reply->isValid());
    REQUIRE(reply->isClientMessage());
    REQUIRE(reply->command() == Command::Get);
    REQUIRE(reply->serviceName() == request.serviceName());
    REQUIRE(reply->clientRequestId() == request.clientRequestId());
    REQUIRE(reply->topic() == request.topic());
    REQUIRE(reply->body() == request.body());
    REQUIRE(reply->error() == request.error());
    REQUIRE(reply->rbacToken() == request.rbacToken());
}

TEST_CASE("Test mmi.openapi", "[broker][mmi][mmi_openapi]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    Broker               broker("testbroker", testSettings());
    RunInThread          brokerRun(broker);

    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    { // request API of not yet existing service
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("mmi.openapi", static_tag);
        request.setBody("a.service", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "mmi.openapi");
        REQUIRE(reply->body() == "");
        REQUIRE(reply->error() == "Requested invalid service 'a.service'");
    }

    // register worker as a.service
    TestNode<MdpMessage> worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready = MdpMessage::createWorkerMessage(Command::Ready);
    ready.setServiceName("a.service", static_tag);
    ready.setBody("API description", static_tag);
    ready.setRbacToken("rbacToken", static_tag);
    worker.send(ready);

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    { // service now exists, API description is returned
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("mmi.openapi", static_tag);
        request.setBody("a.service", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "mmi.openapi");
        REQUIRE(reply->body() == "API description");
        REQUIRE(reply->error() == "");
    }
}

TEST_CASE("Request answered with unknown service", "[broker][unknown_service]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    const auto address = URI("inproc://testrouter");

    Broker     broker("testbroker", testSettings());

    REQUIRE(broker.bind(address, BindOption::Router));

    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(address));

    RunInThread brokerRun(broker);

    auto        request = MdpMessage::createClientMessage(Command::Get);
    request.setServiceName("no.service", static_tag);
    request.setClientRequestId("1", static_tag);
    request.setTopic("/topic", static_tag);
    request.setRbacToken("rbacToken", static_tag);
    client.send(request);

    const auto reply = client.tryReadOne();

    REQUIRE(reply.has_value());
    REQUIRE(reply->isValid());
    REQUIRE(reply->isClientMessage());
    REQUIRE(reply->command() == Command::Final);
    REQUIRE(reply->serviceName() == "no.service");
    REQUIRE(reply->clientRequestId() == "1");
    REQUIRE(reply->topic() == "/mmi.service");
    REQUIRE(reply->body().empty());
    REQUIRE(reply->error() == "unknown service (error 501): 'no.service'");
    REQUIRE(reply->rbacToken() == "RBAC=ADMIN,abcdef12345");
}

TEST_CASE("Test toZeroMQEndpoint conversion", "[utils][toZeroMQEndpoint]") {
    REQUIRE(toZeroMQEndpoint(URI("mdp://127.0.0.1:12345")) == "tcp://127.0.0.1:12345");
    REQUIRE(toZeroMQEndpoint(URI("mds://127.0.0.1:12345")) == "tcp://127.0.0.1:12345");
    REQUIRE(toZeroMQEndpoint(URI("inproc://test")) == "inproc://test");
}

TEST_CASE("Bind broker to endpoints", "[broker][bind]") {
    // the tcp/mdp/mds test cases rely on the ports being free, use wildcards/search for free ports if this turns out to be a problem
    static const std::array testcases = {
        std::tuple{ URI("tcp://127.0.0.1:22345"), BindOption::Router, std::make_optional<URI>("mdp://127.0.0.1:22345") },
        std::tuple{ URI("mdp://127.0.0.1:22346"), BindOption::Router, std::make_optional<URI>("mdp://127.0.0.1:22346") },
        std::tuple{ URI("mdp://127.0.0.1:22347"), BindOption::DetectFromURI, std::make_optional<URI>("mdp://127.0.0.1:22347") },
        std::tuple{ URI("mdp://127.0.0.1:22348"), BindOption::Router, std::make_optional<URI>("mdp://127.0.0.1:22348") },
        std::tuple{ URI("mdp://127.0.0.1:22348"), BindOption::Router, std::optional<URI>{} }, // error, already bound
        std::tuple{ URI("mds://127.0.0.1:22349"), BindOption::DetectFromURI, std::make_optional<URI>("mds://127.0.0.1:22349") },
        std::tuple{ URI("tcp://127.0.0.1:22350"), BindOption::Pub, std::make_optional<URI>("mds://127.0.0.1:22350") },
        std::tuple{ URI("inproc://bindtest"), BindOption::Router, std::make_optional<URI>("inproc://bindtest") },
        std::tuple{ URI("inproc://bindtest_pub"), BindOption::Pub, std::make_optional<URI>("inproc://bindtest_pub") },
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
    using opencmw::majordomo::MdpMessage;

    Broker               broker("testbroker", testSettings());

    TestNode<MdpMessage> worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    auto ready = MdpMessage::createWorkerMessage(Command::Ready);
    ready.setServiceName("a.service", static_tag);
    ready.setBody("API description", static_tag);
    ready.setRbacToken("rbacToken", static_tag);
    worker.send(ready);

    broker.processMessages();

    auto request = MdpMessage::createClientMessage(Command::Get);
    request.setServiceName("a.service", static_tag);
    request.setClientRequestId("1", static_tag);
    request.setTopic("/topic", static_tag);
    request.setRbacToken("rbacToken", static_tag);
    client.send(request);

    broker.processMessages();

    const auto requestAtWorker = worker.tryReadOne();
    REQUIRE(requestAtWorker.has_value());
    REQUIRE(requestAtWorker->isValid());
    REQUIRE(requestAtWorker->isWorkerMessage());
    REQUIRE(requestAtWorker->command() == Command::Get);
    REQUIRE(!requestAtWorker->clientSourceId().empty());
    REQUIRE(requestAtWorker->clientRequestId() == "1");
    REQUIRE(requestAtWorker->topic() == "/topic");
    REQUIRE(requestAtWorker->body().empty());
    REQUIRE(requestAtWorker->error().empty());
    REQUIRE(requestAtWorker->rbacToken() == "rbacToken");

    auto replyFromWorker = MdpMessage::createWorkerMessage(Command::Final);
    replyFromWorker.setClientSourceId(requestAtWorker->clientSourceId(), dynamic_tag);
    replyFromWorker.setClientRequestId("1", static_tag);
    replyFromWorker.setTopic("/topic", static_tag);
    replyFromWorker.setBody("reply body", static_tag);
    replyFromWorker.setRbacToken("rbac_worker", static_tag);
    worker.send(replyFromWorker);

    broker.processMessages();

    const auto reply = client.tryReadOne();
    REQUIRE(reply.has_value());
    REQUIRE(reply->isValid());
    REQUIRE(reply->isClientMessage());
    REQUIRE(reply->command() == Command::Final);
    REQUIRE(reply->serviceName() == "a.service");
    REQUIRE(reply->clientRequestId() == "1");
    REQUIRE(reply->topic() == "/topic");
    REQUIRE(reply->body() == "reply body");
    REQUIRE(reply->error().empty());
    REQUIRE(reply->rbacToken() == "rbac_worker");

    broker.cleanup();

    {
        const auto heartbeat = worker.tryReadOne();
        REQUIRE(heartbeat.has_value());
        REQUIRE(heartbeat->isValid());
        REQUIRE(heartbeat->isWorkerMessage());
        REQUIRE(heartbeat->command() == Command::Heartbeat);
        REQUIRE(heartbeat->serviceName() == "a.service");
        REQUIRE(heartbeat->rbacToken() == "RBAC=ADMIN,abcdef12345");
    }

    const auto disconnect = worker.tryReadOne();
    REQUIRE(disconnect.has_value());
    REQUIRE(disconnect->isValid());
    REQUIRE(disconnect->isWorkerMessage());
    REQUIRE(disconnect->command() == Command::Disconnect);
    REQUIRE(disconnect->serviceName() == "a.service");
    REQUIRE(disconnect->clientRequestId().empty());
    REQUIRE(disconnect->topic() == "/a.service");
    REQUIRE(disconnect->body() == "broker shutdown");
    REQUIRE(disconnect->error().empty());
    REQUIRE(disconnect->rbacToken() == "RBAC=ADMIN,abcdef12345");
}

TEST_CASE("Pubsub example using SUB client/DEALER worker", "[broker][pubsub_sub_dealer]") {
    using opencmw::majordomo::BindOption;
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    const auto publisherAddress = URI("inproc://testpub");

    Broker     broker("testbroker", testSettings());

    REQUIRE(broker.bind(publisherAddress, BindOption::Pub));

    TestNode<BrokerMessage> subscriber(broker.context, ZMQ_SUB);
    REQUIRE(subscriber.connect(publisherAddress, "/a.topic"));
    REQUIRE(subscriber.subscribe("/other.*"));

    broker.processMessages();

    TestNode<MdpMessage> publisher(broker.context);
    REQUIRE(publisher.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // send three notifications, two matching (one exact, one via wildcard), one not matching
    {
        auto notify = MdpMessage::createWorkerMessage(Command::Notify);
        notify.setServiceName("a.service", static_tag);
        notify.setTopic("/a.topic", static_tag);
        notify.setBody("Notification about /a.topic", static_tag);
        notify.setRbacToken("rbac_worker", static_tag);
        publisher.send(notify);
    }

    broker.processMessages();

    {
        auto notify = MdpMessage::createWorkerMessage(Command::Notify);
        notify.setServiceName("a.service", static_tag);
        notify.setTopic("/a.topic_2", static_tag);
        notify.setBody("Notification about /a.topic_2", static_tag);
        notify.setRbacToken("rbac_worker", static_tag);
        publisher.send(notify);
    }

    broker.processMessages();

    {
        auto notify = MdpMessage::createWorkerMessage(Command::Notify);
        notify.setServiceName("a.service", static_tag);
        notify.setTopic("/other.topic", static_tag);
        notify.setBody("Notification about /other.topic", static_tag);
        notify.setRbacToken("rbac_worker", static_tag);
        publisher.send(notify);
    }

    broker.processMessages();

    // receive only messages matching subscriptions

    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->sourceId() == "/a.topic");
        REQUIRE(reply->serviceName() == "a.service");
        REQUIRE(reply->clientRequestId().empty());
        REQUIRE(reply->body() == "Notification about /a.topic");
        REQUIRE(reply->error().empty());
        REQUIRE(reply->rbacToken() == "rbac_worker");
    }

    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->sourceId() == "/other.*");
        REQUIRE(reply->serviceName() == "a.service");
        REQUIRE(reply->clientRequestId().empty());
        REQUIRE(reply->body() == "Notification about /other.topic");
        REQUIRE(reply->error().empty());
        REQUIRE(reply->rbacToken() == "rbac_worker");
    }
}

TEST_CASE("Broker sends heartbeats", "[broker][heartbeat]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;
    using Clock                      = std::chrono::steady_clock;

    constexpr auto heartbeatInterval = std::chrono::milliseconds(50);

    Settings       settings;
    settings.heartbeatInterval = heartbeatInterval;
    settings.heartbeatLiveness = 3;
    Broker               broker("testbroker", settings);

    TestNode<MdpMessage> worker(broker.context);

    RunInThread          brokerRun(broker);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    {
        auto ready = MdpMessage::createWorkerMessage(Command::Ready);
        ready.setServiceName("heartbeat.service", static_tag);
        ready.setBody("API description", static_tag);
        ready.setRbacToken("rbac_worker", static_tag);
        worker.send(ready);
    }

    const auto afterReady = Clock::now();

    std::this_thread::sleep_for(heartbeatInterval * 0.75);

    {
        auto heartbeat = MdpMessage::createWorkerMessage(Command::Heartbeat);
        heartbeat.setServiceName("heartbeat.service", static_tag);
        heartbeat.setRbacToken("rbac_worker", static_tag);
        worker.send(heartbeat);
    }

    const auto heartbeat = worker.tryReadOne();
    REQUIRE(heartbeat.has_value());
    REQUIRE(heartbeat->command() == Command::Heartbeat);

    const auto afterHeartbeat = Clock::now();

    // Ensure that the broker sends a heartbeat after a "reasonable time"
    // (which is a bit more than two hb intervals, if the broker goes into polling
    // (1 hb interval duration) shortly before heartbeats would be due; plus some slack
    // for other delays
    REQUIRE(afterHeartbeat - afterReady < heartbeatInterval * 2.3);

    // As the worker is sending no more heartbeats, ensure that the broker also stops sending them,
    // i.e. that it purged us (silently). We allow two more heartbeats (liveness - 1).

    if (const auto maybeHeartbeat = worker.tryReadOne(heartbeatInterval * 2)) {
        REQUIRE(maybeHeartbeat->command() == Command::Heartbeat);

        if (const auto maybeHeartbeat2 = worker.tryReadOne(heartbeatInterval * 2)) {
            REQUIRE(maybeHeartbeat2->command() == Command::Heartbeat);
        }
    }

    REQUIRE(!worker.tryReadOne(heartbeatInterval * 2).has_value());
}

TEST_CASE("Broker disconnects on unexpected heartbeat", "[broker][unexpected_heartbeat]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    constexpr auto heartbeatInterval = std::chrono::milliseconds(50);

    Settings       settings;
    settings.heartbeatInterval = heartbeatInterval;
    Broker               broker("testbroker", settings);

    TestNode<MdpMessage> worker(broker.context);

    RunInThread          brokerRun(broker);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // send heartbeat without initial ready - invalid
    auto heartbeat = MdpMessage::createWorkerMessage(Command::Heartbeat);
    heartbeat.setServiceName("heartbeat.service", static_tag);
    heartbeat.setRbacToken("rbac_worker", static_tag);
    worker.send(heartbeat);

    const auto disconnect = worker.tryReadOne();
    REQUIRE(disconnect.has_value());
    REQUIRE(disconnect->command() == Command::Disconnect);
}

TEST_CASE("Test RBAC role priority handling", "[broker][rbac]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::Client;
    using opencmw::majordomo::MdpMessage;
    using namespace std::literals;

    // Use higher heartbeat interval so ther broker doesn't bother the worker with heartbeat messages
    opencmw::majordomo::Settings settings;
    settings.heartbeatInterval                                       = std::chrono::seconds(1);

    constexpr std::array<opencmw::rbac::RoleAndPriority, 4> roleList = { { { "ADMIN"sv, 0 }, { "USER"sv, 3 }, { "OTHER"sv, 5 }, { "ROOT"sv, 0 } } };
    Broker                                                  broker("testbroker", settings, opencmw::rbac::RoleSet(roleList));
    RunInThread                                             brokerRun(broker);

    TestNode<MdpMessage>                                    worker(broker.context);
    REQUIRE(worker.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    {
        auto ready = MdpMessage::createWorkerMessage(Command::Ready);
        ready.setServiceName("a.service", static_tag);
        ready.setBody("API description", static_tag);
        ready.setRbacToken("rbac_worker", static_tag);
        worker.send(ready);
    }

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    constexpr auto roles           = std::array{ "OTHER", "OTHER", "USER", "ROOT", "ADMIN" };

    int            clientRequestId = 0;
    for (const auto &role : roles) {
        auto msg = MdpMessage::createClientMessage(Command::Get);
        msg.setClientRequestId(std::to_string(clientRequestId++), dynamic_tag);
        msg.setServiceName("a.service", static_tag);
        msg.setRbacToken(fmt::format("RBAC={},123456abcdef", role), dynamic_tag);
        client.send(msg);
    }

    {
        // read first message but don't reply immediately, this forces the broker to queue the following requests
        const auto msg = worker.tryReadOne();
        REQUIRE(msg.has_value());
        REQUIRE(msg->clientRequestId() == "0");

        // we give the broker time to read and queue the following requests
        std::this_thread::sleep_for(settings.heartbeatInterval * 0.7);

        auto reply = MdpMessage::createWorkerMessage(Command::Final);
        reply.setClientSourceId(msg->clientSourceId(), dynamic_tag);
        reply.setClientRequestId(msg->clientRequestId(), dynamic_tag);
        reply.setBody("Hello!", static_tag);
        worker.send(reply);
    }

    // the remaining messages must have been queued in the broker and thus be reordered:
    // "ROOT", "ADMIN", "USER", "OTHER"
    std::vector<std::string> seenMessages;
    while (seenMessages.size() < roles.size() - 1) {
        const auto msg = worker.tryReadOne();
        REQUIRE(msg.has_value());
        seenMessages.push_back(std::string(msg->clientRequestId()));

        auto reply = MdpMessage::createWorkerMessage(Command::Final);
        reply.setClientSourceId(msg->clientSourceId(), dynamic_tag);
        reply.setClientRequestId(msg->clientRequestId(), dynamic_tag);
        reply.setBody("Hello!", static_tag);
        worker.send(reply);
    }

    REQUIRE(seenMessages == std::vector{ "3"s, "4"s, "2"s, "1"s });
}

TEST_CASE("pubsub example using router socket (DEALER client)", "[broker][pubsub_router]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    Broker               broker("testbroker", testSettings());

    TestNode<MdpMessage> subscriber(broker.context);
    REQUIRE(subscriber.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    TestNode<MdpMessage> publisherOne(broker.context);
    REQUIRE(publisherOne.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    TestNode<MdpMessage> publisherTwo(broker.context);
    REQUIRE(publisherTwo.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    // subscribe client to /cooking.italian
    {
        auto subscribe = MdpMessage::createClientMessage(Command::Subscribe);
        subscribe.setServiceName("first.service", static_tag);
        subscribe.setTopic("/cooking.italian", static_tag);
        subscribe.setRbacToken("rbacToken", static_tag);
        subscriber.send(subscribe);
    }

    broker.processMessages();

    // subscribe client to /cooking.indian
    {
        auto subscribe = MdpMessage::createClientMessage(Command::Subscribe);
        subscribe.setServiceName("second.service", static_tag);
        subscribe.setTopic("/cooking.indian", static_tag);
        subscribe.setRbacToken("rbacToken", static_tag);
        subscriber.send(subscribe);
    }

    broker.processMessages();

    // publisher 1 sends a notification for /cooking.italian
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("first.service", static_tag);
        pubMsg.setTopic("/cooking.italian", static_tag);
        pubMsg.setBody("Original carbonara recipe here!", static_tag);
        pubMsg.setRbacToken("rbac_worker_1", static_tag);
        publisherOne.send(pubMsg);
    }

    broker.processMessages();

    // client receives notification for /cooking.italian
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "first.service");
        REQUIRE(reply->clientRequestId().empty());
        REQUIRE(reply->topic() == "/cooking.italian");
        REQUIRE(reply->body() == "Original carbonara recipe here!");
        REQUIRE(reply->error().empty());
        REQUIRE(reply->rbacToken() == "rbac_worker_1");
    }

    // publisher 2 sends a notification for /cooking.indian
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("second.service", static_tag);
        pubMsg.setTopic("/cooking.indian", static_tag);
        pubMsg.setBody("Try our Chicken Korma!", static_tag);
        pubMsg.setRbacToken("rbac_worker_2", static_tag);
        publisherTwo.send(pubMsg);
    }

    broker.processMessages();

    // client receives notification for /cooking.indian
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "second.service");
        REQUIRE(reply->clientRequestId().empty());
        REQUIRE(reply->topic() == "/cooking.indian");
        REQUIRE(reply->body() == "Try our Chicken Korma!");
        REQUIRE(reply->error().empty());
        REQUIRE(reply->rbacToken() == "rbac_worker_2");
    }

    // unsubscribe client from /cooking.italian
    {
        auto unsubscribe = MdpMessage::createClientMessage(Command::Unsubscribe);
        unsubscribe.setTopic("/cooking.italian", static_tag);
        unsubscribe.setRbacToken("rbacToken", static_tag);
        subscriber.send(unsubscribe);
    }

    broker.processMessages();

    // publisher 1 sends a notification for /cooking.italian
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("first.service", static_tag);
        pubMsg.setTopic("/cooking.italian", static_tag);
        pubMsg.setBody("The best Margherita in town!", static_tag);
        pubMsg.setRbacToken("rbac_worker_1", static_tag);
        publisherOne.send(pubMsg);
    }

    broker.processMessages();

    // publisher 2 sends a notification for /cooking.indian
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("second.service", static_tag);
        pubMsg.setTopic("/cooking.indian", static_tag);
        pubMsg.setBody("Sizzling tikkas in our Restaurant!", static_tag);
        pubMsg.setRbacToken("rbac_worker_2", static_tag);
        publisherTwo.send(pubMsg);
    }

    broker.processMessages();

    // verify that the client receives only the notification from publisher 2

    {
        const auto reply = subscriber.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "second.service");
        REQUIRE(reply->clientRequestId().empty());
        REQUIRE(reply->topic() == "/cooking.indian");
        REQUIRE(reply->body() == "Sizzling tikkas in our Restaurant!");
        REQUIRE(reply->error().empty());
        REQUIRE(reply->rbacToken() == "rbac_worker_2");
    }
}

TEST_CASE("pubsub example using PUB socket (SUB client)", "[broker][pubsub_subclient]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    Broker                  broker("testbroker", testSettings());

    TestNode<BrokerMessage> subscriber(broker.context, ZMQ_SUB);
    REQUIRE(subscriber.connect(opencmw::majordomo::INTERNAL_ADDRESS_PUBLISHER));

    TestNode<MdpMessage> publisherOne(broker.context);
    REQUIRE(publisherOne.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    TestNode<MdpMessage> publisherTwo(broker.context);
    REQUIRE(publisherTwo.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    subscriber.subscribe("/cooking.italian*");

    broker.processMessages();

    subscriber.subscribe("/cooking.indian*");

    broker.processMessages();

    // publisher 1 sends a notification for /cooking.italian.pasta
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("first.service", static_tag);
        pubMsg.setTopic("/cooking.italian.pasta", static_tag);
        pubMsg.setBody("Original carbonara recipe here!", static_tag);
        pubMsg.setRbacToken("rbac_worker_1", static_tag);
        publisherOne.send(pubMsg);
    }

    broker.processMessages();

    // client receives notification for /cooking.italian*
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->sourceId() == "/cooking.italian*");
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "first.service");
        REQUIRE(reply->clientRequestId().empty());
        REQUIRE(reply->topic() == "/cooking.italian.pasta");
        REQUIRE(reply->body() == "Original carbonara recipe here!");
        REQUIRE(reply->error().empty());
        REQUIRE(reply->rbacToken() == "rbac_worker_1");
    }

    // publisher 2 sends a notification for /cooking.indian.chicken
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("second.service", static_tag);
        pubMsg.setTopic("/cooking.indian.chicken", static_tag);
        pubMsg.setBody("Try our Chicken Korma!", static_tag);
        pubMsg.setRbacToken("rbac_worker_2", static_tag);
        publisherTwo.send(pubMsg);
    }

    broker.processMessages();

    // client receives notification for /cooking.indian*
    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->sourceId() == "/cooking.indian*");
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "second.service");
        REQUIRE(reply->clientRequestId().empty());
        REQUIRE(reply->topic() == "/cooking.indian.chicken");
        REQUIRE(reply->body() == "Try our Chicken Korma!");
        REQUIRE(reply->error().empty());
        REQUIRE(reply->rbacToken() == "rbac_worker_2");
    }

    subscriber.unsubscribe("/cooking.italian*");

    broker.processMessages();

    // publisher 1 sends a notification for /cooking.italian.pizza
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("first.service", static_tag);
        pubMsg.setTopic("/cooking.italian.pizza", static_tag);
        pubMsg.setBody("The best Margherita in town!", static_tag);
        pubMsg.setRbacToken("rbac_worker_1", static_tag);
        publisherOne.send(pubMsg);
    }

    broker.processMessages();

    // publisher 2 sends a notification for /cooking.indian.tikkas
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("second.service", static_tag);
        pubMsg.setTopic("/cooking.indian.tikkas", static_tag);
        pubMsg.setBody("Sizzling tikkas in our Restaurant!", static_tag);
        pubMsg.setRbacToken("rbac_worker_2", static_tag);
        publisherTwo.send(pubMsg);
    }

    broker.processMessages();

    // verify that the client receives only the notification from publisher 2

    {
        const auto reply = subscriber.tryReadOne();
        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->sourceId() == "/cooking.indian*");
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->serviceName() == "second.service");
        REQUIRE(reply->clientRequestId().empty());
        REQUIRE(reply->topic() == "/cooking.indian.tikkas");
        REQUIRE(reply->body() == "Sizzling tikkas in our Restaurant!");
        REQUIRE(reply->error().empty());
        REQUIRE(reply->rbacToken() == "rbac_worker_2");
    }
}

using opencmw::majordomo::MdpMessage;

class TestIntHandler {
    int _x = 10;

public:
    explicit TestIntHandler(int initialValue)
        : _x(initialValue) {
    }

    void operator()(RequestContext &context) {
        if (context.request.command() == Command::Get) {
            context.reply.setBody(std::to_string(_x), MessageFrame::dynamic_bytes_tag{});
            return;
        }

        assert(context.request.command() == Command::Set);

        const auto request = context.request.body();
        int        value   = 0;
        const auto result  = std::from_chars(request.begin(), request.end(), value);

        if (result.ec == std::errc::invalid_argument) {
            context.reply.setError("Not a valid int", MessageFrame::static_bytes_tag{});
        } else {
            _x = value;
            context.reply.setBody("Value set. All good!", MessageFrame::static_bytes_tag{});
        }
    }
};

class NonCopyableMovableHandler {
public:
    NonCopyableMovableHandler()                                  = default;
    ~NonCopyableMovableHandler()                                 = default;
    NonCopyableMovableHandler(const NonCopyableMovableHandler &) = delete;
    NonCopyableMovableHandler &operator=(const NonCopyableMovableHandler &) = delete;
    NonCopyableMovableHandler(NonCopyableMovableHandler &&) noexcept        = default;
    NonCopyableMovableHandler &operator=(NonCopyableMovableHandler &&) noexcept = default;

    void                       operator()(RequestContext &) {}
};

TEST_CASE("BasicMdpWorker instantiation", "[worker][instantiation]") {
    // ensure that BasicMdpWorker can be instantiated with lvalue and rvalue handlers
    // lvalues should be used via reference, rvalues moved
    Broker                    broker("testbroker", testSettings());
    NonCopyableMovableHandler handler;

    BasicMdpWorker            worker1("a.service", broker, NonCopyableMovableHandler());
    BasicMdpWorker            worker2("a.service", broker, handler);
    Context                   context;
    BasicMdpWorker            worker5("a.service", INTERNAL_ADDRESS_BROKER, NonCopyableMovableHandler(), context, testSettings());
    BasicMdpWorker            worker6("a.service", INTERNAL_ADDRESS_BROKER, handler, context, testSettings());
}

TEST_CASE("BasicMdpWorker connects to non-existing broker", "[worker]") {
    const Context  context;
    BasicMdpWorker worker("a.service", URI("inproc:/doesnotexist"), TestIntHandler(10), context);
    worker.run(); // returns immediately on connection failure
}

TEST_CASE("BasicMdpWorker run loop quits when broker quits", "[worker]") {
    const Context  context;
    Broker         broker("testbroker", testSettings());
    BasicMdpWorker worker("a.service", broker, TestIntHandler(10));

    RunInThread    brokerRun(broker);

    auto           quitBroker = std::jthread([&broker]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        broker.shutdown();
              });

    worker.run(); // returns when broker disappears
    quitBroker.join();
}

TEST_CASE("BasicMdpWorker connection basics", "[worker][basic_worker_connection]") {
    const Context           context;
    TestNode<BrokerMessage> brokerRouter(context, ZMQ_ROUTER);
    TestNode<BrokerMessage> brokerPub(context, ZMQ_PUB);
    const auto              brokerAddress = opencmw::URI<opencmw::STRICT>("inproc://test/");
    const auto              routerAddress = opencmw::URI<opencmw::STRICT>::factory(brokerAddress).path(opencmw::majordomo::SUFFIX_ROUTER).build();
    const auto              pubAddress    = opencmw::URI<opencmw::STRICT>::factory(brokerAddress).path(opencmw::majordomo::SUFFIX_SUBSCRIBE).build();
    REQUIRE(brokerRouter.bind(routerAddress));
    REQUIRE(brokerPub.bind(pubAddress));
    Settings settings;
    settings.heartbeatInterval       = std::chrono::milliseconds(200);
    settings.heartbeatLiveness       = 2;
    settings.workerReconnectInterval = std::chrono::milliseconds(200);

    BasicMdpWorker worker(
            "a.service", routerAddress, [](RequestContext &) {}, context, settings);
    RunInThread workerRun(worker);

    std::string workerId;

    // worker sends initial READY message
    {
        const auto ready = brokerRouter.tryReadOne();
        REQUIRE(ready.has_value());
        REQUIRE(ready->isValid());
        REQUIRE(ready->command() == Command::Ready);
        REQUIRE(ready->serviceName() == "a.service");
        workerId = ready->sourceId();
    }

    // worker must send a heartbeat
    {
        const auto heartbeat = brokerRouter.tryReadOne(settings.heartbeatInterval * 23 / 10);
        REQUIRE(heartbeat.has_value());
        REQUIRE(heartbeat->isValid());
        REQUIRE(heartbeat->command() == Command::Heartbeat);
        REQUIRE(heartbeat->serviceName() == "a.service");
        REQUIRE(heartbeat->sourceId() == workerId);
    }

    // not receiving heartbeats, the worker reconnects and sends a new READY message
    {
        const auto ready = brokerRouter.tryReadOne(settings.heartbeatInterval * (settings.heartbeatLiveness + 1) + settings.workerReconnectInterval);
        REQUIRE(ready.has_value());
        REQUIRE(ready->isValid());
        REQUIRE(ready->command() == Command::Ready);
        REQUIRE(ready->serviceName() == "a.service");
        workerId = ready->sourceId();
    }

    // send heartbeat to worker
    {
        auto heartbeat = BrokerMessage::createWorkerMessage(Command::Heartbeat);
        heartbeat.setSourceId(workerId, dynamic_tag);
        brokerRouter.send(heartbeat);
    }

    worker.shutdown();

    // worker sends a DISCONNECT on shutdown
    {
        const auto disconnect = brokerRouter.tryReadOne();
        REQUIRE(disconnect.has_value());
        REQUIRE(disconnect->isValid());
        REQUIRE(disconnect->command() == Command::Disconnect);
        REQUIRE(disconnect->serviceName() == "a.service");
        REQUIRE(disconnect->sourceId() == workerId);
    }
}

TEST_CASE("SET/GET example using the BasicMdpWorker class", "[worker][getset_basic_worker]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    Broker         broker("testbroker", testSettings());

    BasicMdpWorker worker("a.service", broker, TestIntHandler(10));
    worker.setServiceDescription("API description");
    worker.setRbacRole("rbacToken");

    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    // until the worker's READY is processed by the broker, it will return
    // an "unknown service" error, retry until we get the expected reply
    bool replyReceived = false;
    while (!replyReceived) {
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("a.service", static_tag);
        request.setClientRequestId("1", static_tag);
        request.setTopic("/topic", static_tag);
        request.setRbacToken("rbacToken", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->clientRequestId() == "1");

        if (!reply->error().empty()) {
            REQUIRE(reply->error().find("error 501") != std::string_view::npos);
        } else {
            REQUIRE(reply->serviceName() == "a.service");
            REQUIRE(reply->topic() == "/topic");
            REQUIRE(reply->body() == "10");
            REQUIRE(reply->error().empty());
            REQUIRE(reply->rbacToken() == "rbacToken");
            replyReceived = true;
        }
    }

    {
        auto request = MdpMessage::createClientMessage(Command::Set);
        request.setServiceName("a.service", static_tag);
        request.setClientRequestId("2", static_tag);
        request.setTopic("/topic", static_tag);
        request.setBody("42", static_tag);
        request.setRbacToken("rbacToken", static_tag);

        client.send(request);

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->clientRequestId() == "2");
        REQUIRE(reply->body() == "Value set. All good!");
        REQUIRE(reply->error().empty());
    }

    {
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("a.service", static_tag);
        request.setClientRequestId("3", static_tag);
        request.setTopic("/topic", static_tag);
        request.setRbacToken("rbacToken", static_tag);
        client.send(request);

        const auto reply = client.tryReadOne();

        REQUIRE(reply.has_value());
        REQUIRE(reply->isValid());
        REQUIRE(reply->isClientMessage());
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->clientRequestId() == "3");
        REQUIRE(reply->topic() == "/topic");
        REQUIRE(reply->body() == "42");
        REQUIRE(reply->error().empty());
    }
}

TEST_CASE("NOTIFY example using the BasicMdpWorker class", "[worker][notify_basic_worker]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    Broker         broker("testbroker", testSettings());

    BasicMdpWorker worker("beverages", broker, TestIntHandler(10));
    worker.setServiceDescription("API description");
    worker.setRbacRole("rbacToken");

    TestNode<BrokerMessage> client(broker.context, ZMQ_XSUB);
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
            MdpMessage notify;
            notify.setTopic("/beer.time", static_tag);
            notify.setBody("Have a beer", static_tag);
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            const auto notification = client.tryReadOne(std::chrono::milliseconds(20));
            if (notification && notification->serviceName() != "mmi.service") {
                seenNotification = true;
                REQUIRE(notification->isValid());
                REQUIRE(notification->isClientMessage());
                REQUIRE(notification->command() == Command::Final);
                REQUIRE(notification->sourceId() == "/beer*");
                REQUIRE(notification->topic() == "/beer.time");
                REQUIRE(notification->body() == "Have a beer");
            }
        }
    }

    {
        MdpMessage notify;
        notify.setTopic("/beer.error", static_tag);
        notify.setError("Fridge empty!", static_tag);
        REQUIRE(worker.notify(std::move(notify)));
    }

    bool seenError = false;
    while (!seenError) {
        const auto notification = client.tryReadOne(std::chrono::milliseconds(20));
        if (!notification)
            continue;

        // there might be extra messages from above, ignore them
        if (notification->topic() == "/beer.time") {
            continue;
        }

        REQUIRE(notification->isValid());
        REQUIRE(notification->isClientMessage());
        REQUIRE(notification->command() == Command::Final);
        REQUIRE(notification->sourceId() == "/beer*");
        REQUIRE(notification->topic() == "/beer.error");
        REQUIRE(notification->error() == "Fridge empty!");
        seenError = true;
    }

    {
        // as the subscribe for wine* was sent before the beer* one, this should be
        // race-free now (as know the beer* subscribe was processed by everyone)
        MdpMessage notify;
        notify.setTopic("/wine.italian", static_tag);
        notify.setBody("Try our Chianti!", static_tag);
        REQUIRE(worker.notify(std::move(notify)));
    }

    {
        const auto notification = client.tryReadOne();
        REQUIRE(notification.has_value());
        REQUIRE(notification->isValid());
        REQUIRE(notification->isClientMessage());
        REQUIRE(notification->command() == Command::Final);
        REQUIRE(notification->sourceId() == "/wine*");
        REQUIRE(notification->topic() == "/wine.italian");
        REQUIRE(notification->body() == "Try our Chianti!");
    }

    // unsubscribe from /beer*
    REQUIRE(client.sendRawFrame("\x0/beer*"s));

    // loop until we get two consecutive messages about wine, it means that the beer unsubscribe was processed
    while (true) {
        {
            MdpMessage notify;
            notify.setTopic("/wine.portuguese", static_tag);
            notify.setBody("New Vinho Verde arrived.", static_tag);
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            MdpMessage notify;
            notify.setTopic("/beer.offer", static_tag);
            notify.setBody("Get our pilsner now!", static_tag);
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            MdpMessage notify;
            notify.setTopic("/wine.portuguese", static_tag);
            notify.setBody("New Vinho Verde arrived.", static_tag);
            REQUIRE(worker.notify(std::move(notify)));
        }

        const auto msg1 = client.tryReadOne();
        REQUIRE(msg1.has_value());
        REQUIRE(msg1->sourceId() == "/wine*");

        const auto msg2 = client.tryReadOne();
        REQUIRE(msg2.has_value());
        if (msg2->sourceId() == "/wine*") {
            break;
        }

        REQUIRE(msg2->sourceId() == "/beer*");

        const auto msg3 = client.tryReadOne();
        REQUIRE(msg3.has_value());
        REQUIRE(msg3->sourceId() == "/wine*");
    }
}

TEST_CASE("NOTIFY example using the BasicMdpWorker class (via ROUTER socket)", "[worker][notify_basic_worker_router]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    Broker         broker("testbroker", testSettings());

    BasicMdpWorker worker("beverages", broker, TestIntHandler(10));
    worker.setServiceDescription("API description");
    worker.setRbacRole("rbacToken");

    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    {
        auto subscribe = MdpMessage::createClientMessage(Command::Subscribe);
        subscribe.setServiceName("beverages", static_tag);
        subscribe.setTopic("/wine", static_tag);
        client.send(subscribe);
    }
    {
        auto subscribe = MdpMessage::createClientMessage(Command::Subscribe);
        subscribe.setServiceName("beverages", static_tag);
        subscribe.setTopic("/beer", static_tag);
        client.send(subscribe);
    }

    bool seenNotification = false;

    // we have a potential race here: the worker might not have processed the
    // subscribe yet and thus discarding the notification. Send notifications
    // in a loop until one gets through.
    while (!seenNotification) {
        {
            MdpMessage notify;
            notify.setTopic("/beer", static_tag);
            notify.setBody("Have a beer", static_tag);
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            const auto notification = client.tryReadOne(std::chrono::milliseconds(20));
            if (notification && notification->serviceName() != "mmi.service") {
                seenNotification = true;
                REQUIRE(notification->isValid());
                REQUIRE(notification->isClientMessage());
                REQUIRE(notification->command() == Command::Final);
                REQUIRE(notification->topic() == "/beer");
                REQUIRE(notification->body() == "Have a beer");
            }
        }
    }

    {
        // as the subscribe for /wine was sent before the /beer one, this should be
        // race-free now (as know the /beer subscribe was processed by everyone)
        MdpMessage notify;
        notify.setTopic("/wine", static_tag);
        notify.setBody("Try our Chianti!", static_tag);
        REQUIRE(worker.notify(std::move(notify)));
    }

    {
        const auto notification = client.tryReadOne();
        REQUIRE(notification.has_value());
        REQUIRE(notification->isValid());
        REQUIRE(notification->isClientMessage());
        REQUIRE(notification->command() == Command::Final);
        REQUIRE(notification->topic() == "/wine");
        REQUIRE(notification->body() == "Try our Chianti!");
    }

    // unsubscribe from /beer
    {
        auto unsubscribe = MdpMessage::createClientMessage(Command::Unsubscribe);
        unsubscribe.setServiceName("beverages", static_tag);
        unsubscribe.setTopic("/beer", static_tag);
        client.send(unsubscribe);
    }

    // loop until we get two consecutive messages about wine, it means that the beer unsubscribe was processed
    while (true) {
        {
            MdpMessage notify;
            notify.setTopic("/wine", static_tag);
            notify.setBody("New Vinho Verde arrived.", static_tag);
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            MdpMessage notify;
            notify.setTopic("/beer", static_tag);
            notify.setBody("Get our pilsner now!", static_tag);
            REQUIRE(worker.notify(std::move(notify)));
        }
        {
            MdpMessage notify;
            notify.setTopic("/wine", static_tag);
            notify.setBody("New Vinho Verde arrived.", static_tag);
            REQUIRE(worker.notify(std::move(notify)));
        }

        const auto msg1 = client.tryReadOne();
        REQUIRE(msg1.has_value());
        REQUIRE(msg1->topic() == "/wine");

        const auto msg2 = client.tryReadOne();
        REQUIRE(msg2.has_value());
        if (msg2->topic() == "/wine") {
            break;
        }

        REQUIRE(msg2->topic() == "/beer");
        const auto msg3 = client.tryReadOne();
        REQUIRE(msg3.has_value());
        REQUIRE(msg3->topic() == "/wine");
    }
}

TEST_CASE("SET/GET example using a lambda as the worker's request handler", "[worker][lambda_handler]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::Client;
    using opencmw::majordomo::MdpMessage;

    Broker broker("testbroker", testSettings());

    auto   handleInt = [](RequestContext &requestContext) {
        static int value = 100;
        if (requestContext.request.command() == Command::Get) {
            requestContext.reply.setBody(std::to_string(value), MessageFrame::dynamic_bytes_tag{});
            return;
        }

        assert(requestContext.request.command() == Command::Set);

        const auto request     = requestContext.request.body();
        int        parsedValue = 0;
        const auto result      = std::from_chars(request.begin(), request.end(), parsedValue);

        if (result.ec == std::errc::invalid_argument) {
            requestContext.reply.setError("Not a valid int", MessageFrame::static_bytes_tag{});
        } else {
            value = parsedValue;
            requestContext.reply.setBody("Value set. All good!", MessageFrame::static_bytes_tag{});
        }
    };

    BasicMdpWorker worker("a.service", broker, std::move(handleInt));
    worker.setServiceDescription("API description");
    worker.setRbacRole("rbacToken");

    Client client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    client.get("a.service", "", [](auto &&message) {
        REQUIRE(message.body() == "100");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));

    client.set("a.service", "42", [](auto &&message) {
        REQUIRE(message.body() == "Value set. All good!");
        REQUIRE(message.error().empty());
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));

    client.get("a.service", "", [](auto &&message) {
        REQUIRE(message.body() == "42");
        REQUIRE(message.error().empty());
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));
}

TEST_CASE("Worker's request handler throws an exception", "[worker][handler_exception]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::Client;
    using opencmw::majordomo::MdpMessage;

    Broker broker("testbroker", testSettings());

    auto   handleRequest = [](RequestContext &) {
        throw std::runtime_error("Something went wrong!");
    };

    BasicMdpWorker worker("a.service", broker, std::move(handleRequest));
    worker.setServiceDescription("API description");
    worker.setRbacRole("rbacToken");

    Client client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    client.get("a.service", "", [](auto &&message) {
        REQUIRE(message.error() == "Caught exception for service 'a.service'\nrequest message: \nexception: Something went wrong!");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));
}

TEST_CASE("Worker's request handler throws an unexpected exception", "[worker][handler_unexpected_exception]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::Client;
    using opencmw::majordomo::MdpMessage;

    Broker broker("testbroker", testSettings());

    auto   handleRequest = [](RequestContext &) {
        throw std::string("Something went wrong!");
    };

    BasicMdpWorker worker("a.service", broker, std::move(handleRequest));
    worker.setServiceDescription("API description");
    worker.setRbacRole("rbacToken");

    Client client(broker.context);
    REQUIRE(client.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER));

    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "a.service"));

    client.get("a.service", "", [](auto &&message) {
        REQUIRE(message.error() == "Caught unexpected exception for service 'a.service'\nrequest message: ");
    });

    REQUIRE(client.tryRead(std::chrono::seconds(3)));
}
