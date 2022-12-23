#include "helpers.hpp"

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

CMRC_DECLARE(assets);

using opencmw::majordomo::Broker;
using opencmw::majordomo::BrokerMessage;
using opencmw::majordomo::Command;
using opencmw::majordomo::MdpMessage;
using opencmw::majordomo::MessageFrame;
using opencmw::majordomo::Settings;
using opencmw::majordomo::Worker;

using opencmw::majordomo::DEFAULT_REST_PORT;
using opencmw::majordomo::PLAIN_HTTP;

using opencmw::Annotated;
using opencmw::NoUnit;

struct TestContext {
    opencmw::TimingCtx      ctx;                               ///< doesn't make much sense here, but it is a good opportunity to test this class
    opencmw::MIME::MimeType contentType = opencmw::MIME::HTML; ///< Used for de-/serialisation
};
ENABLE_REFLECTION_FOR(TestContext, contentType)

struct AddressRequest {
    int id;
};
ENABLE_REFLECTION_FOR(AddressRequest, id)

struct AddressEntry {
    int                                                  id;
    Annotated<std::string, NoUnit, "Name of the person"> name;
    std::string                                          street;
    Annotated<int, NoUnit, "Number">                     streetNumber;
    std::string                                          postalCode;
    std::string                                          city;
    bool                                                 isCurrent;
};
ENABLE_REFLECTION_FOR(AddressEntry, name, street, streetNumber, postalCode, city, isCurrent)

struct TestAddressHandler {
    std::unordered_map<int, AddressEntry> _entries;

    TestAddressHandler() {
        _entries.emplace(42, AddressEntry{ 42, "Santa Claus", "Elf Road", 123, "88888", "North Pole", true });
    }

    /**
     * The handler function that the handler is required to implement.
     */
    void operator()(opencmw::majordomo::RequestContext &rawCtx, const TestContext & /*requestContext*/, const AddressRequest &request, TestContext & /*replyContext*/, AddressEntry &output) {
        if (rawCtx.request.command() == Command::Get) {
            const auto it = _entries.find(request.id);
            if (it == _entries.end()) {
                output = _entries.cbegin()->second;
            } else {
                output = it->second;
            }
        } else if (rawCtx.request.command() == Command::Set) {
        }
    }
};

template<typename Mode, typename VirtualFS>
class SimpleTestRestBackend : public opencmw::majordomo::RestBackend<Mode, VirtualFS> {
    using super_t = opencmw::majordomo::RestBackend<Mode, VirtualFS>;

public:
    using super_t::RestBackend;

    static MdpMessage deserializeMessage(std::string_view method, std::string_view serialized) {
        // clang-format off
        auto result = MdpMessage::createClientMessage(
                method == "SUB" ? Command::Subscribe :
                method == "PUT" ? Command::Set :
                /* default */     Command::Get);
        // clang-format on

        // For the time being, just use ';' as frame separator. Not meant
        // to be a safe long-term solution:
        auto       currentBegin = serialized.cbegin();
        const auto bodyEnd      = serialized.cend();
        auto       currentEnd   = std::find(currentBegin, serialized.cend(), ';');

        for (std::size_t i = 2; i < result.requiredFrameCount(); ++i) {
            result.setFrameData(i, std::string_view(currentBegin, currentEnd), MessageFrame::dynamic_bytes_tag{});
            currentBegin = (currentEnd != bodyEnd) ? currentEnd + 1 : bodyEnd;
            currentEnd   = std::find(currentBegin, serialized.cend(), ';');
        }
        return result;
    }

    void registerHandlers() override {
        super_t::registerHandlers();
    }
};

std::jthread makeGetRequestResponseCheckerThread(const std::string &address, const std::string &requiredResponse, [[maybe_unused]] std::source_location location = std::source_location::current()) {
    return std::jthread([=] {
        httplib::Client http("localhost", DEFAULT_REST_PORT);
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
    Broker                                          broker("TestBroker", testSettings());
    auto                                            fs = cmrc::assets::get_filesystem();
    SimpleTestRestBackend<PLAIN_HTTP, decltype(fs)> rest(broker, fs);

    // For subscription matching, it is necessary that broker knows how to handle the query params "ctx" and "contentType".
    // ("ctx" needs to use the TimingCtxFilter, and "contentType" compare the mime types (currently simply a string comparison))
    // Here we register the members of TestContext as query params, with the member names being the keys, and using the member types
    // for correct matching.
    //
    // Note that the worker uses the same settings for matching, but as it knows about TestContext, it does this registration automatically.
    opencmw::query::registerTypes(TestContext(), broker);

    // Create MajordomoWorker with our domain objects, and our TestHandler.
    Worker<"addressbook", TestContext, AddressRequest, AddressEntry> worker(broker, TestAddressHandler());

    // Run worker and broker in separate threads
    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilServiceAvailable(broker.context, "addressbook"));

    auto httpThreadJSON = makeGetRequestResponseCheckerThread("/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=application%2Fjavascript", "Santa Claus");

    auto httpThreadHTML = makeGetRequestResponseCheckerThread("/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=text%2Fhtml", "<td class=\"propTable-fValue\">Elf Road</td>");
}
