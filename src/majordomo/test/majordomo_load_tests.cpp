#include "ClientCommon.hpp"
#include "IoBuffer.hpp"
#include "IoSerialiser.hpp"
#include "IoSerialiserYaS.hpp"
#include <majordomo/Broker.hpp>
#include <majordomo/LoadTestWorker.hpp>
#include <majordomo/Settings.hpp>
#include <MIME.hpp>
#include <opencmw.hpp>
#include <RestClient.hpp>
#include <TimingCtx.hpp>

#include <catch2/catch.hpp>

#include <array>

// Concepts and tests use common types
#include <concepts/majordomo/helpers.hpp>

using namespace opencmw;

constexpr std::uint16_t kServerPort = 12348;

namespace {
template<typename T>
void waitFor(std::atomic<T> &responseCount, T expected, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto start = std::chrono::system_clock::now();
    while (responseCount.load() < expected && std::chrono::system_clock::now() - start < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const auto result = responseCount.load() == expected;
    if (!result) {
        FAIL(fmt::format("Expected {} responses, but got {}\n", expected, responseCount.load()));
    }
}
} // namespace

TEST_CASE("Load test", "[majordomo][majordomoworker][load_test][http2]") {
    majordomo::Broker             broker("/TestBroker", testSettings());
    majordomo::rest::Settings     rest;
    rest.port     = kServerPort;
    auto bound    = broker.bindRest(rest);
    if (!bound) {
        FAIL(fmt::format("Failed to bind REST server: {}", bound.error()));
        return;
    }

    query::registerTypes(opencmw::majordomo::load_test::Context(), broker);

    majordomo::load_test::Worker<"/loadTest"> worker(broker);

    RunInThread                               brokerRun(broker);
    RunInThread                               workerRun(worker);
    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    constexpr auto                                             kNClients     = 80UZ;
    constexpr auto                                             kNUpdates     = 100UZ;
    constexpr auto                                             kIntervalMs   = 300UZ;
    constexpr auto                                             kPayloadSize  = 40000UZ;

    std::atomic<std::size_t>                                   responseCount = 0;

    std::array<std::unique_ptr<client::RestClient>, kNClients> clients;
    for (std::size_t i = 0; i < clients.size(); i++) {
        clients[i] = std::make_unique<client::RestClient>(client::DefaultContentTypeHeader(MIME::BINARY));
    }

    const auto start = std::chrono::system_clock::now();
    std::array<int, kNClients * kNUpdates> latencies;

    for (std::size_t i = 0; i < clients.size(); i++) {
        client::Command cmd;
        cmd.command     = mdp::Command::Subscribe;
        cmd.serviceName = "/loadTest";
        cmd.topic       = URI<>(fmt::format("http://localhost:{}/loadTest?topic={}&intervalMs={}&payloadSize={}&nUpdates={}", kServerPort, i, kIntervalMs, kPayloadSize, kNUpdates));
        cmd.callback    = [&responseCount, &latencies, kPayloadSize](const auto &msg) {
            REQUIRE(msg.command == mdp::Command::Notify);
            REQUIRE(msg.error == "");
            REQUIRE(msg.data.size() > 0);
            const auto index = responseCount.fetch_add(1);

            majordomo::load_test::Payload payload;
            try {
                IoBuffer buffer{ msg.data };
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buffer, payload);
                REQUIRE(payload.data.size() == kPayloadSize);
            } catch (const opencmw::ProtocolException &e) {
                FAIL(fmt::format("Failed to deserialise payload: {}", e.what()));
                return;
            }
            const auto latency = majordomo::load_test::timestampNs() - payload.timestampNs;
            latencies[index] = static_cast<int>(latency);
        };
        clients[i]->request(std::move(cmd));
    }

    waitFor(responseCount, kNUpdates * clients.size(), 1min);

    fmt::println("Received {} responses in {}ms (Net production time: {}ms)", responseCount.load(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count(), kNUpdates * kIntervalMs);
    // TODO maybe print more detailed distribution
    const auto averageLatency = std::accumulate(latencies.begin(), latencies.end(), 0) / static_cast<int>(latencies.size());
    fmt::println("Average latency: {}µs", averageLatency/ 1000);
    // TODO compute drift over time
}
