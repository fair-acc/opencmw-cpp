#include <catch2/catch.hpp>
#define ENABLE_RESULT_CHECKS 1
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

    {
        client.get(URI<uri_check::STRICT>::UriFactory(uri).path("services/test").build());

        server.processRequest([](auto &&req, auto &&reply) {
            REQUIRE(req.command() == Command::Get);
            REQUIRE(req.body() == "");
            reply.setBody("42", MessageFrame::dynamic_bytes_tag{});
        });
        opencmw::client::RawMessage result;
        REQUIRE(client.receive(result));
        REQUIRE(result.data == std::vector<std::byte>{ std::byte{ '4' }, std::byte{ '2' } });
        REQUIRE(result.context.empty());
    }

    {
        client.set(URI<uri_check::STRICT>::UriFactory(uri).path("services/test").build(), byte_array_from_string("100"));

        server.processRequest([](auto &&req, auto &&reply) {
            REQUIRE(req.command() == Command::Set);
            REQUIRE(req.body() == "100");
            reply.setBody("", MessageFrame::dynamic_bytes_tag{});
        });

        opencmw::client::RawMessage result;
        REQUIRE(client.receive(result));
        REQUIRE(result.data.empty());
        REQUIRE(result.context.empty());
    }
}

TEST_CASE("Basic Client Subscription Test", "[Client]") {
    const Context               context{};
    MockServer                  server(context);

    std::vector<zmq_pollitem_t> pollitems{};
    SubscriptionClient          subscriptionClient(context, pollitems, 100ms, "subscriptionClientID");
    auto                        uri = URI<uri_check::STRICT>(server.addressSub());
    subscriptionClient.connect(uri);
    subscriptionClient.housekeeping(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()));

    auto endpoint = URI<uri_check::STRICT>::UriFactory(uri).path("a.service").build();

    subscriptionClient.subscribe(endpoint);
    std::this_thread::sleep_for(50ms); // allow for subscription to be established

    server.notify("a.service", "101");
    server.notify("a.service", "102");

    {
        opencmw::client::RawMessage result;
        REQUIRE(subscriptionClient.receive(result));
        REQUIRE(result.data == std::vector<std::byte>{ std::byte{ '1' }, std::byte{ '0' }, std::byte{ '1' } });
        REQUIRE(result.context.empty());
    }
    {
        opencmw::client::RawMessage result;
        REQUIRE(subscriptionClient.receive(result));
        REQUIRE(result.data == std::vector<std::byte>{ std::byte{ '1' }, std::byte{ '0' }, std::byte{ '2' } });
        REQUIRE(result.context.empty());
    }
    {
        REQUIRE_THROWS(subscriptionClient.get(endpoint));
        REQUIRE_THROWS(subscriptionClient.set(endpoint, {}));
    }
}

} // namespace opencmw_client_test
