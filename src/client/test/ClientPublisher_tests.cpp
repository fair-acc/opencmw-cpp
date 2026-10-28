#include <catch2/catch.hpp>

#include <Client.hpp>
#include <IoBuffer.hpp>
#include <MockServer.hpp>
#include <ThreadAffinity.hpp>

#include <algorithm>

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
        REQUIRE(message.data.asString() == "100");
        REQUIRE(message.error == "404");
        REQUIRE(message.rbac.asString() == "rbac");
        received++;
    });
    std::this_thread::sleep_for(20ms); // allow the request to reach the server
    server.processRequest([&endpoint](auto &&req, auto &reply) {
        REQUIRE(req.command == Command::Get);
        reply.data  = IoBuffer("100");
        reply.error = "404";
        reply.rbac  = IoBuffer("rbac");
        reply.topic = Message::URI::factory(endpoint).addQueryParameter("ctx", "test_ctx").build();
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
        reply.data  = IoBuffer();
        reply.topic = Message::URI::factory(endpoint).addQueryParameter("ctx", "test_ctx").build();
    });
    std::this_thread::sleep_for(10ms); // allow the reply to reach the client
    REQUIRE(received == 2);
}

struct PinThreadToSingleCpuGuard {
    std::vector<bool> previousAffinity = opencmw::thread::getThreadAffinity();

    PinThreadToSingleCpuGuard() {
        std::vector<bool> singleCpu(previousAffinity.size(), false);
        singleCpu[static_cast<std::size_t>(std::ranges::find(previousAffinity, true) - previousAffinity.begin())] = true;
        opencmw::thread::setThreadAffinity(singleCpu);
    }

    ~PinThreadToSingleCpuGuard() { opencmw::thread::setThreadAffinity(previousAffinity); }
};

TEST_CASE("Regression test for messages not coming together as a single multipart message", "[MDClientCtx]") {
    // this bug was that a message poller might see a message while it is only partially sent. With multiple cores, the issue
    // does not reproduce reliably because the sender is able to get in the second half of the message through before the
    // poller can read it
    const PinThreadToSingleCpuGuard pinGuard;
    constexpr int                   kNumRequests = 500;
    const zmq::Context              zctx{};
    MockServer                      server(zctx);
    MDClientCtx                     client(zctx, 20ms, "burstClient");
    const auto                      endpoint = URI<STRICT>::factory(URI<STRICT>(server.address())).scheme("mdp").path("/a.service").build();

    for (int i = 0; i < kNumRequests; ++i) {
        opencmw::client::Command set;
        set.command = Command::Set;
        set.topic   = endpoint;
        set.data    = IoBuffer("abc");
        client.request(std::move(set));
    }

    int setsReceived = 0;
    int setsWithData = 0;
    while (setsReceived < kNumRequests) {
        server.processRequest([&](auto &&req, auto &reply) {
            if (req.command == Command::Set) {
                setsReceived++;
                if (req.data.asString() == "abc") {
                    setsWithData++;
                }
            }
            reply.topic = Message::URI::factory(endpoint).build();
        });
    }

    REQUIRE(setsReceived == kNumRequests);
    REQUIRE(setsWithData == kNumRequests);
    client.stop();
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
        server.notify("/a.service?C=2", payload); // received by client
        server.notify("/a.service?C=3", payload); // not received by client
    }
    std::this_thread::sleep_for(10ms); // allow for all the notifications to reach the client
    std::print("received notifications {}\n", received.load());
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
