#include <catch2/catch.hpp>
#define ENABLE_RESULT_CHECKS 1
#include <Client.hpp>
#include <MockServer.hpp>

namespace opencmw_client_publisher_test {

using opencmw::URI;
using opencmw::uri_check;
using opencmw::client::Client;
using opencmw::client::ClientContext;
using opencmw::client::MDClientCtx;
using opencmw::client::SubscriptionClient;
using opencmw::disruptor::Disruptor;
using opencmw::majordomo::Command;
using opencmw::majordomo::Context;
using opencmw::majordomo::MessageFrame;
using opencmw::majordomo::MockServer;
using opencmw::uri_check::STRICT;
using namespace std::chrono_literals;

TEST_CASE("Basic get/set test", "[ClientContext]") {
    const Context                                             zctx{};
    MockServer                                                server(zctx);
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<MDClientCtx>(zctx, 20ms, "testMajordomoClient"));
    ClientContext clientContext{ std::move(clients) };
    // send some requests
    auto endpoint = URI<STRICT>::factory(URI<STRICT>(server.address())).scheme("mdp").path("/a.service").addQueryParameter("C", "2").build();
    fmt::print("issuing get request\n");
    std::atomic<int> received{ 0 };
    clientContext.get(endpoint, [&received](const opencmw::client::RawMessage &message) {
        REQUIRE(message.data.size() == 3); // == "100");
        REQUIRE(message.context == "test_ctx");
        received++;
    });
    std::this_thread::sleep_for(20ms); // allow the request to reach the server
    server.processRequest([&endpoint](auto &&req, auto &reply) {
        REQUIRE(req.command() == Command::Get);
        reply.setBody(std::to_string(100), MessageFrame::dynamic_bytes_tag{});
        reply.setTopic(URI<STRICT>::factory(endpoint).addQueryParameter("ctx", "test_ctx").build().str, MessageFrame::dynamic_bytes_tag{});
    });
    std::this_thread::sleep_for(20ms); // hacky: this is needed because the requests are only identified using their uri, so we cannot have multiple requests with identical uris
    fmt::print("issuing set request\n");
    clientContext.set(
            endpoint, [&received](const opencmw::client::RawMessage &message) {
                REQUIRE(message.data.empty()); // == "100");
                REQUIRE(message.context == "test_ctx");
                received++;
            },
            std::vector<std::byte>{ std::byte{ 'a' }, std::byte{ 'b' }, std::byte{ 'c' } });
    std::this_thread::sleep_for(20ms); // allow the request to reach the server
    server.processRequest([&endpoint](auto &&req, auto &reply) {
        REQUIRE(req.command() == Command::Set);
        REQUIRE(req.body() == "abc");
        reply.setBody(std::string(), MessageFrame::dynamic_bytes_tag{});
        reply.setTopic(URI<STRICT>::factory(endpoint).addQueryParameter("ctx", "test_ctx").build().str, MessageFrame::dynamic_bytes_tag{});
    });
    std::this_thread::sleep_for(10ms); // allow the reply to reach the client
    REQUIRE(received == 2);
    clientContext.stop();
}

TEST_CASE("Basic subscription test", "[ClientContext]") {
    const Context                                             zctx{};
    MockServer                                                server(zctx);
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<MDClientCtx>(zctx, 20ms, ""));
    ClientContext clientContext{ std::move(clients) };
    // subscription
    auto endpoint = URI<STRICT>::factory(URI<STRICT>(server.addressSub())).scheme("mds").path("/a.service").addQueryParameter("C", "2").build();
    fmt::print("subscribing\n");
    std::atomic<int> received{ 0 };
    clientContext.subscribe(endpoint, [&received](const opencmw::client::RawMessage &update) {
        if (update.data.size() == 7) {
            received++;
            fmt::print("v");
        } else {
            fmt::print("\nError: message of wrong length: {}\n", update.data.size());
            FAIL();
        }
    });
    std::this_thread::sleep_for(10ms); // allow for the subscription request to be processed
    // send notifications
    for (int i = 0; i < 100; i++) {
        server.notify("a.service", endpoint.str, "bar-baz");
        fmt::print("^");
    }
    std::this_thread::sleep_for(10ms); // allow for all the notifications to reach the client
    REQUIRE(received == 100);
    clientContext.unsubscribe(endpoint);
    fmt::print("\n");
    std::this_thread::sleep_for(10ms); // allow for the unsubscription request to be processed
    // send notifications
    for (int i = 0; i < 100; i++) {
        server.notify("a.service", endpoint.str, "bar-baz");
        fmt::print("^");
    }
    std::this_thread::sleep_for(10ms); // allow for all the notifications to reach the client
    REQUIRE(received == 100);
    std::this_thread::sleep_for(100ms); // allow for all the notifications to reach the client
    clientContext.stop();               // cleanup
}

} // namespace opencmw_client_publisher_test
