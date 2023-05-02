#include <catch2/catch.hpp>
#include <majordomo/MockClient.hpp>
#include <MockServer.hpp>

// Concepts and tests use common types
#include <concepts/majordomo/helpers.hpp>

TEST_CASE("SET/GET of MockServer", "[mock-server][lambda_handler]") {
    using namespace opencmw;
    using namespace opencmw::majordomo;
    using namespace std::chrono_literals;

    zmq::Context context{};
    MockServer   server(context);

    MockClient client(server.context());
    REQUIRE(client.connect(opencmw::URI<>(server.address())));
    // REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    client.get("a.service", "", [](auto &&message) {
        fmt::print("A: {}\n", message);
        REQUIRE(message.error() == "");
        REQUIRE(message.body() == "100");
    });
    server.processRequest([](auto &&req, auto &reply) {
        REQUIRE(req.command() == Command::Get);
        reply.setBody(std::to_string(100), MessageFrame::dynamic_bytes_tag{});
    });
    REQUIRE(client.tryRead(3s));

    client.set("a.service", "42", [](auto &&message) {
        fmt::print("B: {}\n", message);
        REQUIRE(message.error() == "");
        REQUIRE(message.body() == "Value set. All good!");
    });
    server.processRequest([](auto &&req, auto &reply) {
        REQUIRE(req.command() == Command::Set);
        REQUIRE(req.body() == "42");
        reply.setBody("Value set. All good!", MessageFrame::static_bytes_tag{});
    });
    REQUIRE(client.tryRead(3s));

    client.get("a.service", "", [](auto &&message) {
        fmt::print("C: {}\n", message);
        REQUIRE(message.error() == "");
        REQUIRE(message.body() == "42");
    });

    server.processRequest([](auto &&req, auto &reply) {
        REQUIRE(req.command() == Command::Get);
        reply.setBody(std::to_string(42), MessageFrame::dynamic_bytes_tag{});
    });

    REQUIRE(client.tryRead(3s));
}

TEST_CASE("MockServer Subscription Test", "[mock-server][lambda_handler]") {
    using opencmw::majordomo::BasicMdpMessage;
    using opencmw::majordomo::Command;
    using opencmw::majordomo::MessageFormat;
    using opencmw::majordomo::MessageFrame;
    using opencmw::majordomo::MockServer;
    using namespace opencmw;
    using namespace std::chrono_literals;

    zmq::Context                                           context{};
    MockServer                                             server(context);

    TestNode<BasicMdpMessage<MessageFormat::WithSourceId>> client(context, ZMQ_SUB);
    REQUIRE(client.connect(opencmw::URI<>(server.addressSub())));

    client.subscribe("a.service");
    std::this_thread::sleep_for(10ms); // wait for the subscription to be set-up. todo: investigate more clever way

    server.notify("a.service", "100");
    server.notify("a.service", "23");

    {
        auto reply = client.tryReadOne();
        fmt::print("{}\n", reply.has_value());
        REQUIRE(reply);
        REQUIRE(reply->body() == "100");
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->topic() == "a.service");
    }
    {
        auto reply = client.tryReadOne();
        fmt::print("{}\n", reply.has_value());
        REQUIRE(reply);
        REQUIRE(reply->body() == "23");
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->topic() == "a.service");
    }

    server.notify("a.service", "10");
    {
        auto reply = client.tryReadOne();
        fmt::print("{}\n", reply.has_value());
        REQUIRE(reply);
        REQUIRE(reply->body() == "10");
        REQUIRE(reply->command() == Command::Final);
        REQUIRE(reply->topic() == "a.service");
    }
}
