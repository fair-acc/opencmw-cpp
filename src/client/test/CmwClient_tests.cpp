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
    const Context context{};
    MockServer    server(context);

    Client        client(context, 1000ms, "clientID");
    auto          uri = URI<uri_check::RELAXED>(server.address());

    {
        client.connect(uri);
        client.housekeeping(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()));
        // std::this_thread::sleep_for(100ms);

        client.get(URI<uri_check::RELAXED>::UriFactory(uri).path("services/test").build());

        server.processRequest([](auto &&req, auto &&reply) {
            REQUIRE(req.command() == Command::Get);
            REQUIRE(req.body() == "");
            reply.setBody("42", MessageFrame::dynamic_bytes_tag{});
        });
        opencmw::client::RawMessage result;
        REQUIRE(client.read(result));
        REQUIRE(result.data == std::vector<std::byte>{ std::byte{ '4' }, std::byte{ '2' } });
        REQUIRE(result.context.empty());
    }

    {
        client.set(URI<uri_check::RELAXED>::UriFactory(uri).path("services/test").build(), byte_array_from_string("100"));

        server.processRequest([](auto &&req, auto &&reply) {
            REQUIRE(req.command() == Command::Set);
            REQUIRE(req.body() == "100");
            reply.setBody("", MessageFrame::dynamic_bytes_tag{});
        });

        opencmw::client::RawMessage result;
        REQUIRE(client.read(result));
        REQUIRE(result.data == std::vector<std::byte>{});
        REQUIRE(result.context.empty());
    }

    // this would be the code for fallback to mdp protocol
    // server.processRequest([](auto &&req, auto &&reply) {
    //     REQUIRE(req.command() == Command::Subscribe);
    //     REQUIRE(req.body() == "");
    //     reply.setBody("", MessageFrame::dynamic_bytes_tag{});
    // });
    // {
    //     auto result = client.read();
    //     REQUIRE(result);
    //     auto expected = ""s;
    //     REQUIRE(result->data == std::vector<uint8_t>(expected.begin(), expected.end()));
    //     REQUIRE(result->context.empty());
    // }
}

TEST_CASE("Basic Client Subscription Test", "[Client]") {
    const Context      context{};
    MockServer         server(context);

    SubscriptionClient subscriptionClient(context, 1000ms, "subscriptionClientID");
    auto               uri = URI<uri_check::RELAXED>(server.addressSub());
    subscriptionClient.connect(uri);
    subscriptionClient.housekeeping(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()));

    subscriptionClient.subscribe(URI<uri_check::RELAXED>::UriFactory(uri).path("a.service").build());
    std::this_thread::sleep_for(50ms); // allow for subscription to be established

    server.notify("a.service", "101");
    server.notify("a.service", "102");

    {
        opencmw::client::RawMessage result;
        REQUIRE(subscriptionClient.read(result));
        REQUIRE(result.data == std::vector<std::byte>{ std::byte{ '1' }, std::byte{ '0' }, std::byte{ '1' } });
        REQUIRE(result.context.empty());
    }
    {
        opencmw::client::RawMessage result;
        REQUIRE(subscriptionClient.read(result));
        REQUIRE(result.data == std::vector<std::byte>{ std::byte{ '1' }, std::byte{ '0' }, std::byte{ '2' } });
        REQUIRE(result.context.empty());
    }
}

} // namespace opencmw_client_test
