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

    MockClient   client(server.context());
    REQUIRE(client.connect(opencmw::URI<>(server.address())));
    // REQUIRE(client.connect(INTERNAL_ADDRESS_BROKER));

    client.get("a.service", {}, [](auto &&message) {
        REQUIRE(message.error == "");
        REQUIRE(message.data.asString() == "100");
    });
    server.processRequest([](auto &&req, auto &reply) {
        REQUIRE(req.command == mdp::Command::Get);
        reply.data = IoBuffer("100");
    });
    REQUIRE(client.tryRead(3s));

    client.set("a.service", IoBuffer("42"), [](auto &&message) {
        REQUIRE(message.error == "");
        REQUIRE(message.data.asString() == "Value set. All good!");
    });
    server.processRequest([](auto &&req, auto &reply) {
        REQUIRE(req.command == mdp::Command::Set);
        REQUIRE(req.data.asString() == "42");
        reply.data = IoBuffer("Value set. All good!");
    });
    REQUIRE(client.tryRead(3s));

    client.get("a.service", {}, [](auto &&message) {
        REQUIRE(message.error == "");
        REQUIRE(message.data.asString() == "42");
    });

    server.processRequest([](auto &&req, auto &reply) {
        REQUIRE(req.command == mdp::Command::Get);
        reply.data = IoBuffer("42");
    });

    REQUIRE(client.tryRead(3s));
}

TEST_CASE("MockServer Subscription Test", "[mock-server][lambda_handler]") {
    using opencmw::majordomo::MockServer;
    using namespace opencmw;
    using namespace std::chrono_literals;

    zmq::Context      context{};
    MockServer        server(context);

    BrokerMessageNode client(context, ZMQ_SUB);
    REQUIRE(client.connect(opencmw::URI<>(server.addressSub())));

    client.subscribe("a.service");
    std::this_thread::sleep_for(10ms); // wait for the subscription to be set-up. todo: investigate more clever way

    server.notify("a.service", "100");
    server.notify("a.service", "23");

    {
        auto reply = client.tryReadOne();
        fmt::print("{}\n", reply.has_value());
        REQUIRE(reply);
        REQUIRE(reply->data.asString() == "100");
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->endpoint.str() == "a.service");
    }
    {
        auto reply = client.tryReadOne();
        fmt::print("{}\n", reply.has_value());
        REQUIRE(reply);
        REQUIRE(reply->data.asString() == "23");
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->endpoint.str() == "a.service");
    }

    server.notify("a.service", "10");
    {
        auto reply = client.tryReadOne();
        fmt::print("{}\n", reply.has_value());
        REQUIRE(reply);
        REQUIRE(reply->data.asString() == "10");
        REQUIRE(reply->command == mdp::Command::Final);
        REQUIRE(reply->endpoint.str() == "a.service");
    }
}
