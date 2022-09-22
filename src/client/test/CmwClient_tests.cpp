#include <catch2/catch.hpp>

#include <Client.hpp>
#include <MockServer.hpp>

namespace opencmw_client_test {

using opencmw::URI;
using opencmw::uri_check;
using opencmw::client::Client;
using opencmw::client::SubscriptionClient;
using opencmw::majordomo::Command;
using opencmw::majordomo::Context;
using opencmw::majordomo::MessageFrame;
using opencmw::majordomo::MockServer;
using namespace std::chrono_literals;

static std::span<const std::byte> byte_array_from_string(const std::string_view &string) {
    return { reinterpret_cast<const std::byte *>(string.data()), string.length() };
}

TEST_CASE("Basic Client Get/Set Test", "[Client]") {
    const Context               context{};
    MockServer                  server(context);

    std::vector<zmq_pollitem_t> pollitems{};
    opencmw::client::Client     client(context, pollitems);
    auto                        uri = URI<uri_check::STRICT>(server.address());

    SECTION("Get") {
        auto reqId = MessageFrame("1", MessageFrame::dynamic_bytes_tag{});
        client.get(URI<uri_check::STRICT>::UriFactory(uri).path("services/test").build(), reqId);

        server.processRequest([&uri](auto &&req, auto &&reply) {
            REQUIRE(req.command() == Command::Get);
            REQUIRE(req.body() == "");
            REQUIRE(req.clientRequestId() == "1");
            reply.setBody("42", MessageFrame::dynamic_bytes_tag{});
            reply.setTopic(URI<uri_check::STRICT>::factory(uri).addQueryParameter("ctx", "test_ctx1").build().str(), MessageFrame::dynamic_bytes_tag{});
        });
        opencmw::mdp::Message result;
        REQUIRE(client.receive(result));
        REQUIRE(result.data.asString() == "42");
    }

    SECTION("Set") {
        auto reqId = MessageFrame("2", MessageFrame::dynamic_bytes_tag{});
        client.set(URI<uri_check::STRICT>::UriFactory(uri).path("services/test").build(), reqId, byte_array_from_string("100"));

        server.processRequest([&uri](auto &&req, auto &&reply) {
            REQUIRE(req.command() == Command::Set);
            REQUIRE(req.body() == "100");
            REQUIRE(req.clientRequestId() == "2");
            reply.setBody("", MessageFrame::dynamic_bytes_tag{});
            reply.setTopic(URI<uri_check::STRICT>::factory(uri).addQueryParameter("ctx", "test_ctx2").build().str(), MessageFrame::dynamic_bytes_tag{});
        });

        opencmw::mdp::Message result;
        REQUIRE(client.receive(result));
        REQUIRE(result.data.size() == 0);
    }
}

TEST_CASE("Basic Client Subscription Test", "[Client]") {
    const Context               context{};
    MockServer                  server(context);

    std::vector<zmq_pollitem_t> pollitems{};
    SubscriptionClient          subscriptionClient(context, pollitems, 100ms, "subscriptionClientID");
    auto                        uri = URI<uri_check::STRICT>(server.addressSub());
    subscriptionClient.connect(uri);
    subscriptionClient.housekeeping(std::chrono::system_clock::now());

    auto endpoint = URI<uri_check::STRICT>::UriFactory(uri).path("a.service").build();

    auto reqId    = MessageFrame("2", MessageFrame::dynamic_bytes_tag{});
    subscriptionClient.subscribe(endpoint, reqId);
    std::this_thread::sleep_for(50ms); // allow for subscription to be established

    server.notify("/a.service", URI<uri_check::STRICT>::factory(endpoint).addQueryParameter("ctx", "test_ctx1").build().str(), "101");
    server.notify("/a.service", URI<uri_check::STRICT>::factory(endpoint).addQueryParameter("ctx", "test_ctx2").build().str(), "102");

    opencmw::mdp::Message resultOfNotify1;
    REQUIRE(subscriptionClient.receive(resultOfNotify1));
    REQUIRE(resultOfNotify1.data.asString() == "101");

    opencmw::mdp::Message resultOfNotifyTwo;
    REQUIRE(subscriptionClient.receive(resultOfNotifyTwo));
    REQUIRE(resultOfNotifyTwo.data.asString() == "102");

    // receive expected exception
    auto reqId2 = MessageFrame("9", MessageFrame::dynamic_bytes_tag{});
    REQUIRE_THROWS(subscriptionClient.get(endpoint, reqId2));
    REQUIRE_THROWS(subscriptionClient.set(endpoint, reqId2, {}));
}

} // namespace opencmw_client_test
