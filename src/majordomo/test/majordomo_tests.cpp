#include "helpers.hpp"

#include <majordomo/BasicMdpWorker.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Client.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/Utils.hpp>

#include <catch2/catch.hpp>
#include <fmt/format.h>

#include <charconv>
#include <cstdlib>
#include <deque>
#include <thread>

using namespace opencmw::majordomo;
using URI = opencmw::URI<>;

static opencmw::majordomo::Settings testSettings() {
    Settings settings;
    settings.heartbeatInterval = std::chrono::milliseconds(100);
    return settings;
}

template<typename MessageType>
class TestNode {
    std::deque<MessageType> _receivedMessages;

public:
    Socket _socket;

    explicit TestNode(const Context &context, int socket_type = ZMQ_DEALER)
        : _socket(context, socket_type) {
    }

    bool connect(const URI &address, std::string_view subscription = "") {
        auto result = zmq_invoke(zmq_connect, _socket, toZeroMQEndpoint(address).data());
        if (!result) return false;

        if (!subscription.empty()) {
            return subscribe(subscription);
        }

        return true;
    }

    bool subscribe(std::string_view subscription) {
        assert(!subscription.empty());
        return zmq_invoke(zmq_setsockopt, _socket, ZMQ_SUBSCRIBE, subscription.data(), subscription.size()).isValid();
    }

    bool unsubscribe(std::string_view subscription) {
        assert(!subscription.empty());
        return zmq_invoke(zmq_setsockopt, _socket, ZMQ_UNSUBSCRIBE, subscription.data(), subscription.size()).isValid();
    }

    MessageType readOne() {
        while (_receivedMessages.empty()) {
            auto message = MessageType::receive(_socket);
            if (message) {
                _receivedMessages.emplace_back(std::move(*message));
            }
        }

        assert(!_receivedMessages.empty());
        auto msg = std::move(_receivedMessages.front());
        _receivedMessages.pop_front();
        return msg;
    }

    std::optional<MessageType> tryReadOne(std::chrono::milliseconds timeout) {
        assert(_receivedMessages.empty());

        std::array<zmq_pollitem_t, 1> pollerItems;
        pollerItems[0].socket = _socket.zmq_ptr;
        pollerItems[0].events = ZMQ_POLLIN;

        const auto result     = zmq_invoke(zmq_poll, pollerItems.data(), static_cast<int>(pollerItems.size()), timeout.count());
        if (!result.isValid())
            return {};

        return MessageType::receive(_socket);
    }

    void send(MdpMessage &message) {
        message.send(_socket).assertSuccess();
    }
};

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

TEST_CASE("Request answered with unknown service", "[broker][unknown_service]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    const auto address = URI("inproc://testrouter");

    Broker     broker("testbroker", testSettings());

    REQUIRE(broker.bind(address, Broker::BindOption::Router));

    TestNode<MdpMessage> client(broker.context);
    REQUIRE(client.connect(address));

    RunInThread brokerRun(broker);

    auto        request = MdpMessage::createClientMessage(Command::Get);
    request.setServiceName("no.service", static_tag);
    request.setClientRequestId("1", static_tag);
    request.setTopic("/topic", static_tag);
    request.setRbacToken("rbacToken", static_tag);
    client.send(request);

    const auto reply = client.readOne();

    REQUIRE(reply.isValid());
    REQUIRE(reply.isClientMessage());
    REQUIRE(reply.command() == Command::Final);
    REQUIRE(reply.serviceName() == "no.service");
    REQUIRE(reply.clientRequestId() == "1");
    REQUIRE(reply.topic() == "mmi.service");
    REQUIRE(reply.body().empty());
    REQUIRE(reply.error() == "unknown service (error 501): 'no.service'");
    REQUIRE(reply.rbacToken() == "TODO (RBAC)");
}

TEST_CASE("Test toZeroMQEndpoint conversion", "[utils][toZeroMQEndpoint]") {
    REQUIRE(toZeroMQEndpoint(URI("mdp://127.0.0.1:12345")) == "tcp://127.0.0.1:12345");
    REQUIRE(toZeroMQEndpoint(URI("mds://127.0.0.1:12345")) == "tcp://127.0.0.1:12345");
    REQUIRE(toZeroMQEndpoint(URI("inproc://test")) == "inproc://test");
}

TEST_CASE("Bind broker to endpoints", "[broker][bind]") {
    // the tcp/mdp/mds test cases rely on the ports being free, use wildcards/search for free ports if this turns out to be a problem
    static const std::array testcases = {
        std::tuple{ URI("tcp://127.0.0.1:22345"), Broker::BindOption::Router, std::make_optional<URI>("mdp://127.0.0.1:22345") },
        std::tuple{ URI("mdp://127.0.0.1:22346"), Broker::BindOption::Router, std::make_optional<URI>("mdp://127.0.0.1:22346") },
        std::tuple{ URI("mdp://127.0.0.1:22347"), Broker::BindOption::DetectFromURI, std::make_optional<URI>("mdp://127.0.0.1:22347") },
        std::tuple{ URI("mdp://127.0.0.1:22348"), Broker::BindOption::Router, std::make_optional<URI>("mdp://127.0.0.1:22348") },
        std::tuple{ URI("mdp://127.0.0.1:22348"), Broker::BindOption::Router, std::optional<URI>{} }, // error, already bound
        std::tuple{ URI("mds://127.0.0.1:22349"), Broker::BindOption::DetectFromURI, std::make_optional<URI>("mds://127.0.0.1:22349") },
        std::tuple{ URI("tcp://127.0.0.1:22350"), Broker::BindOption::Pub, std::make_optional<URI>("mds://127.0.0.1:22350") },
        std::tuple{ URI("inproc://bindtest"), Broker::BindOption::Router, std::make_optional<URI>("inproc://bindtest") },
        std::tuple{ URI("inproc://bindtest_pub"), Broker::BindOption::Pub, std::make_optional<URI>("inproc://bindtest_pub") },
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

    broker.processOneMessage();

    auto request = MdpMessage::createClientMessage(Command::Get);
    request.setServiceName("a.service", static_tag);
    request.setClientRequestId("1", static_tag);
    request.setTopic("/topic", static_tag);
    request.setRbacToken("rbacToken", static_tag);
    client.send(request);

    broker.processOneMessage();

    const auto requestAtWorker = worker.readOne();
    REQUIRE(requestAtWorker.isValid());
    REQUIRE(requestAtWorker.isWorkerMessage());
    REQUIRE(requestAtWorker.command() == Command::Get);
    REQUIRE(!requestAtWorker.clientSourceId().empty());
    REQUIRE(requestAtWorker.clientRequestId() == "1");
    REQUIRE(requestAtWorker.topic() == "/topic");
    REQUIRE(requestAtWorker.body().empty());
    REQUIRE(requestAtWorker.error().empty());
    REQUIRE(requestAtWorker.rbacToken() == "rbacToken");

    auto replyFromWorker = MdpMessage::createWorkerMessage(Command::Final);
    replyFromWorker.setClientSourceId(requestAtWorker.clientSourceId(), dynamic_tag);
    replyFromWorker.setClientRequestId("1", static_tag);
    replyFromWorker.setTopic("/topic", static_tag);
    replyFromWorker.setBody("reply body", static_tag);
    replyFromWorker.setRbacToken("rbac_worker", static_tag);
    worker.send(replyFromWorker);

    broker.processOneMessage();

    const auto reply = client.readOne();
    REQUIRE(reply.isValid());
    REQUIRE(reply.isClientMessage());
    REQUIRE(reply.command() == Command::Final);
    REQUIRE(reply.serviceName() == "a.service");
    REQUIRE(reply.clientRequestId() == "1");
    REQUIRE(reply.topic() == "/topic");
    REQUIRE(reply.body() == "reply body");
    REQUIRE(reply.error().empty());
    REQUIRE(reply.rbacToken() == "rbac_worker");

    broker.cleanup();

    {
        const auto heartbeat = worker.readOne();
        REQUIRE(heartbeat.isValid());
        REQUIRE(heartbeat.isWorkerMessage());
        REQUIRE(heartbeat.command() == Command::Heartbeat);
        REQUIRE(heartbeat.serviceName() == "a.service");
        REQUIRE(heartbeat.rbacToken() == "TODO (RBAC)");
    }

    const auto disconnect = worker.readOne();
    REQUIRE(disconnect.isValid());
    REQUIRE(disconnect.isWorkerMessage());
    REQUIRE(disconnect.command() == Command::Disconnect);
    REQUIRE(disconnect.serviceName() == "a.service");
    REQUIRE(disconnect.clientRequestId().empty());
    REQUIRE(disconnect.topic() == "a.service");
    REQUIRE(disconnect.body() == "broker shutdown");
    REQUIRE(disconnect.error().empty());
    REQUIRE(disconnect.rbacToken() == "TODO (RBAC)");
}

TEST_CASE("Pubsub example using SUB client/DEALER worker", "[broker][pubsub_sub_dealer]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    const auto publisherAddress = URI("inproc://testpub");

    Broker     broker("testbroker", testSettings());

    REQUIRE(broker.bind(publisherAddress, Broker::BindOption::Pub));

    TestNode<BrokerMessage> subscriber(broker.context, ZMQ_SUB);
    REQUIRE(subscriber.connect(publisherAddress, "/a.topic"));
    REQUIRE(subscriber.subscribe("/other.*"));

    broker.processOneMessage();

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

    broker.processOneMessage();

    {
        auto notify = MdpMessage::createWorkerMessage(Command::Notify);
        notify.setServiceName("a.service", static_tag);
        notify.setTopic("/a.topic_2", static_tag);
        notify.setBody("Notification about /a.topic_2", static_tag);
        notify.setRbacToken("rbac_worker", static_tag);
        publisher.send(notify);
    }

    broker.processOneMessage();

    {
        auto notify = MdpMessage::createWorkerMessage(Command::Notify);
        notify.setServiceName("a.service", static_tag);
        notify.setTopic("/other.topic", static_tag);
        notify.setBody("Notification about /other.topic", static_tag);
        notify.setRbacToken("rbac_worker", static_tag);
        publisher.send(notify);
    }

    broker.processOneMessage();

    // receive only messages matching subscriptions

    {
        const auto reply = subscriber.readOne();
        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.sourceId() == "/a.topic");
        REQUIRE(reply.serviceName() == "a.service");
        REQUIRE(reply.clientRequestId().empty());
        REQUIRE(reply.body() == "Notification about /a.topic");
        REQUIRE(reply.error().empty());
        REQUIRE(reply.rbacToken() == "rbac_worker");
    }

    {
        const auto reply = subscriber.readOne();
        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.sourceId() == "/other.*");
        REQUIRE(reply.serviceName() == "a.service");
        REQUIRE(reply.clientRequestId().empty());
        REQUIRE(reply.body() == "Notification about /other.topic");
        REQUIRE(reply.error().empty());
        REQUIRE(reply.rbacToken() == "rbac_worker");
    }
}

TEST_CASE("Broker sends heartbeats", "[broker][heartbeat]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;
    using Clock                      = std::chrono::steady_clock;

    constexpr auto heartbeatInterval = std::chrono::milliseconds(50);

    Settings       settings;
    settings.heartbeatInterval = heartbeatInterval;
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

    const auto heartbeat = worker.readOne();
    REQUIRE(heartbeat.command() == Command::Heartbeat);

    const auto afterHeartbeat = Clock::now();

    // Ensure that the broker sends a heartbeat after a "reasonable time"
    REQUIRE(afterHeartbeat - afterReady < heartbeatInterval * 2);

    // As the worker is sending no more heartbeats, ensure that the broker also stops sending them,
    // i.e. that it purged us (silently). We allow one more heartbeat.
    const auto maybeHeartbeat = worker.tryReadOne(heartbeatInterval * 2);
    REQUIRE((maybeHeartbeat.has_value() || maybeHeartbeat->command() == Command::Heartbeat));
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

    const auto disconnect = worker.readOne();
    REQUIRE(disconnect.command() == Command::Disconnect);
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

    broker.processOneMessage();

    // subscribe client to /cooking.indian
    {
        auto subscribe = MdpMessage::createClientMessage(Command::Subscribe);
        subscribe.setServiceName("second.service", static_tag);
        subscribe.setTopic("/cooking.indian", static_tag);
        subscribe.setRbacToken("rbacToken", static_tag);
        subscriber.send(subscribe);
    }

    broker.processOneMessage();

    // publisher 1 sends a notification for /cooking.italian
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("first.service", static_tag);
        pubMsg.setTopic("/cooking.italian", static_tag);
        pubMsg.setBody("Original carbonara recipe here!", static_tag);
        pubMsg.setRbacToken("rbac_worker_1", static_tag);
        publisherOne.send(pubMsg);
    }

    broker.processOneMessage();

    // client receives notification for /cooking.italian
    {
        const auto reply = subscriber.readOne();
        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.serviceName() == "first.service");
        REQUIRE(reply.clientRequestId().empty());
        REQUIRE(reply.topic() == "/cooking.italian");
        REQUIRE(reply.body() == "Original carbonara recipe here!");
        REQUIRE(reply.error().empty());
        REQUIRE(reply.rbacToken() == "rbac_worker_1");
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

    broker.processOneMessage();

    // client receives notification for /cooking.indian
    {
        const auto reply = subscriber.readOne();
        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.serviceName() == "second.service");
        REQUIRE(reply.clientRequestId().empty());
        REQUIRE(reply.topic() == "/cooking.indian");
        REQUIRE(reply.body() == "Try our Chicken Korma!");
        REQUIRE(reply.error().empty());
        REQUIRE(reply.rbacToken() == "rbac_worker_2");
    }

    // unsubscribe client from /cooking.italian
    {
        auto unsubscribe = MdpMessage::createClientMessage(Command::Unsubscribe);
        unsubscribe.setTopic("/cooking.italian", static_tag);
        unsubscribe.setRbacToken("rbacToken", static_tag);
        subscriber.send(unsubscribe);
    }

    broker.processOneMessage();

    // publisher 1 sends a notification for /cooking.italian
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("first.service", static_tag);
        pubMsg.setTopic("/cooking.italian", static_tag);
        pubMsg.setBody("The best Margherita in town!", static_tag);
        pubMsg.setRbacToken("rbac_worker_1", static_tag);
        publisherOne.send(pubMsg);
    }

    broker.processOneMessage();

    // publisher 2 sends a notification for /cooking.indian
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("second.service", static_tag);
        pubMsg.setTopic("/cooking.indian", static_tag);
        pubMsg.setBody("Sizzling tikkas in our Restaurant!", static_tag);
        pubMsg.setRbacToken("rbac_worker_2", static_tag);
        publisherTwo.send(pubMsg);
    }

    broker.processOneMessage();

    // verify that the client receives only the notification from publisher 2

    {
        const auto reply = subscriber.readOne();

        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.serviceName() == "second.service");
        REQUIRE(reply.clientRequestId().empty());
        REQUIRE(reply.topic() == "/cooking.indian");
        REQUIRE(reply.body() == "Sizzling tikkas in our Restaurant!");
        REQUIRE(reply.error().empty());
        REQUIRE(reply.rbacToken() == "rbac_worker_2");
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

    broker.processOneMessage();

    subscriber.subscribe("/cooking.indian*");

    broker.processOneMessage();

    // publisher 1 sends a notification for /cooking.italian.pasta
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("first.service", static_tag);
        pubMsg.setTopic("/cooking.italian.pasta", static_tag);
        pubMsg.setBody("Original carbonara recipe here!", static_tag);
        pubMsg.setRbacToken("rbac_worker_1", static_tag);
        publisherOne.send(pubMsg);
    }

    broker.processOneMessage();

    // client receives notification for /cooking.italian*
    {
        const auto reply = subscriber.readOne();
        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.sourceId() == "/cooking.italian*");
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.serviceName() == "first.service");
        REQUIRE(reply.clientRequestId().empty());
        REQUIRE(reply.topic() == "/cooking.italian.pasta");
        REQUIRE(reply.body() == "Original carbonara recipe here!");
        REQUIRE(reply.error().empty());
        REQUIRE(reply.rbacToken() == "rbac_worker_1");
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

    broker.processOneMessage();

    // client receives notification for /cooking.indian*
    {
        const auto reply = subscriber.readOne();
        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.sourceId() == "/cooking.indian*");
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.serviceName() == "second.service");
        REQUIRE(reply.clientRequestId().empty());
        REQUIRE(reply.topic() == "/cooking.indian.chicken");
        REQUIRE(reply.body() == "Try our Chicken Korma!");
        REQUIRE(reply.error().empty());
        REQUIRE(reply.rbacToken() == "rbac_worker_2");
    }

    subscriber.unsubscribe("/cooking.italian*");

    broker.processOneMessage();

    // publisher 1 sends a notification for /cooking.italian.pizza
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("first.service", static_tag);
        pubMsg.setTopic("/cooking.italian.pizza", static_tag);
        pubMsg.setBody("The best Margherita in town!", static_tag);
        pubMsg.setRbacToken("rbac_worker_1", static_tag);
        publisherOne.send(pubMsg);
    }

    broker.processOneMessage();

    // publisher 2 sends a notification for /cooking.indian.tikkas
    {
        auto pubMsg = MdpMessage::createWorkerMessage(Command::Notify);
        pubMsg.setServiceName("second.service", static_tag);
        pubMsg.setTopic("/cooking.indian.tikkas", static_tag);
        pubMsg.setBody("Sizzling tikkas in our Restaurant!", static_tag);
        pubMsg.setRbacToken("rbac_worker_2", static_tag);
        publisherTwo.send(pubMsg);
    }

    broker.processOneMessage();

    // verify that the client receives only the notification from publisher 2

    {
        const auto reply = subscriber.readOne();

        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.sourceId() == "/cooking.indian*");
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.serviceName() == "second.service");
        REQUIRE(reply.clientRequestId().empty());
        REQUIRE(reply.topic() == "/cooking.indian.tikkas");
        REQUIRE(reply.body() == "Sizzling tikkas in our Restaurant!");
        REQUIRE(reply.error().empty());
        REQUIRE(reply.rbacToken() == "rbac_worker_2");
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
    BasicMdpWorker            worker3("a.service", INTERNAL_ADDRESS_BROKER, NonCopyableMovableHandler(), Context(), testSettings());
    BasicMdpWorker            worker4("a.service", INTERNAL_ADDRESS_BROKER, handler, Context(), testSettings());
    BasicMdpWorker            worker5("a.service", INTERNAL_ADDRESS_BROKER, NonCopyableMovableHandler(), testSettings());
    BasicMdpWorker            worker6("a.service", INTERNAL_ADDRESS_BROKER, handler, testSettings());
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

        const auto reply = client.readOne();

        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.clientRequestId() == "1");

        if (!reply.error().empty()) {
            REQUIRE(reply.error().find("error 501") != std::string_view::npos);
        } else {
            REQUIRE(reply.serviceName() == "a.service");
            REQUIRE(reply.topic() == "/topic");
            REQUIRE(reply.body() == "10");
            REQUIRE(reply.error().empty());
            REQUIRE(reply.rbacToken() == "rbacToken");
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

        const auto reply = client.readOne();

        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.clientRequestId() == "2");
        REQUIRE(reply.body() == "Value set. All good!");
        REQUIRE(reply.error().empty());
    }

    {
        auto request = MdpMessage::createClientMessage(Command::Get);
        request.setServiceName("a.service", static_tag);
        request.setClientRequestId("3", static_tag);
        request.setTopic("/topic", static_tag);
        request.setRbacToken("rbacToken", static_tag);
        client.send(request);

        const auto reply = client.readOne();

        REQUIRE(reply.isValid());
        REQUIRE(reply.isClientMessage());
        REQUIRE(reply.command() == Command::Final);
        REQUIRE(reply.clientRequestId() == "3");
        REQUIRE(reply.topic() == "/topic");
        REQUIRE(reply.body() == "42");
        REQUIRE(reply.error().empty());
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

    // until the worker's READY is processed by the broker, it will return
    // an "unknown service" error, retry until we get the expected reply
    bool goodReplyReceived = false;
    while (!goodReplyReceived) {
        bool anyMessageReceived = false;
        client.get("a.service", "", [&goodReplyReceived, &anyMessageReceived](auto &&message) {
            anyMessageReceived = true;
            if (message.error().empty()) {
                REQUIRE(message.body() == "100");
                goodReplyReceived = true;
            } else {
                REQUIRE(message.error() == "unknown service (error 501): 'a.service'");
            }
        });

        while (!anyMessageReceived) {
            client.tryRead(std::chrono::milliseconds(20));
        }
    }

    bool replyReceived = false;

    client.set("a.service", "42", [&replyReceived](auto &&message) {
        REQUIRE(message.body() == "Value set. All good!");
        REQUIRE(message.error().empty());
        replyReceived = true;
    });

    while (!replyReceived) {
        client.tryRead(std::chrono::milliseconds(20));
    }

    replyReceived = false;

    client.get("a.service", "", [&replyReceived](auto &&message) {
        REQUIRE(message.body() == "42");
        REQUIRE(message.error().empty());
        replyReceived = true;
    });

    while (!replyReceived) {
        client.tryRead(std::chrono::milliseconds(20));
    }
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

    // until the worker's READY is processed by the broker, it will return
    // an "unknown service" error, retry until we get the expected reply
    bool exceptionReplyReceived = false;
    while (!exceptionReplyReceived) {
        bool anyMessageReceived = false;
        client.get("a.service", "", [&exceptionReplyReceived, &anyMessageReceived](auto &&message) {
            anyMessageReceived = true;
            if (message.error().starts_with("unknown service")) {
                REQUIRE(message.error() == "unknown service (error 501): 'a.service'");
            } else {
                REQUIRE(message.error() == "Caught exception for service 'a.service'\nrequest message: \nexception: Something went wrong!");
                exceptionReplyReceived = true;
            }
        });

        while (!anyMessageReceived) {
            client.tryRead(std::chrono::milliseconds(20));
        }
    }
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

    // until the worker's READY is processed by the broker, it will return
    // an "unknown service" error, retry until we get the expected reply
    bool exceptionReplyReceived = false;
    while (!exceptionReplyReceived) {
        bool anyMessageReceived = false;
        client.get("a.service", "", [&exceptionReplyReceived, &anyMessageReceived](auto &&message) {
            anyMessageReceived = true;
            if (message.error().starts_with("unknown service")) {
                REQUIRE(message.error() == "unknown service (error 501): 'a.service'");
            } else {
                REQUIRE(message.error() == "Caught unexpected exception for service 'a.service'\nrequest message: ");
                exceptionReplyReceived = true;
            }
        });

        while (!anyMessageReceived) {
            client.tryRead(std::chrono::milliseconds(20));
        }
    }
}
