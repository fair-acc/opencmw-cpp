#include "ClientCommon.hpp"
#include "IoBuffer.hpp"
#include "IoSerialiser.hpp"
#include "IoSerialiserYaS.hpp"
#include <concepts/majordomo/helpers.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/LoadTestWorker.hpp>
#include <majordomo/Settings.hpp>
#include <MIME.hpp>
#include <opencmw.hpp>
#include <RestClient.hpp>
#include <TimingCtx.hpp>

#include <catch2/catch.hpp>

#include <array>

using namespace opencmw;

constexpr std::uint16_t kServerPort = 12355;

namespace {
template<typename T>
void waitFor(std::atomic<T> &responseCount, T expected, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto start = std::chrono::system_clock::now();
    while (responseCount.load() < expected && std::chrono::system_clock::now() - start < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const auto received = responseCount.load();
    if (received != expected) {
        FAIL(std::format("Expected {} responses, but got {}\n", expected, received));
    }
}
} // namespace

TEST_CASE("Load test", "[majordomo][majordomoworker][load_test][http2]") {
    majordomo::Broker             broker("/TestBroker", testSettings());
    majordomo::rest::Settings     rest;
    rest.port      = kServerPort;
    rest.protocols = majordomo::rest::Protocol::Http2;
    auto bound     = broker.bindRest(rest);
    if (!bound) {
        FAIL(std::format("Failed to bind REST server: {}", bound.error()));
        return;
    }

    query::registerTypes(opencmw::load_test::Context(), broker);

    majordomo::load_test::Worker worker(broker);

    RunInThread                  brokerRun(broker);
    RunInThread                  workerRun(worker);
    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    constexpr auto                                             kNClients              = 10UZ;
    constexpr auto                                             kSubscriptions         = 10UZ;
    constexpr bool                                             kSeparateSubscriptions = true;
    constexpr auto                                             kNUpdates              = 100UZ;
    constexpr auto                                             kIntervalMs            = 40UZ;
    constexpr auto                                             kInitialDelayMs        = 50UZ;
    constexpr auto                                             kPayloadSize           = 4096UZ;

    std::atomic<std::size_t>                                   responseCount = 0;

    std::array<std::unique_ptr<client::RestClient>, kNClients> clients;
    for (std::size_t i = 0; i < clients.size(); i++) {
        clients[i] = std::make_unique<client::RestClient>(client::DefaultContentTypeHeader(MIME::BINARY));
    }

    const auto start = std::chrono::system_clock::now();
    std::array<std::uint64_t, kNClients * kSubscriptions * kNUpdates> latencies;

    std::atomic<std::size_t>                                          responseCountOfi0j0 = 0;
    std::array<std::uint64_t, kNUpdates>                              latenciesOfi0j0;

    for (std::size_t i = 0; i < clients.size(); i++) {
        for (std::size_t j = 0; j < kSubscriptions; j++) {
            client::Command cmd;
            cmd.command      = mdp::Command::Subscribe;
            cmd.serviceName  = "/loadTest";
            const auto topic = std::format("{}:{}", kSeparateSubscriptions ? i : 0, j);
            cmd.topic        = URI<>(std::format("http://localhost:{}/loadTest?topic={}&intervalMs={}&payloadSize={}&nUpdates={}&initialDelayMs={}", kServerPort, topic, kIntervalMs, kPayloadSize, kNUpdates, kInitialDelayMs));
            cmd.callback     = [&responseCount, &responseCountOfi0j0, &latencies, &latenciesOfi0j0, kPayloadSize, i, j](const auto &msg) {
                REQUIRE(msg.command == mdp::Command::Notify);
                REQUIRE(msg.error == "");
                REQUIRE(msg.data.size() > 0);
                const auto                    index = responseCount.fetch_add(1);

                load_test::Payload            payload;
                try {
                    IoBuffer buffer{ msg.data };
                    opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buffer, payload);
                    REQUIRE(payload.data.size() == kPayloadSize);
                } catch (const opencmw::ProtocolException &e) {
                    FAIL(std::format("Failed to deserialise payload: {}", e.what()));
                    return;
                }
                const auto now     = opencmw::load_test::timestamp().count();
                const auto latency = now - payload.timestampNs;
                assert(latency >= 0);
                if (latency < 0) {
                    std::print("Negative latency: {} ({} - {})\n", latency, now, payload.timestampNs);
                }
                latencies[index] = static_cast<std::uint64_t>(latency);
                if (i == 0 && j == 0) {
                    const auto idx       = responseCountOfi0j0.fetch_add(1);
                    latenciesOfi0j0[idx] = static_cast<std::uint64_t>(latency / 1000);
                }
            };
            clients[i]->request(std::move(cmd));
        }
    }

    waitFor(responseCount, kNUpdates * kSubscriptions * kNClients, 30s);

    std::println("Received {} responses in {}ms (Net production time: {}ms)", responseCount.load(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count(), kInitialDelayMs + (kNUpdates * kIntervalMs));
    // TODO maybe print more detailed distribution
    std::uint64_t sumLatency = 0;
    for (const auto &latency : latencies) {
        sumLatency += latency;
    }

    const auto averageLatency = static_cast<double>(sumLatency) / static_cast<double>(responseCount.load());
    std::println("Average latency: {}µs", averageLatency / 1000.0);
    // TODO compute drift over time
}
