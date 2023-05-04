#include <catch2/catch.hpp>

#include <Client.hpp>
#include <IoBuffer.hpp>
#include <MockServer.hpp>

namespace opencmw_client_publisher_test {

using opencmw::client::Client;
using opencmw::client::ClientContext;
using opencmw::client::MDClientCtx;
using opencmw::client::SubscriptionClient;
using opencmw::disruptor::Disruptor;
using opencmw::majordomo::MockServer;
using opencmw::mdp::Command;
using opencmw::mdp::Message;
using opencmw::uri_check::STRICT;
using namespace opencmw;
using namespace std::chrono_literals;

TEST_CASE("Basic get/set test", "[ClientContext]") {
    const zmq::Context                                        zctx{};
    MockServer                                                server(zctx);
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<MDClientCtx>(zctx, 20ms, "testMajordomoClient"));
    ClientContext clientContext{ std::move(clients) };
    // send some requests
    auto             endpoint = URI<STRICT>::factory(URI<STRICT>(server.address())).scheme("mdp").path("/a.service").addQueryParameter("C", "2").build();
    std::atomic<int> received{ 0 };
    clientContext.get(endpoint, [&received](const Message &message) {
        REQUIRE(message.data.size() == 3); // == "100");
        received++;
    });
    std::this_thread::sleep_for(20ms); // allow the request to reach the server
    server.processRequest([&endpoint](auto &&req, auto &reply) {
        REQUIRE(req.command == Command::Get);
        reply.data = IoBuffer("100");
        reply.endpoint = Message::URI::factory(endpoint).addQueryParameter("ctx", "test_ctx").build();
    });
    std::this_thread::sleep_for(20ms); // hacky: this is needed because the requests are only identified using their uri, so we cannot have multiple requests with identical uris
    auto              testData = std::vector<std::byte>{ std::byte{ 'a' }, std::byte{ 'b' }, std::byte{ 'c' } };
    opencmw::IoBuffer dataSetRequest;
    dataSetRequest.put('a');
    dataSetRequest.put('b');
    dataSetRequest.put('c');
    clientContext.set(
            endpoint, [&received](const Message &message) {
                REQUIRE(message.data.size() == 0); // == "100");
                received++;
            },
            std::move(dataSetRequest));
    std::this_thread::sleep_for(20ms); // allow the request to reach the server
    server.processRequest([&endpoint](auto &&req, auto &reply) {
        REQUIRE(req.command == Command::Set);
        REQUIRE(req.data.asString() == "abc");
        reply.data = IoBuffer();
        reply.endpoint = Message::URI::factory(endpoint).addQueryParameter("ctx", "test_ctx").build();
    });
    std::this_thread::sleep_for(10ms); // allow the reply to reach the client
    REQUIRE(received == 2);
    clientContext.stop();
}

TEST_CASE("Basic subscription test", "[ClientContext]") {
    constexpr std::string_view                                payload = "payload";
    const zmq::Context                                        zctx{};
    MockServer                                                server(zctx);
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<MDClientCtx>(zctx, 20ms, ""));
    ClientContext clientContext{ std::move(clients) };
    std::this_thread::sleep_for(100ms);
    // subscription
    auto             endpoint = URI<STRICT>::factory(URI<STRICT>(server.addressSub())).scheme("mds").path("/a.service").addQueryParameter("C", "2").build();
    std::atomic<int> received{ 0 };
    clientContext.subscribe(endpoint, [&received, &payload](const Message &update) {
        if (update.data.size() == payload.size()) {
            received++;
        }
    });
    std::this_thread::sleep_for(100ms); // allow for the subscription request to be processed
    // send notifications
    for (int i = 0; i < 100; i++) {
        server.notify(*endpoint.relativeRefNoFragment(), payload);
    }
    std::this_thread::sleep_for(10ms); // allow for all the notifications to reach the client
    fmt::print("received notifications {}\n", received);
    REQUIRE(received == 100);
    clientContext.unsubscribe(endpoint);
    std::this_thread::sleep_for(10ms); // allow for the unsubscription request to be processed
                                       //    // send notifications
                                       //    for (int i = 0; i < 100; i++) {
                                       //        server.notify("a.service?C=2", payload);
                                       //    }
                                       //    std::this_thread::sleep_for(10ms); // allow for all the notifications to reach the client
                                       //    REQUIRE(received == 100);
                                       //    std::this_thread::sleep_for(100ms); // allow for all the notifications to reach the client
    clientContext.stop();              // cleanup
}

} // namespace opencmw_client_publisher_test
