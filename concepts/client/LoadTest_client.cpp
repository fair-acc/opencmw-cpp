#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>

#include <MIME.hpp>
#include <RestClient.hpp>
#include <URI.hpp>

#include "ClientCommon.hpp"
#include "helpers.hpp"

using namespace std::chrono_literals;

std::string                 schema() {
    if (auto env = ::getenv("DISABLE_REST_HTTPS"); env != nullptr && std::string_view(env) == "1") {
        return "http";
    } else {
        return "https";
    }
}


int main() {
    constexpr auto kServerPort     =  8080;
    constexpr auto kNClients       =  80UZ;
    constexpr auto kNSubscriptions = 10UZ;
    constexpr auto kNUpdates       = 5000UZ;
    constexpr auto kIntervalMs     = 40UZ;
    constexpr auto kPayloadSize    = 4096UZ;

    std::array<std::unique_ptr<opencmw::client::RestClient>, kNClients> clients;
    for (std::size_t i = 0; i < clients.size(); i++) {
        clients[i] = std::make_unique<opencmw::client::RestClient>(opencmw::client::DefaultContentTypeHeader(opencmw::MIME::BINARY), opencmw::client::VerifyServerCertificates(false));
    }
    std::atomic<std::size_t> responseCount = 0;

    const auto start = std::chrono::system_clock::now();

    for (std::size_t i = 0; i < kNClients; i++) {
        for (std::size_t j = 0; j < kNSubscriptions; j++) {
            opencmw::client::Command cmd;
            cmd.command     = opencmw::mdp::Command::Subscribe;
            cmd.serviceName = "/loadTest";
            cmd.topic       = opencmw::URI<>(std::format("{}://localhost:{}/loadTest?initialDelayMs=1000&topic={}&intervalMs={}&payloadSize={}&nUpdates={}", schema(), kServerPort, /*i,*/ j, kIntervalMs, kPayloadSize, kNUpdates));
            cmd.callback    = [&responseCount](const auto &msg) {
                responseCount++;
            };
            clients[i]->request(std::move(cmd));
        }
    }

    constexpr auto expectedResponses = kNClients * kNSubscriptions * kNUpdates;

    std::uint64_t counter = 0;
    while (responseCount < expectedResponses) {
        counter += 50;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (counter % 20 == 0) {
            std::println("Received {} of {} responses", responseCount.load(), expectedResponses);
        }
    }

    const auto end = std::chrono::system_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::println("Elapsed time: {} ms", elapsed);
    return 0;
}
