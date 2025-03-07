#include <atomic>
#include <string_view>
#include <thread>

#include <MIME.hpp>
#include <RestClient.hpp>
#include <URI.hpp>

#include "helpers.hpp"

using namespace std::chrono_literals;

// These are not main-local, as JS doesn't end when
// C++ main ends
namespace test_state {
#ifndef __EMSCRIPTEN__
opencmw::client::RestClient client(opencmw::client::VerifyServerCertificates(false));
#else
opencmw::client::RestClient client;
#endif
std::string                 schema() {
    if (auto env = ::getenv("DISABLE_REST_HTTPS"); env != nullptr && std::string_view(env) == "1") {
        return "http";
    } else {
        return "https";
    }
}

auto get_uri          = opencmw::URI<opencmw::STRICT>(schema() + "://localhost:8080/addressbook?contentType=application/json&ctx=FAIR.SELECTOR.ALL");
auto subscription_uri = opencmw::URI<opencmw::STRICT>(schema() + "://localhost:8080/beverages/wine");

using opencmw::mdp::Command;

auto get_step         = rest_test_step<Command::Get>(client, get_uri);
auto subscribe_step   = rest_test_step<Command::Subscribe>(client, subscription_uri, no_check, 4);
auto unsubscribe_step = rest_test_step<Command::Unsubscribe>(client, subscription_uri, no_check, 0);

auto run              = rest_test_runner(
        get_step,
        subscribe_step,
        unsubscribe_step);
} // namespace test_state

int main() {
    using namespace test_state;

#ifndef __EMSCRIPTEN__
    std::print("Waiting for everything to finish");
    auto waiting_since = std::chrono::system_clock::now();
    while (!run.all_done && std::chrono::system_clock::now() - waiting_since < 20s) {
        std::this_thread::sleep_for(250ms);
    }
    if (!run.all_done) {
        std::cerr << "Test timed out\n";
        return 1;
    }
#endif
}
