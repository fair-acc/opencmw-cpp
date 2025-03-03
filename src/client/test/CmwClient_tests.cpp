#include <catch2/catch.hpp>

#include <Client.hpp>
#include <MockServer.hpp>

namespace opencmw_client_test {

using opencmw::client::Client;
using opencmw::client::SubscriptionClient;
using opencmw::majordomo::MockServer;
using opencmw::mdp::Command;
using opencmw::mdp::Message;
using opencmw::mdp::Topic;
using namespace opencmw;
using namespace std::chrono_literals;

static std::span<const std::byte> byte_array_from_string(const std::string_view &string) {
    return { reinterpret_cast<const std::byte *>(string.data()), string.length() };
}

TEST_CASE("Basic Client Get/Set Test", "[Client]") {
    const zmq::Context          context{};
    MockServer                  server(context);

    std::vector<zmq_pollitem_t> pollitems{};
    opencmw::client::Client     client(context, pollitems);
    auto                        uri = URI<uri_check::STRICT>(server.address());

    SECTION("Get") {
        std::string reqId = "1";
        client.get(URI<uri_check::STRICT>::UriFactory(uri).path("services/test").build(), reqId);

        server.processRequest([&uri](auto &&req, auto &&reply) {
            REQUIRE(req.command == Command::Get);
            REQUIRE(req.data.empty());
            REQUIRE(req.clientRequestID.asString() == "1");
            reply.data  = opencmw::IoBuffer("42");
            reply.topic = Message::URI::factory(uri).addQueryParameter("ctx", "test_ctx1").build();
        });
        Message result;
        REQUIRE(client.receive(result));
        REQUIRE(result.data.asString() == "42");
    }

    SECTION("Set") {
        std::string reqId = "2";
        client.set(URI<uri_check::STRICT>::UriFactory(uri).path("services/test").build(), reqId, byte_array_from_string("100"));

        server.processRequest([&uri](auto &&req, auto &&reply) {
            REQUIRE(req.command == Command::Set);
            REQUIRE(req.data.asString() == "100");
            REQUIRE(req.clientRequestID.asString() == "2");
            reply.data  = opencmw::IoBuffer();
            reply.topic = Message::URI::factory(uri).addQueryParameter("ctx", "test_ctx2").build();
        });

        Message result;
        REQUIRE(client.receive(result));
        REQUIRE(result.data.empty());
    }
}

TEST_CASE("Basic Client Subscription Test", "[Client]") {
    using URI = URI<uri_check::STRICT>;
    const zmq::Context          context{};
    MockServer                  server(context);

    std::vector<zmq_pollitem_t> pollitems{};
    SubscriptionClient          subscriptionClient(context, pollitems, 100ms, "subscriptionClientID");
    auto                        uri = URI(server.addressSub());

    subscriptionClient.connect(uri);
    subscriptionClient.housekeeping(std::chrono::system_clock::now());

    // When connecting directly to a worker instead of broker, subscribing do a generic prefix (e.g. "a.service") will not work, as ZMQ appends now "#" to the subscription. Instead, the full URI must be used.
    const auto endpoint1 = URI::UriFactory(uri).path("a.service").addQueryParameter("ctx", "test_ctx1").build();
    const auto endpoint2 = URI::UriFactory(uri).path("a.service").addQueryParameter("ctx", "test_ctx2").build();

    subscriptionClient.subscribe(endpoint1, "1");
    subscriptionClient.subscribe(endpoint2, "2");
    std::this_thread::sleep_for(50ms); // allow for subscription to be established

    server.notify("/a.service?ctx=test_ctx1#", "101");
    server.notify("/a.service?ctx=test_ctx2#", "102");

    Message resultOfNotify1;
    REQUIRE(subscriptionClient.receive(resultOfNotify1));
    REQUIRE(resultOfNotify1.data.asString() == "101");

    Message resultOfNotifyTwo;
    REQUIRE(subscriptionClient.receive(resultOfNotifyTwo));
    REQUIRE(resultOfNotifyTwo.data.asString() == "102");

    // receive expected exception
    std::string reqId2 = "9";
    REQUIRE_THROWS(subscriptionClient.get(endpoint1, reqId2));
    REQUIRE_THROWS(subscriptionClient.set(endpoint2, reqId2, {}));
}

} // namespace opencmw_client_test
