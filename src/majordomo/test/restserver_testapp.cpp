#include "helpers.hpp"

#include <majordomo/RestBackend.hpp>

#include <majordomo/Broker.hpp>
#include <majordomo/Client.hpp>
#include <majordomo/Debug.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/Utils.hpp>
#include <majordomo/Worker.hpp>

#include <fmt/format.h>

#include <charconv>
#include <cstdlib>
#include <deque>
#include <thread>
#include <variant>

CMRC_DECLARE(assets);

using namespace opencmw;
using namespace opencmw::majordomo;

struct TestContext {
    opencmw::TimingCtx      ctx;                               ///< doesn't make much sense here, but it is a good opportunity to test this class
    opencmw::MIME::MimeType contentType = opencmw::MIME::HTML; ///< Used for de-/serialisation
};
ENABLE_REFLECTION_FOR(TestContext, contentType)

struct AddressRequest {
    std::string name;
    std::string street;
    int         streetNumber;
    std::string postalCode;
    std::string city;
    bool        isCurrent;
};
ENABLE_REFLECTION_FOR(AddressRequest, name, street, streetNumber, postalCode, city, isCurrent)

struct AddressEntry {
    Annotated<std::string, NoUnit, "Name of the person"> name;
    std::string                                          street;
    Annotated<int, NoUnit, "Number">                     streetNumber;
    std::string                                          postalCode;
    std::string                                          city;
    bool                                                 isCurrent;
};
ENABLE_REFLECTION_FOR(AddressEntry, name, street, streetNumber, postalCode, city, isCurrent)

/**
 * The handler implementing the MajordomoHandler concept, here holding a single hardcoded address entry.
 */
struct TestAddressHandler {
    AddressEntry _entry;

    TestAddressHandler()
        : _entry{ "Santa Claus", "Elf Road", 123, "88888", "North Pole", true } {
    }

    /**
     * The handler function that the handler is required to implement.
     */
    void operator()(opencmw::majordomo::RequestContext &rawCtx, const TestContext & /*requestContext*/, const AddressRequest &request, TestContext & /*replyContext*/, AddressEntry &output) {
        if (rawCtx.request.command() == Command::Get) {
            output = _entry;
        } else if (rawCtx.request.command() == Command::Set) {
            _entry = AddressEntry{
                .name         = request.name,
                .street       = request.street,
                .streetNumber = request.streetNumber,
                .postalCode   = request.postalCode,
                .city         = request.city,
                .isCurrent    = request.isCurrent
            };
        }
    }
};

template<typename Mode, typename VirtualFS, role... Roles>
class FileServerRestBackend : public RestBackend<Mode, VirtualFS, Roles...> {
private:
    using super_t = RestBackend<Mode, VirtualFS, Roles...>;
    std::filesystem::path _serverRoot;
    using super_t::_svr;
    using super_t::DEFAULT_REST_SCHEME;

public:
    using super_t::RestBackend;

    FileServerRestBackend(Broker<Roles...> &broker, const VirtualFS &vfs, std::filesystem::path serverRoot, URI<> restAddress = URI<>::factory().scheme(DEFAULT_REST_SCHEME).hostName("0.0.0.0").port(DEFAULT_REST_PORT).build())
        : super_t(broker, vfs, restAddress), _serverRoot(std::move(serverRoot)) {
    }

    static auto deserializeSemicolonFormattedMessage(std::string_view method, std::string_view serialized) {
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
        _svr.set_mount_point("/", _serverRoot.string());

        _svr.Post("/stdio.html", [](const httplib::Request &request, httplib::Response &response) {
            opencmw::debug::log() << "QtWASM:" << request.body;
            response.set_content("", "text/plain");
        });

        auto cmrcHandler = [this](const httplib::Request &request, httplib::Response &response) {
            if (super_t::_vfs.is_file(request.path)) {
                auto file = super_t::_vfs.open(request.path);
                response.set_content(std::string(file.begin(), file.end()), "");
            }
        };

        _svr.Get("/assets/.*", cmrcHandler);

        // Register default handlers
        super_t::registerHandlers();
    }
};

int main(int argc, char **argv) {
    std::string rootPath = "./";
    if (argc > 1) {
        rootPath = argv[1];
    }

    std::cerr << "Starting server for " << rootPath << "\n";

    std::cerr << "Open up https://localhost:8080/addressbook/addresses?contentType=text/html&ctx=FAIR.SELECTOR.ALL in your web browser\n";

    // Majordomo broker
    Broker broker("testbroker", testSettings());
    opencmw::query::registerTypes(TestContext(), broker);

    // Make the REST server serve files from the Path specified by argv[1] or ./ by default
    static const auto fs = cmrc::assets::get_filesystem();
    std::variant<
            std::monostate,
            FileServerRestBackend<PLAIN_HTTP, decltype(fs)>,
            FileServerRestBackend<HTTPS, decltype(fs)>>
            rest;
    if (auto env = ::getenv("DISABLE_REST_HTTPS"); env != nullptr && std::string_view(env) == "1") {
        rest.emplace<FileServerRestBackend<PLAIN_HTTP, decltype(fs)>>(broker, fs, rootPath);
    } else {
        rest.emplace<FileServerRestBackend<HTTPS, decltype(fs)>>(broker, fs, rootPath);
    }

    // Majordomo Workers
    Worker<"addressbook", TestContext, AddressRequest, AddressEntry>  workerA(broker, TestAddressHandler());
    Worker<"addressbookB", TestContext, AddressRequest, AddressEntry> workerB(broker, TestAddressHandler());

    // Start threads for the broker and workers
    RunInThread brokerRun(broker);
    RunInThread workerARun(workerA);
    RunInThread workerBRun(workerB);

    waitUntilServiceAvailable(broker.context, "addressbook");

    // Fake message publisher - sends messages on notifier.service
    TestNode<MdpMessage> publisher(broker.context);
    publisher.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER);

    for (int i = 0; true; ++i) {
        std::cerr << "Sending new address (step " << i << ")\n";

        const auto entry = AddressEntry{
            .name         = "Easter Bunny",
            .street       = "Carrot Road",
            .streetNumber = i,
            .postalCode   = "88888",
            .city         = "Easter Island",
            .isCurrent    = false
        };
        workerA.notify("/addresses", TestContext{ .ctx = opencmw::TimingCtx(1, {}, {}, {}), .contentType = opencmw::MIME::JSON }, entry);

        std::this_thread::sleep_for(5s);
    }
}
