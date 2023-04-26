#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>

#include <MIME.hpp>
#include <opencmw.hpp>
#include <TimingCtx.hpp>

#include <catch2/catch.hpp>
#include <fmt/format.h>
#include <refl.hpp>

#include <exception>
#include <unordered_map>
#include <thread>

// Concepts and tests use common types
#include <concepts/majordomo/helpers.hpp>

std::jthread makeGetRequestResponseCheckerThread(const std::string &address, const std::string &requiredResponse, [[maybe_unused]] std::source_location location = std::source_location::current()) {
    return std::jthread([=] {
        httplib::Client http("localhost", majordomo::DEFAULT_REST_PORT);
        http.set_keep_alive(true);
        const auto response = http.Get(address.data());

#define requireWithSource(arg) \
    if (!(arg)) opencmw::debug::withLocation(location) << "<- call got a failed requirement:"; \
    REQUIRE(arg)
        requireWithSource(response);
        requireWithSource(response->status == 200);
        requireWithSource(response->body.find(requiredResponse) != std::string::npos);
#undef requireWithSource
    });
}

TEST_CASE("Simple MajordomoWorker example showing its usage", "[majordomo][majordomoworker][simple_example]") {
    // We run both broker and worker inproc
    majordomo::Broker                                          broker("TestBroker", testSettings());
    auto                                                       fs = cmrc::assets::get_filesystem();
    FileServerRestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest(broker, fs);
    RunInThread restServerRun(rest);

    // For subscription matching, it is necessary that broker knows how to handle the query params "ctx" and "contentType".
    // ("ctx" needs to use the TimingCtxFilter, and "contentType" compare the mime types (currently simply a string comparison))
    // Here we register the members of TestContext as query params, with the member names being the keys, and using the member types
    // for correct matching.
    //
    // Note that the worker uses the same settings for matching, but as it knows about TestContext, it does this registration automatically.
    opencmw::query::registerTypes(SimpleContext(), broker);

    // Create MajordomoWorker with our domain objects, and our TestHandler.
    majordomo::Worker<"addressbook", SimpleContext, AddressRequest, AddressEntry> worker(broker, TestAddressHandler());

    // Run worker and broker in separate threads
    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "addressbook"));

    auto httpThreadJSON = makeGetRequestResponseCheckerThread("/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=application%2Fjavascript", "Santa Claus");

    auto httpThreadHTML = makeGetRequestResponseCheckerThread("/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=text%2Fhtml", "<td class=\"propTable-fValue\">Elf Road</td>");
}
