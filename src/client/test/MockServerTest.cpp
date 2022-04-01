#include "../../majordomo/test/helpers.hpp" // TestNode
#include <catch2/catch.hpp>
#include <majordomo/Client.hpp>
#include <MockServer.hpp>

TEST_CASE("SET/GET", "[mock-server][lambda_handler]") {
    using namespace opencmw::majordomo;
    using namespace std::chrono_literals;

    Context    context{};
    MockServer server(context);
    REQUIRE(server.bind(opencmw::URI<>("mdp://127.0.0.1:2345")));
    // REQUIRE(server.bind(INTERNAL_ADDRESS_BROKER));

    Client client(server.context());
    REQUIRE(client.connect(opencmw::URI<>("mdp://127.0.0.1:2345")));
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

    client.get("a.service", "", [](auto &&message) {
        fmt::print("C: {}\n", message);
        REQUIRE(message.error() == "");
        REQUIRE(message.body() == "42");
    });

    server.processRequest([](auto &&req, auto &reply) {
        REQUIRE(req.command() == Command::Set);
        REQUIRE(req.body() == "42");
        reply.setBody("Value set. All good!", MessageFrame::static_bytes_tag{});
    });
    server.processRequest([](auto &&req, auto &reply) {
        REQUIRE(req.command() == Command::Get);
        reply.setBody(std::to_string(42), MessageFrame::dynamic_bytes_tag{});
    });
    REQUIRE(client.tryRead(3s));
    REQUIRE(client.tryRead(3s));
}

TEST_CASE("Subscription Test", "[mock-server][lambda_handler]") {
    using namespace opencmw::majordomo;
    using namespace std::chrono_literals;

    Context    context{};
    MockServer server(context);
    REQUIRE(server.bind(opencmw::URI<>("mdp://127.0.0.1:2345")));
    REQUIRE(server.bindPub(opencmw::URI<>("mds://127.0.0.1:2346")));
    // REQUIRE(server.bind(INTERNAL_ADDRESS_BROKER));

    TestNode<BasicMdpMessage<MessageFormat::WithSourceId>> client(context, ZMQ_SUB);
    REQUIRE(client.connect(opencmw::URI<>("mds://127.0.0.1:2346")));
    // REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

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
