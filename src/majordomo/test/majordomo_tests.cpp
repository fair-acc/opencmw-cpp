#include <cstdlib>

#include <majordomo/Message.hpp>
#include <majordomo/broker.hpp>

#include <catch2/catch.hpp>
#include <deque>
#include <thread>

using Majordomo::OpenCMW::MdpMessage;

class TestNode {
    std::deque<yaz::Message> _receivedMessages;

public:
    yaz::Socket<yaz::Message, TestNode *> _socket;

    explicit TestNode(yaz::Context &context, int socket_type = ZMQ_DEALER)
        : _socket(yaz::make_socket<yaz::Message>(context, socket_type, this)) {
    }

    bool connect(std::string_view address, std::string_view subscription="") {
        return _socket.connect(address, subscription);
    }

    yaz::Message read_one() {
        while (_receivedMessages.empty()) {
            _socket.read();
        }

        assert(!_receivedMessages.empty());
        auto msg = std::move(_receivedMessages.front());
        _receivedMessages.pop_front();
        return msg;
    }

    void send(yaz::Message &&message) {
        _socket.send(std::move(message));
    }

    void handle_message(auto &, auto &&message) {
        _receivedMessages.emplace_back(std::move(message));
    }
};

TEST_CASE("OpenCMW::Message basics", "[Majordomo]") {
    using Majordomo::OpenCMW::MdpMessage;

    auto msg = MdpMessage::createClientMessage(MdpMessage::ClientCommand::Final);
    REQUIRE(msg.isClientMessage());
    REQUIRE(msg.clientCommand() == MdpMessage::ClientCommand::Final);

    auto tag = yaz::MessagePart::static_bytes_tag{};
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

    REQUIRE(msg.parts_count() == 9);
    REQUIRE(msg[0].data().empty());
    REQUIRE(msg[1].data() == "MDPC03");
    REQUIRE(msg[2].data() == "\x6");
    REQUIRE(msg[3].data() == "service://abc");
    REQUIRE(msg[4].data() == "request 1");
    REQUIRE(msg[5].data() == "I'm a topic");
    REQUIRE(msg[6].data() == "test body test body test body test body test body test body test body");
    REQUIRE(msg[7].data() == "fail!");
    REQUIRE(msg[8].data() == "password");
}

TEST_CASE("Request answered with unknown service", "[Broker]") {
    using Majordomo::OpenCMW::Broker;
    using Majordomo::OpenCMW::MdpMessage;

    yaz::Context   context;
    Broker         broker("testbroker", {}, context);

    std::thread    brokerThread([&broker] {
        broker.run();
       });

    constexpr auto address = std::string_view("inproc://broker/router"); // TODO use shared address with broker

    TestNode       client(context);
    REQUIRE(client.connect(address));

    yaz::Message request;
    request.add_part(std::make_unique<std::string>("MDPC03"));
    request.add_part(std::make_unique<std::string>("\x1"));
    request.add_part(std::make_unique<std::string>("no.service"));
    request.add_part(std::make_unique<std::string>("1"));
    request.add_part(std::make_unique<std::string>("topic"));
    request.add_part();
    request.add_part();
    request.add_part(std::make_unique<std::string>("rbac"));

    client.send(std::move(request));

    const auto reply = client.read_one();
    REQUIRE(reply.parts_count() == 8);
    REQUIRE(reply[0].data() == "MDPC03");
    REQUIRE(reply[1].data() == "\x6");
    REQUIRE(reply[2].data() == "no.service");
    REQUIRE(reply[3].data() == "1");
    REQUIRE(reply[4].data() == "mmi.service");
    REQUIRE(reply[5].data().empty());
    REQUIRE(reply[6].data() == "unknown service (error 501): 'no.service'");
    REQUIRE(reply[7].data() == "TODO (RBAC)");

    broker.shutdown();
    brokerThread.join();
}

TEST_CASE("One client/one worker roundtrip", "[Broker]") {
    using Majordomo::OpenCMW::Broker;
    using Majordomo::OpenCMW::MdpMessage;

    yaz::Context   context;
    Broker         broker("testbroker", {}, context);

    constexpr auto address = std::string_view("inproc://broker/router"); // TODO use shared address with broker

    TestNode       worker(context);
    REQUIRE(worker.connect(address));

    TestNode client(context);
    REQUIRE(client.connect(address));

    yaz::Message ready;
    ready.add_part(std::make_unique<std::string>("MDPW03"));
    ready.add_part(std::make_unique<std::string>("\x6")); // READY
    ready.add_part(std::make_unique<std::string>("a.service"));
    ready.add_part(std::make_unique<std::string>("1"));
    ready.add_part(std::make_unique<std::string>("topic"));
    ready.add_part(std::make_unique<std::string>("API description"));
    ready.add_part();
    ready.add_part(std::make_unique<std::string>("rbac"));
    worker.send(std::move(ready));

    broker.process_one_message();

    yaz::Message request;
    request.add_part(std::make_unique<std::string>("MDPC03"));
    request.add_part(std::make_unique<std::string>("\x1")); // GET
    request.add_part(std::make_unique<std::string>("a.service"));
    request.add_part(std::make_unique<std::string>("1"));
    request.add_part(std::make_unique<std::string>("topic"));
    request.add_part();
    request.add_part();
    request.add_part(std::make_unique<std::string>("rbac"));

    client.send(std::move(request));

    broker.process_one_message();

    const auto request_at_worker = worker.read_one();
    REQUIRE(request_at_worker.parts_count() == 8);
    REQUIRE(request_at_worker[0].data() == "MDPW03");
    REQUIRE(request_at_worker[1].data() == "\x1"); // GET
    REQUIRE(!request_at_worker[2].data().empty()); // client ID
    REQUIRE(request_at_worker[3].data() == "1");
    REQUIRE(request_at_worker[4].data() == "topic");
    REQUIRE(request_at_worker[5].data().empty());
    REQUIRE(request_at_worker[6].data().empty());
    REQUIRE(request_at_worker[7].data() == "rbac");

    yaz::Message reply_from_worker;
    reply_from_worker.add_part(std::make_unique<std::string>("MDPW03"));
    reply_from_worker.add_part(std::make_unique<std::string>("\x4")); // FINAL
    reply_from_worker.add_part(std::make_unique<std::string>(request_at_worker[2].data()));
    reply_from_worker.add_part(std::make_unique<std::string>("1"));
    reply_from_worker.add_part(std::make_unique<std::string>("topic"));
    reply_from_worker.add_part(std::make_unique<std::string>("reply body"));
    reply_from_worker.add_part();
    reply_from_worker.add_part(std::make_unique<std::string>("rbac_worker"));

    worker.send(std::move(reply_from_worker));

    broker.process_one_message();

    const auto reply = client.read_one();
    REQUIRE(reply.parts_count() == 8);
    REQUIRE(reply[0].data() == "MDPC03");
    REQUIRE(reply[1].data() == "\x4"); // FINAL
    REQUIRE(reply[2].data() == "a.service");
    REQUIRE(reply[3].data() == "1");
    REQUIRE(reply[4].data() == "topic");
    REQUIRE(reply[5].data() == "reply body");
    REQUIRE(reply[6].data().empty());
    REQUIRE(reply[7].data() == "rbac_worker");

    broker.cleanup();

    const auto disconnect = worker.read_one();
    REQUIRE(disconnect.parts_count() == 8);
    REQUIRE(disconnect[0].data() == "MDPW03");
    REQUIRE(disconnect[1].data() == "\x7"); // DISCONNECT
    REQUIRE(disconnect[2].data() == "a.service");
    REQUIRE(disconnect[3].data().empty());
    REQUIRE(disconnect[4].data() == "a.service");
    REQUIRE(disconnect[5].data() == "broker shutdown");
    REQUIRE(disconnect[6].data().empty());
    REQUIRE(disconnect[7].data() == "TODO (RBAC)");
}

TEST_CASE("Simple pubsub example", "[Broker]") {
    using Majordomo::OpenCMW::Broker;
    using Majordomo::OpenCMW::MdpMessage;

    yaz::Context   context;
    Broker         broker("testbroker", {}, context);

    constexpr auto router_address = std::string_view("inproc://broker/router"); // TODO use shared address with broker
    constexpr auto publisher_address = std::string_view("inproc://broker/publisher"); // TODO use shared address with broker

    TestNode subscriber(context, ZMQ_SUB);
    REQUIRE(subscriber.connect(publisher_address, "a.topic"));

    broker.process_one_message();

    TestNode publisher(context);
    REQUIRE(publisher.connect(router_address));

    yaz::Message pub_msg1;
    pub_msg1.add_part(std::make_unique<std::string>("MDPW03"));
    pub_msg1.add_part(std::make_unique<std::string>("\x5")); // NOTIFY
    pub_msg1.add_part(std::make_unique<std::string>("a.service"));
    pub_msg1.add_part(std::make_unique<std::string>("1"));
    pub_msg1.add_part(std::make_unique<std::string>("a.topic"));
    pub_msg1.add_part(std::make_unique<std::string>("First notification about a.topic"));
    pub_msg1.add_part();
    pub_msg1.add_part(std::make_unique<std::string>("rbac_worker"));

    publisher.send(std::move(pub_msg1));

    broker.process_one_message();

    const auto reply = subscriber.read_one();
    REQUIRE(reply.parts_count() == 9);
    REQUIRE(reply[0].data() == "a.topic");
    REQUIRE(reply[1].data() == "MDPC03");
    REQUIRE(reply[2].data() == "\x6"); // FINAL
    REQUIRE(reply[3].data() == "a.service");
    REQUIRE(reply[4].data() == "1");
    REQUIRE(reply[5].data() == "a.topic");
    REQUIRE(reply[6].data() == "First notification about a.topic");
    REQUIRE(reply[7].data().empty());
    REQUIRE(reply[8].data() == "rbac_worker");
}
