#include <majordomo/base64pp.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>
#include <majordomo/Worker.hpp>

#include <atomic>
#include <fstream>
#include <iomanip>
#include <thread>

using namespace opencmw::majordomo;

CMRC_DECLARE(testImages);
CMRC_DECLARE(assets);

// from restserver_testapp.cpp
template<typename Mode, typename VirtualFS, role... Roles>
class FileServerRestBackend : public RestBackend<Mode, VirtualFS, Roles...> {
private:
    using super_t = RestBackend<Mode, VirtualFS, Roles...>;
    std::filesystem::path _serverRoot;
    using super_t::_svr;
    using super_t::DEFAULT_REST_SCHEME;

public:
    using super_t::RestBackend;

    FileServerRestBackend(Broker<Roles...> &broker, const VirtualFS &vfs, std::filesystem::path serverRoot, opencmw::URI<> restAddress = opencmw::URI<>::factory().scheme(DEFAULT_REST_SCHEME).hostName("0.0.0.0").port(DEFAULT_REST_PORT).build())
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

struct TestContext {
    opencmw::TimingCtx      ctx;
    std::string             testFilter;
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};

// TODO using unsupported types throws in the mustache serialiser, the exception isn't properly handled,
// the browser just shows a bit of gibberish instead of the error message.

ENABLE_REFLECTION_FOR(TestContext, ctx, testFilter, contentType)

struct Request {
    std::string             name;
    opencmw::TimingCtx      timingCtx;
    std::string             customFilter;
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};

ENABLE_REFLECTION_FOR(Request, name, timingCtx, customFilter /*, contentType*/)

struct Reply {
    // TODO java demonstrates custom enums here - we don't support that, but also the example doesn't need it
    /*
    enum class Option {
        REPLY_OPTION1,
        REPLY_OPTION2,
        REPLY_OPTION3,
        REPLY_OPTION4
    };
*/
    std::string        name;
    bool               booleanReturnType;
    int8_t             byteReturnType;
    int16_t            shortReturnType;
    int32_t            intReturnType;
    int64_t            longReturnType;
    std::string        byteArray;
    opencmw::TimingCtx timingCtx;
    std::string        lsaContext;
    // Option replyOption = Option::REPLY_OPTION2;
};

ENABLE_REFLECTION_FOR(Reply, name, booleanReturnType, byteReturnType, shortReturnType, intReturnType, longReturnType, timingCtx, lsaContext /*, replyOption*/)

struct HelloWorldHandler {
    std::string customFilter = "uninitialised";

    void        operator()(RequestContext &rawCtx, const TestContext &requestContext, const Request &in, TestContext &replyContext, Reply &out) {
        using namespace std::chrono;
        const auto now        = system_clock::now();
        const auto sinceEpoch = system_clock::to_time_t(now);
        out.name              = fmt::format("Hello World! The local time is: {}", std::put_time(std::localtime(&sinceEpoch), "%Y-%m-%d %H:%M:%S"));
        out.byteArray         = in.name; // doesn't really make sense atm
        out.byteReturnType    = 42;

        out.timingCtx         = opencmw::TimingCtx(3, {}, {}, {}, duration_cast<microseconds>(now.time_since_epoch()));
        if (rawCtx.request.command() == Command::Set) {
            customFilter = in.customFilter;
        }
        out.lsaContext           = customFilter;

        replyContext.ctx         = out.timingCtx;
        replyContext.ctx         = opencmw::TimingCtx(3, {}, {}, {}, duration_cast<microseconds>(now.time_since_epoch()));
        replyContext.contentType = requestContext.contentType;
        replyContext.testFilter  = fmt::format("HelloWorld - reply topic = {}", requestContext.testFilter);
    }
};

struct ImageData {
    std::string base64;
    // TODO MimeType currently not serialisable by YaS/Json/cmwlight serialisers
    std::string contentType;
};

ENABLE_REFLECTION_FOR(ImageData, base64, contentType)

struct BinaryData {
    std::string resourceName;
    ImageData   image;
};

ENABLE_REFLECTION_FOR(BinaryData, resourceName, image)

std::string_view stripStart(std::string_view s, std::string_view prefix) {
    if (s.starts_with(prefix)) {
        s.remove_prefix(prefix.size());
    }
    return s;
}

using opencmw::majordomo::Empty;

template<units::basic_fixed_string serviceName, typename... Meta>
class ImageServiceWorker : public Worker<serviceName, TestContext, Empty, BinaryData, Meta...> {
    std::vector<std::vector<std::uint8_t>> imageData;
    std::atomic<std::size_t>               selectedImage;
    std::atomic<bool>                      shutdownRequested;
    std::jthread                           notifyThread;

    static constexpr auto                  PROPERTY_NAME = std::string_view("testImage");

public:
    using super_t = Worker<serviceName, TestContext, Empty, BinaryData, Meta...>;

    template<typename BrokerType>
    explicit ImageServiceWorker(const BrokerType &broker, std::chrono::milliseconds updateInterval)
        : super_t(broker, {}) {
        const auto fs = cmrc::testImages::get_filesystem();
        for (const auto &path : fs.iterate_directory("/testImages")) {
            if (path.is_file()) {
                const auto file = fs.open(fmt::format("testImages/{}", path.filename()));
                imageData.push_back(std::vector<std::uint8_t>(file.begin(), file.end()));
            }
        }
        assert(!imageData.empty());

        notifyThread = std::jthread([this, updateInterval] {
            while (!shutdownRequested) {
                std::this_thread::sleep_for(updateInterval);
                selectedImage = (selectedImage + 1) % imageData.size();
                TestContext context;
                // TODO ideally we could send this notification to any subscription independent of their contentType
                context.contentType = opencmw::MIME::JSON;
                BinaryData reply;
                reply.resourceName      = "test.png";
                reply.image.base64      = base64pp::encode(imageData[selectedImage]);
                reply.image.contentType = "image/png"; // MIME::PNG;
                // TODO the subscription via REST has a leading slash, so this "/" is necessary for it to match, check if that can be avoided
                super_t::notify("/", context, reply);
            }
        });

        super_t::setCallback([this](RequestContext &rawCtx, const TestContext &, const Empty &, TestContext &, BinaryData &out) {
            using namespace opencmw;
            const auto topicPath  = URI<RELAXED>(std::string(rawCtx.request.topic())).path().value_or("");
            const auto path       = stripStart(topicPath, "/");
            out.resourceName      = stripStart(stripStart(path, PROPERTY_NAME), "/");
            out.image.base64      = base64pp::encode(imageData[selectedImage]);
            out.image.contentType = "image/png"; // MIME::PNG;
        });
    }

    ~ImageServiceWorker() {
        shutdownRequested = true;
        notifyThread.join();
    }
};

int main() {
    using opencmw::URI;

    // note: inconsistency: brokerName as ctor argument, worker's serviceName as NTTP
    // note: default roles different from java (has: ADMIN, READ_WRITE, READ_ONLY, ANYONE, NULL)
    Broker                                          primaryBroker("PrimaryBroker");
    auto                                            fs = cmrc::assets::get_filesystem();

    FileServerRestBackend<PLAIN_HTTP, decltype(fs)> rest(primaryBroker, fs, "./");

    const auto                                      brokerRouterAddress = primaryBroker.bind(URI<>("mds://127.0.0.1:12345"));

    if (!brokerRouterAddress) {
        std::cerr << "Could not bind to broker address" << std::endl;
        return 1;
    }

    // note: our thread handling is very verbose, offer nicer API
    std::jthread primaryBrokerThread([&primaryBroker] {
        primaryBroker.run();
    });

    // second broker to test DNS functionalities
    Broker       secondaryBroker("SecondaryTestBroker", { .dnsAddress = *brokerRouterAddress });

    std::jthread secondaryBrokerThread([&secondaryBroker] {
        secondaryBroker.run();
    });

    // TODO IIRC we agreed that service names should be valid URIs and thus have a / prepended, but "/helloWorld"
    // doesn't work with the REST interface (http://localhost:8080/helloWorld and http://localhost:8080//helloWorld are mapped to "helloWorld")

    // TODO '"Reply": { "name": ... }' isn't valid json I think (not an object at top-level; also, Firefox doesn't like it). Should we omit the '"Reply:"?

    Worker<"helloWorld", TestContext, Request, Reply, description<"A friendly service saying hello">> helloWorldWorker(primaryBroker, HelloWorldHandler());
    ImageServiceWorker<"testImage", description<"Returns an image">>                                  imageWorker(primaryBroker, std::chrono::seconds(10));

    std::jthread                                                                                      helloWorldThread([&helloWorldWorker] {
        helloWorldWorker.run();
                                                                                         });

    std::jthread                                                                                      imageThread([&imageWorker] {
        imageWorker.run();
                                                                                         });

    primaryBrokerThread.join();

    secondaryBroker.shutdown();
    secondaryBrokerThread.join();
    // workers terminate when broker shuts down
    helloWorldThread.join();
    imageThread.join();
}
