#include "helpers.hpp"

#include <majordomo/BasicMdpWorker.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Client.hpp>
#include <majordomo/Message.hpp>

#include <catch2/catch.hpp>
#include <fmt/format.h>

#include <charconv>
#include <cstdlib>
#include <deque>
#include <thread>

using namespace opencmw::majordomo;

template<typename MessageType>
class TestNode {
    std::deque<MessageType> _receivedMessages;

public:
    Socket _socket;

    explicit TestNode(Context &context, int socket_type = ZMQ_DEALER)
        : _socket(context, socket_type) {
    }

    bool connect(std::string_view address, std::string_view subscription = "") {
        auto result = zmq_invoke(zmq_connect, _socket, address);
        if (!result) return false;

        if (!subscription.empty()) {
            return zmq_invoke(zmq_setsockopt, _socket, ZMQ_SUBSCRIBE, subscription.data(), subscription.size()).isValid();
        }

        return result.isValid();
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

TEST_CASE("OpenCMW::Message basics", "[message]") {
    {
        auto msg = BrokerMessage::createClientMessage(BrokerMessage::ClientCommand::Final);
        REQUIRE(msg.isClientMessage());
        REQUIRE(msg.clientCommand() == BrokerMessage::ClientCommand::Final);

        auto tag = MessageFrame::static_bytes_tag{};
        msg.setTopic("I'm a topic", tag);
        msg.setServiceName("service://abc", tag);
        msg.setClientRequestId("request 1", tag);
        msg.setBody("test body test body test body test body test body test body test body", tag);
        msg.setError("fail!", tag);
        msg.setRbac("password", tag);

        REQUIRE(msg.isClientMessage());
        REQUIRE(msg.clientCommand() == BrokerMessage::ClientCommand::Final);
        REQUIRE(msg.topic() == "I'm a topic");
        REQUIRE(msg.serviceName() == "service://abc");
        REQUIRE(msg.clientRequestId() == "request 1");
        REQUIRE(msg.body() == "test body test body test body test body test body test body test body");
        REQUIRE(msg.error() == "fail!");
        REQUIRE(msg.rbac() == "password");

        REQUIRE(msg.isValid());
        REQUIRE(msg.availableFrameCount() == 9);
        REQUIRE(msg.frameAt(0).data() == "");
        REQUIRE(msg.frameAt(1).data() == "MDPC03");
        REQUIRE(msg.frameAt(2).data() == "\x6");
        REQUIRE(msg.frameAt(3).data() == "service://abc");
        REQUIRE(msg.frameAt(4).data() == "request 1");
        REQUIRE(msg.frameAt(5).data() == "I'm a topic");
        REQUIRE(msg.frameAt(6).data() == "test body test body test body test body test body test body test body");
        REQUIRE(msg.frameAt(7).data() == "fail!");
        REQUIRE(msg.frameAt(8).data() == "password");
    }

    {
        auto msg = MdpMessage::createClientMessage(MdpMessage::ClientCommand::Final);
        REQUIRE(msg.isClientMessage());
        REQUIRE(msg.clientCommand() == MdpMessage::ClientCommand::Final);

        auto tag = MessageFrame::static_bytes_tag{};
        msg.setTopic("I'm a topic", tag);
        msg.setServiceName("service://abc", tag);
        msg.setClientRequestId("request 1", tag);
        msg.setBody("test body test body test body test body test body test body test body", tag);
        msg.setError("fail!", tag);
        msg.setRbac("password", tag);

        REQUIRE(msg.isClientMessage());
        REQUIRE(msg.clientCommand() == MdpMessage::ClientCommand::Final);
        REQUIRE(msg.topic() == "I'm a topic");
        REQUIRE(msg.serviceName() == "service://abc");
        REQUIRE(msg.clientRequestId() == "request 1");
        REQUIRE(msg.body() == "test body test body test body test body test body test body test body");
        REQUIRE(msg.error() == "fail!");
        REQUIRE(msg.rbac() == "password");

        REQUIRE(msg.isValid());
        REQUIRE(msg.availableFrameCount() == 8);
        REQUIRE(msg.frameAt(0).data() == "MDPC03");
        REQUIRE(msg.frameAt(1).data() == "\x6");
        REQUIRE(msg.frameAt(2).data() == "service://abc");
        REQUIRE(msg.frameAt(3).data() == "request 1");
        REQUIRE(msg.frameAt(4).data() == "I'm a topic");
        REQUIRE(msg.frameAt(5).data() == "test body test body test body test body test body test body test body");
        REQUIRE(msg.frameAt(6).data() == "fail!");
        REQUIRE(msg.frameAt(7).data() == "password");
    }
}

TEST_CASE("Request answered with unknown service", "[broker][unknown_service]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    constexpr auto address = std::string_view("inproc://testrouter");

    Context        context;
    Broker         broker("testbroker", {}, context);

    REQUIRE(broker.bind(address, Broker::BindOption::Router));

    RunInThread          brokerRun(broker);

    TestNode<MdpMessage> client(context);
    REQUIRE(client.connect(address));

    MdpMessage request;
    request.setFrames({ std::make_unique<std::string>("MDPC03"),
            std::make_unique<std::string>("\x1"),
            std::make_unique<std::string>("no.service"),
            std::make_unique<std::string>("1"),
            std::make_unique<std::string>("topic"),
            std::make_unique<std::string>(""),
            std::make_unique<std::string>(""),
            std::make_unique<std::string>("rbac") });

    client.send(request);

    const auto reply = client.readOne();

    REQUIRE(reply.isValid());
    REQUIRE(reply.availableFrameCount() == 8);
    REQUIRE(reply.frameAt(0).data() == "MDPC03");
    REQUIRE(reply.frameAt(1).data() == "\x6");
    REQUIRE(reply.frameAt(2).data() == "no.service");
    REQUIRE(reply.frameAt(3).data() == "1");
    REQUIRE(reply.frameAt(4).data() == "mmi.service");
    REQUIRE(reply.frameAt(5).data().empty());
    REQUIRE(reply.frameAt(6).data() == "unknown service (error 501): 'no.service'");
    REQUIRE(reply.frameAt(7).data() == "TODO (RBAC)");
}

TEST_CASE("One client/one worker roundtrip", "[broker][roundtrip]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    constexpr auto address = std::string_view("inproc://testrouter");

    Context        context;
    Broker         broker("testbroker", {}, context);

    REQUIRE(broker.bind(address, Broker::BindOption::Router));

    TestNode<MdpMessage> worker(context);
    REQUIRE(worker.connect(address));

    TestNode<MdpMessage> client(context);
    REQUIRE(client.connect(address));

    MdpMessage ready;
    ready.setFrames({ std::make_unique<std::string>("MDPW03"),
            std::make_unique<std::string>("\x6"), // READY
            std::make_unique<std::string>("a.service"),
            std::make_unique<std::string>("1"),
            std::make_unique<std::string>("topic"),
            std::make_unique<std::string>("API description"),
            std::make_unique<std::string>(""),
            std::make_unique<std::string>("rbac") });
    worker.send(ready);

    broker.processOneMessage();

    MdpMessage request;
    request.setFrames({ std::make_unique<std::string>("MDPC03"),
            std::make_unique<std::string>("\x1"), // GET
            std::make_unique<std::string>("a.service"),
            std::make_unique<std::string>("1"),
            std::make_unique<std::string>("topic"),
            std::make_unique<std::string>(""),
            std::make_unique<std::string>(""),
            std::make_unique<std::string>("rbac") });

    client.send(request);

    broker.processOneMessage();

    const auto requestAtWorker = worker.readOne();
    REQUIRE(requestAtWorker.isValid());
    REQUIRE(requestAtWorker.availableFrameCount() == 8);
    REQUIRE(requestAtWorker.frameAt(0).data() == "MDPW03");
    REQUIRE(requestAtWorker.frameAt(1).data() == "\x1"); // GET
    REQUIRE(!requestAtWorker.frameAt(2).data().empty()); // client ID
    REQUIRE(requestAtWorker.frameAt(3).data() == "1");
    REQUIRE(requestAtWorker.frameAt(4).data() == "topic");
    REQUIRE(requestAtWorker.frameAt(5).data().empty());
    REQUIRE(requestAtWorker.frameAt(6).data().empty());
    REQUIRE(requestAtWorker.frameAt(7).data() == "rbac");

    MdpMessage replyFromWorker;
    replyFromWorker.setFrames({ std::make_unique<std::string>("MDPW03"),
            std::make_unique<std::string>("\x4"), // FINAL
            std::make_unique<std::string>(requestAtWorker.frameAt(2).data()),
            std::make_unique<std::string>("1"),
            std::make_unique<std::string>("topic"),
            std::make_unique<std::string>("reply body"),
            std::make_unique<std::string>(""),
            std::make_unique<std::string>("rbac_worker") });

    worker.send(replyFromWorker);

    broker.processOneMessage();

    const auto reply = client.readOne();
    REQUIRE(reply.isValid());
    REQUIRE(reply.availableFrameCount() == 8);
    REQUIRE(reply.frameAt(0).data() == "MDPC03");
    REQUIRE(reply.frameAt(1).data() == "\x6"); // FINAL
    REQUIRE(reply.frameAt(2).data() == "a.service");
    REQUIRE(reply.frameAt(3).data() == "1");
    REQUIRE(reply.frameAt(4).data() == "topic");
    REQUIRE(reply.frameAt(5).data() == "reply body");
    REQUIRE(reply.frameAt(6).data().empty());
    REQUIRE(reply.frameAt(7).data() == "rbac_worker");

    broker.cleanup();

    const auto disconnect = worker.readOne();
    REQUIRE(disconnect.isValid());
    REQUIRE(disconnect.availableFrameCount() == 8);
    REQUIRE(disconnect.frameAt(0).data() == "MDPW03");
    REQUIRE(disconnect.frameAt(1).data() == "\x7"); // DISCONNECT
    REQUIRE(disconnect.frameAt(2).data() == "a.service");
    REQUIRE(disconnect.frameAt(3).data().empty());
    REQUIRE(disconnect.frameAt(4).data() == "a.service");
    REQUIRE(disconnect.frameAt(5).data() == "broker shutdown");
    REQUIRE(disconnect.frameAt(6).data().empty());
    REQUIRE(disconnect.frameAt(7).data() == "TODO (RBAC)");
}

TEST_CASE("Simple pubsub example using pub socket", "[broker][pubsub_pub]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    constexpr auto routerAddress    = std::string_view("inproc://testrouter");
    constexpr auto publisherAddress = std::string_view("inproc://testpub");

    Context        context;
    Broker         broker("testbroker", {}, context);

    REQUIRE(broker.bind(routerAddress, Broker::BindOption::Router));
    REQUIRE(broker.bind(publisherAddress, Broker::BindOption::Pub));

    TestNode<BrokerMessage> subscriber(context, ZMQ_SUB);
    REQUIRE(subscriber.connect(publisherAddress, "a.topic"));

    broker.processOneMessage();

    TestNode<MdpMessage> publisher(context);
    REQUIRE(publisher.connect(routerAddress));

    MdpMessage pubMsg1;
    pubMsg1.setFrames({ std::make_unique<std::string>("MDPW03"),
            std::make_unique<std::string>("\x5"), // NOTIFY
            std::make_unique<std::string>("a.service"),
            std::make_unique<std::string>("1"),
            std::make_unique<std::string>("a.topic"),
            std::make_unique<std::string>("First notification about a.topic"),
            std::make_unique<std::string>(""),
            std::make_unique<std::string>("rbac_worker") });

    publisher.send(pubMsg1);

    broker.processOneMessage();

    const auto reply = subscriber.readOne();
    REQUIRE(reply.isValid());
    REQUIRE(reply.availableFrameCount() == 9);
    REQUIRE(reply.frameAt(0).data() == "a.topic");
    REQUIRE(reply.frameAt(1).data() == "MDPC03");
    REQUIRE(reply.frameAt(2).data() == "\x6"); // FINAL
    REQUIRE(reply.frameAt(3).data() == "a.service");
    REQUIRE(reply.frameAt(4).data() == "1");
    REQUIRE(reply.frameAt(5).data() == "a.topic");
    REQUIRE(reply.frameAt(6).data() == "First notification about a.topic");
    REQUIRE(reply.frameAt(7).data().empty());
    REQUIRE(reply.frameAt(8).data() == "rbac_worker");
}

TEST_CASE("pubsub example using router socket", "[broker][pubsub_router]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    constexpr auto address = std::string_view("inproc://testrouter");

    Context        context;
    Broker         broker("testbroker", {}, context);

    REQUIRE(broker.bind(address, Broker::BindOption::Router));

    TestNode<MdpMessage> subscriber(context);
    REQUIRE(subscriber.connect(address));

    TestNode<MdpMessage> publisherOne(context);
    REQUIRE(publisherOne.connect(address));

    TestNode<MdpMessage> publisherTwo(context);
    REQUIRE(publisherTwo.connect(address));

    // subscribe client to a.topic
    {
        MdpMessage subscribe;
        subscribe.setFrames({ std::make_unique<std::string>("MDPC03"),
                std::make_unique<std::string>("\x3"), // SUBSCRIBE
                std::make_unique<std::string>("first.service"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("a.topic"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("rbac") });
        subscriber.send(subscribe);
    }

    broker.processOneMessage();

    // subscribe client to another.topic
    {
        MdpMessage subscribe;
        subscribe.setFrames({ std::make_unique<std::string>("MDPC03"),
                std::make_unique<std::string>("\x3"), // SUBSCRIBE
                std::make_unique<std::string>("second.service"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("another.topic"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("rbac") });
        subscriber.send(subscribe);
    }

    broker.processOneMessage();

    // publisher 1 sends a notification for a.topic
    {
        MdpMessage pubMsg;
        pubMsg.setFrames({ std::make_unique<std::string>("MDPW03"),
                std::make_unique<std::string>("\x5"), // NOTIFY
                std::make_unique<std::string>("first.service"),
                std::make_unique<std::string>("1"),
                std::make_unique<std::string>("a.topic"),
                std::make_unique<std::string>("First notification about a.topic"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("rbac_worker_1") });
        publisherOne.send(pubMsg);
    }

    broker.processOneMessage();

    // client receives notification for a.topic
    {
        const auto reply = subscriber.readOne();
        REQUIRE(reply.availableFrameCount() == 8);
        REQUIRE(reply.frameAt(0).data() == "MDPC03");
        REQUIRE(reply.frameAt(1).data() == "\x6"); // FINAL
        REQUIRE(reply.frameAt(2).data() == "first.service");
        REQUIRE(reply.frameAt(3).data() == "1");
        REQUIRE(reply.frameAt(4).data() == "a.topic");
        REQUIRE(reply.frameAt(5).data() == "First notification about a.topic");
        REQUIRE(reply.frameAt(6).data().empty());
        REQUIRE(reply.frameAt(7).data() == "rbac_worker_1");
    }

    // publisher 2 sends a notification for another.topic
    {
        MdpMessage pubMsg;
        pubMsg.setFrames({ std::make_unique<std::string>("MDPW03"),
                std::make_unique<std::string>("\x5"), // NOTIFY
                std::make_unique<std::string>("second.service"),
                std::make_unique<std::string>("1"),
                std::make_unique<std::string>("another.topic"),
                std::make_unique<std::string>("First notification about another.topic"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("rbac_worker_2") });
        publisherTwo.send(pubMsg);
    }

    broker.processOneMessage();

    // client receives notification for another.topic
    {
        const auto reply = subscriber.readOne();
        REQUIRE(reply.isValid());
        REQUIRE(reply.availableFrameCount() == 8);
        REQUIRE(reply.frameAt(0).data() == "MDPC03");
        REQUIRE(reply.frameAt(1).data() == "\x6"); // FINAL
        REQUIRE(reply.frameAt(2).data() == "second.service");
        REQUIRE(reply.frameAt(3).data() == "1");
        REQUIRE(reply.frameAt(4).data() == "another.topic");
        REQUIRE(reply.frameAt(5).data() == "First notification about another.topic");
        REQUIRE(reply.frameAt(6).data().empty());
        REQUIRE(reply.frameAt(7).data() == "rbac_worker_2");
    }

    // unsubscribe client from first.service
    {
        MdpMessage unsubscribe;
        unsubscribe.setFrames({ std::make_unique<std::string>("MDPC03"),
                std::make_unique<std::string>("\x4"), // UNSUBSCRIBE
                std::make_unique<std::string>("first.service"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("a.topic"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("rbac") });
        subscriber.send(unsubscribe);
    }

    broker.processOneMessage();

    // publisher 1 sends a notification for a.topic
    {
        MdpMessage pubMsg;
        pubMsg.setFrames({ std::make_unique<std::string>("MDPW03"),
                std::make_unique<std::string>("\x5"), // NOTIFY
                std::make_unique<std::string>("first.service"),
                std::make_unique<std::string>("1"),
                std::make_unique<std::string>("a.topic"),
                std::make_unique<std::string>("First notification about a.topic"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("rbac_worker_1") });
        publisherOne.send(pubMsg);
    }

    broker.processOneMessage();

    // publisher 2 sends a notification for another.topic
    {
        MdpMessage pubMsg;
        pubMsg.setFrames({ std::make_unique<std::string>("MDPW03"),
                std::make_unique<std::string>("\x5"), // NOTIFY
                std::make_unique<std::string>("second.service"),
                std::make_unique<std::string>("1"),
                std::make_unique<std::string>("another.topic"),
                std::make_unique<std::string>("Second notification about another.topic"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("rbac_worker_2") });
        publisherTwo.send(pubMsg);
    }

    broker.processOneMessage();

    // verify that the client receives only the notification from publisherTwo

    {
        const auto reply = subscriber.readOne();
        REQUIRE(reply.availableFrameCount() == 8);
        REQUIRE(reply.frameAt(0).data() == "MDPC03");
        REQUIRE(reply.frameAt(1).data() == "\x6"); // FINAL
        REQUIRE(reply.frameAt(2).data() == "second.service");
        REQUIRE(reply.frameAt(3).data() == "1");
        REQUIRE(reply.frameAt(4).data() == "another.topic");
        REQUIRE(reply.frameAt(5).data() == "Second notification about another.topic");
        REQUIRE(reply.frameAt(6).data().empty());
        REQUIRE(reply.frameAt(7).data() == "rbac_worker_2");
    }
}

using opencmw::majordomo::MdpMessage;

class TestIntWorker : public opencmw::majordomo::BasicMdpWorker {
    int _x = 10;

public:
    explicit TestIntWorker(Context &context, std::string serviceName, int initialValue)
        : opencmw::majordomo::BasicMdpWorker(context, serviceName)
        , _x(initialValue) {
    }

    std::optional<MdpMessage> handleGet(MdpMessage &&msg) override {
        msg.setWorkerCommand(MdpMessage::WorkerCommand::Final);
        msg.setBody(std::to_string(_x), MessageFrame::dynamic_bytes_tag{});
        return std::move(msg);
    }

    std::optional<MdpMessage> handleSet(MdpMessage &&msg) override {
        const auto request = msg.body();
        int        value;
        const auto result = std::from_chars(request.begin(), request.end(), value);

        if (result.ec == std::errc::invalid_argument) {
            msg.setWorkerCommand(MdpMessage::WorkerCommand::Final);
            msg.setBody("", MessageFrame::static_bytes_tag{});
            msg.setError("Not a valid int", MessageFrame::static_bytes_tag{});
        } else {
            msg.setWorkerCommand(MdpMessage::WorkerCommand::Final);
            _x = value;
            msg.setBody("Value set. All good!", MessageFrame::static_bytes_tag{});
            msg.setError("", MessageFrame::static_bytes_tag{});
        }

        return std::move(msg);
    }
};

TEST_CASE("SET/GET example using the BasicMdpWorker class", "[worker][getset_basic_worker]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::MdpMessage;

    constexpr auto address = std::string_view("inproc://testrouter");

    Context        context;
    Broker         broker("testbroker", {}, context);

    REQUIRE(broker.bind(address, Broker::BindOption::Router));

    RunInThread   brokerRun(broker);

    TestIntWorker worker(context, "a.service", 10);
    worker.setServiceDescription("API description");
    worker.setRbacRole("rbac");

    REQUIRE(worker.connect(address));

    RunInThread          workerRun(worker);

    TestNode<MdpMessage> client(context);
    REQUIRE(client.connect(address));

    // until the worker's READY is processed by the broker, it will return
    // an "unknown service" error, retry until we get the expected reply
    bool replyReceived = false;
    while (!replyReceived) {
        MdpMessage request;
        request.setFrames({ std::make_unique<std::string>("MDPC03"),
                std::make_unique<std::string>("\x1"), // GET
                std::make_unique<std::string>("a.service"),
                std::make_unique<std::string>("1"),
                std::make_unique<std::string>("topic"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("rbac") });

        client.send(request);

        const auto reply = client.readOne();

        REQUIRE(reply.isValid());
        REQUIRE(reply.availableFrameCount() == 8);
        REQUIRE(reply.frameAt(0).data() == "MDPC03");
        REQUIRE(reply.frameAt(1).data() == "\x6"); // FINAL

        if (!reply.frameAt(6).data().empty()) {
            REQUIRE(reply.frameAt(6).data().find("error 501") != std::string_view::npos);
        } else {
            REQUIRE(reply.frameAt(2).data() == "a.service");
            REQUIRE(reply.frameAt(3).data() == "1");
            REQUIRE(reply.frameAt(4).data() == "topic");
            REQUIRE(reply.frameAt(5).data() == "10");
            REQUIRE(reply.frameAt(6).data().empty());
            REQUIRE(reply.frameAt(7).data() == "rbac");
            replyReceived = true;
        }
    }

    {
        MdpMessage request;
        request.setFrames({ std::make_unique<std::string>("MDPC03"),
                std::make_unique<std::string>("\x2"), // SET
                std::make_unique<std::string>("a.service"),
                std::make_unique<std::string>("2"),
                std::make_unique<std::string>("topic"),
                std::make_unique<std::string>("42"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("rbac") });

        client.send(request);

        const auto reply = client.readOne();

        REQUIRE(reply.availableFrameCount() == 8);
        REQUIRE(reply.frameAt(0).data() == "MDPC03");
        REQUIRE(reply.frameAt(1).data() == "\x6"); // FINAL
        REQUIRE(reply.frameAt(3).data() == "2");
        REQUIRE(reply.frameAt(5).data() == "Value set. All good!");
        REQUIRE(reply.frameAt(6).data() == "");
    }

    {
        MdpMessage request;
        request.setFrames({ std::make_unique<std::string>("MDPC03"),
                std::make_unique<std::string>("\x1"), // GET
                std::make_unique<std::string>("a.service"),
                std::make_unique<std::string>("3"),
                std::make_unique<std::string>("topic"),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>(""),
                std::make_unique<std::string>("rbac") });

        client.send(request);

        const auto reply = client.readOne();

        REQUIRE(reply.isValid());
        REQUIRE(reply.availableFrameCount() == 8);
        REQUIRE(reply.frameAt(0).data() == "MDPC03");
        REQUIRE(reply.frameAt(1).data() == "\x6"); // FINAL
        REQUIRE(reply.frameAt(3).data() == "3");
        REQUIRE(reply.frameAt(5).data() == "42");
    }
}

TEST_CASE("SET/GET example using the Client and Worker classes", "[client][getset]") {
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::Client;
    using opencmw::majordomo::MdpMessage;

    constexpr auto address = std::string_view("inproc://testrouter");

    Context        context;
    Broker         broker("testbroker", {}, context);

    REQUIRE(broker.bind(address, Broker::BindOption::Router));

    RunInThread   brokerRun(broker);

    TestIntWorker worker(context, "a.service", 100);
    worker.setServiceDescription("API description");
    worker.setRbacRole("rbac");

    REQUIRE(worker.connect(address));

    RunInThread workerRun(worker);

    Client      client(context);

    REQUIRE(client.connect(address));

    const auto mainThreadId = std::atomic(std::this_thread::get_id());

    // until the worker's READY is processed by the broker, it will return
    // an "unknown service" error, retry until we get the expected reply
    bool goodReplyReceived = false;
    while (!goodReplyReceived) {
        bool anyMessageReceived = false;
        client.get("a.service", "", [&goodReplyReceived, &anyMessageReceived, &mainThreadId](auto &&message) {
            REQUIRE(mainThreadId == std::this_thread::get_id());
            anyMessageReceived = true;
            if (message.error().empty()) {
                REQUIRE(message.body() == "100");
                goodReplyReceived = true;
            } else {
                REQUIRE(message.error() == "unknown service (error 501): 'a.service'");
            }
        });

        while (!anyMessageReceived) {
            client.tryRead();
        }
    }

    bool replyReceived = false;

    client.set("a.service", "42", [&replyReceived, &mainThreadId](auto &&message) {
        REQUIRE(mainThreadId == std::this_thread::get_id());
        REQUIRE(message.body() == "Value set. All good!");
        REQUIRE(message.error().empty());
        replyReceived = true;
    });

    while (!replyReceived) {
        client.tryRead();
    }

    replyReceived = false;

    client.get("a.service", "", [&replyReceived, &mainThreadId](auto &&message) {
        REQUIRE(mainThreadId == std::this_thread::get_id());
        REQUIRE(message.body() == "42");
        REQUIRE(message.error().empty());
        replyReceived = true;
    });

    while (!replyReceived) {
        client.tryRead();
    }
}
