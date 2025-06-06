#include <RestClient.hpp>

#include "concepts/client/helpers.hpp"

using namespace std::chrono_literals;

// These are not main-local, as JS doesn't end when
// C++ main ends
namespace test {
#ifndef __EMSCRIPTEN__
opencmw::client::RestClient client(opencmw::client::VerifyServerCertificates(false));
#else
opencmw::client::RestClient client;
#endif

std::string schema() {
    if (auto env = ::getenv("DISABLE_REST_HTTPS"); env != nullptr && std::string_view(env) == "1") {
        return "http";
    } else {
        return "https";
    }
}

auto        get_uri              = opencmw::URI<opencmw::STRICT>(schema() + "://localhost:8080/addressbook?contentType=application/json&ctx=FAIR.SELECTOR.ALL");
auto        subscription_uri     = opencmw::URI<opencmw::STRICT>(schema() + "://localhost:8080/beverages/wine");

std::string initial_message_json = R"({
"name": "Santa Claus",
"street": "Elf Road",
"streetNumber": 123,
"postalCode": "88888",
"city": "North Pole",
"isCurrent": true
})";

std::string updated_message_json = R"({
"name": "Claus, Santa",
"street": "Elf Road",
"streetNumber": 123,
"postalCode": "88888",
"city": "North Pole",
"isCurrent": true
})";

using opencmw::mdp::Command;

auto initial_get_step  = rest_test_step<Command::Get>(client, get_uri, result_equals(initial_message_json));
auto set_step          = rest_test_step<Command::Set>(client, get_uri, updated_message_json);
auto later_get_step    = rest_test_step<Command::Get>(client, get_uri, result_equals(updated_message_json));
auto revert_step       = rest_test_step<Command::Set>(client, get_uri, initial_message_json);
auto reverted_get_step = rest_test_step<Command::Get>(client, get_uri, result_equals(initial_message_json));

auto subscribe_step    = rest_test_step<Command::Subscribe>(client, subscription_uri, no_check, 4);
auto unsubscribe_step  = rest_test_step<Command::Unsubscribe>(client, subscription_uri, no_check, 0);

auto run               = rest_test_runner(
        initial_get_step,
        set_step,
        later_get_step,
        revert_step,
        reverted_get_step,
        subscribe_step,
        unsubscribe_step);
} // namespace test

int main() {
    using namespace test;

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
