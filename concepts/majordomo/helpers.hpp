#ifndef CONCEPTS_MAJORDOMO_HELPERS_H
#define CONCEPTS_MAJORDOMO_HELPERS_H

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

// OpenCMW Core
#include <MIME.hpp>
#include <TimingCtx.hpp>

// OpenCMW Majordomo
#include <majordomo/base64pp.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/RestBackend.hpp>
#include <majordomo/Worker.hpp>

CMRC_DECLARE(testImages);
CMRC_DECLARE(assets);

namespace majordomo = opencmw::majordomo;
using namespace std::chrono_literals;
using namespace std::string_literals;

namespace detail {
inline std::string_view stripPrefix(std::string_view s, std::string_view prefix) {
    if (s.starts_with(prefix)) {
        s.remove_prefix(prefix.size());
    }
    return s;
}
} // namespace detail

struct SimpleContext {
    opencmw::TimingCtx      ctx;
    std::string             testFilter;
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};
// TODO using unsupported types throws in the mustache serialiser, the exception isn't properly handled,
// the browser just shows a bit of gibberish instead of the error message.
ENABLE_REFLECTION_FOR(SimpleContext, ctx, testFilter, contentType)

struct SimpleRequest {
    std::string             name;
    opencmw::TimingCtx      timingCtx;
    std::string             customFilter;
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};
ENABLE_REFLECTION_FOR(SimpleRequest, name, timingCtx, customFilter /*, contentType*/)

struct SimpleReply {
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
ENABLE_REFLECTION_FOR(SimpleReply, name, booleanReturnType, byteReturnType, shortReturnType, intReturnType, longReturnType, timingCtx, lsaContext /*, replyOption*/)

struct AddressEntry {
    opencmw::Annotated<std::string, opencmw::NoUnit, "Name of the person"> name;
    std::string                                                            street;
    opencmw::Annotated<int, opencmw::NoUnit, "Number">                     streetNumber;
    std::string                                                            postalCode;
    std::string                                                            city;
    bool                                                                   isCurrent;
};
ENABLE_REFLECTION_FOR(AddressEntry, name, street, streetNumber, postalCode, city, isCurrent)

struct AddressRequest {
    std::string name;
    std::string street;
    int         streetNumber;
    std::string postalCode;
    std::string city;
    bool        isCurrent;
};
ENABLE_REFLECTION_FOR(AddressRequest, name, street, streetNumber, postalCode, city, isCurrent)

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

struct TestAddressHandler {
    AddressEntry _entry = { "Santa Claus", "Elf Road", 123, "88888", "North Pole", true };

    /**
     * The handler function that the handler is required to implement.
     */
    void operator()(const opencmw::majordomo::RequestContext &rawCtx, const SimpleContext & /*requestContext*/, const AddressRequest &request, SimpleContext & /*replyContext*/, AddressEntry &output) {
        if (rawCtx.request.command == opencmw::mdp::Command::Get) {
            output = _entry;
        } else if (rawCtx.request.command == opencmw::mdp::Command::Set) {
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

struct HelloWorldHandler {
    std::string customFilter = "uninitialised";

    void        operator()(majordomo::RequestContext &rawCtx, const SimpleContext &requestContext, const SimpleRequest &in, SimpleContext &replyContext, SimpleReply &out) {
        using namespace std::chrono;
        const auto now        = system_clock::now();
        const auto sinceEpoch = system_clock::to_time_t(now);
        out.name              = fmt::format("Hello World! The local time is: {}", std::put_time(std::localtime(&sinceEpoch), "%Y-%m-%d %H:%M:%S"));
        out.byteArray         = in.name; // doesn't really make sense atm
        out.byteReturnType    = 42;

        out.timingCtx         = opencmw::TimingCtx(3, {}, {}, {}, duration_cast<microseconds>(now.time_since_epoch()));
        if (rawCtx.request.command == opencmw::mdp::Command::Set) {
            customFilter = in.customFilter;
        }
        out.lsaContext           = customFilter;

        replyContext.ctx         = out.timingCtx;
        replyContext.ctx         = opencmw::TimingCtx(3, {}, {}, {}, duration_cast<microseconds>(now.time_since_epoch()));
        replyContext.contentType = requestContext.contentType;
        replyContext.testFilter  = fmt::format("HelloWorld - reply topic = {}", requestContext.testFilter);
    }
};

template<units::basic_fixed_string serviceName, typename... Meta>
class ImageServiceWorker : public majordomo::Worker<serviceName, SimpleContext, majordomo::Empty, BinaryData, Meta...> {
    std::vector<std::vector<std::uint8_t>> imageData;
    std::atomic<std::size_t>               selectedImage;
    std::atomic<bool>                      shutdownRequested;
    std::jthread                           notifyThread;

    static constexpr auto                  PROPERTY_NAME = std::string_view("testImage");

public:
    using super_t = majordomo::Worker<serviceName, SimpleContext, majordomo::Empty, BinaryData, Meta...>;

    template<typename BrokerType>
    explicit ImageServiceWorker(const BrokerType &broker, std::chrono::milliseconds updateInterval)
        : super_t(broker, {}) {
        const auto fs = cmrc::testImages::get_filesystem();
        for (const auto &path : fs.iterate_directory("/assets/testImages")) {
            if (path.is_file()) {
                const auto file = fs.open(fmt::format("assets/testImages/{}", path.filename()));
                imageData.push_back(std::vector<std::uint8_t>(file.begin(), file.end()));
            }
        }
        assert(!imageData.empty());

        notifyThread = std::jthread([this, updateInterval] {
            while (!shutdownRequested) {
                std::this_thread::sleep_for(updateInterval);
                selectedImage = (selectedImage + 1) % imageData.size();
                SimpleContext context;
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

        super_t::setCallback([this](majordomo::RequestContext &rawCtx, const SimpleContext &, const majordomo::Empty &, SimpleContext &, BinaryData &out) {
            using namespace opencmw;
            const auto topicPath  = rawCtx.request.endpoint.path().value_or("");
            const auto path       = ::detail::stripPrefix(topicPath, "/");
            out.resourceName      = ::detail::stripPrefix(::detail::stripPrefix(path, PROPERTY_NAME), "/");
            out.image.base64      = base64pp::encode(imageData[selectedImage]);
            out.image.contentType = "image/png"; // MIME::PNG;
        });
    }

    ~ImageServiceWorker() {
        shutdownRequested = true;
        notifyThread.join();
    }
};

template<typename Mode, typename VirtualFS, majordomo::role... Roles>
class FileServerRestBackend : public majordomo::RestBackend<Mode, VirtualFS, Roles...> {
private:
    using super_t = majordomo::RestBackend<Mode, VirtualFS, Roles...>;
    std::filesystem::path _serverRoot;
    using super_t::_svr;
    using super_t::DEFAULT_REST_SCHEME;

public:
    using super_t::RestBackend;

    FileServerRestBackend(majordomo::Broker<Roles...> &broker, const VirtualFS &vfs, std::filesystem::path serverRoot, opencmw::URI<> restAddress = opencmw::URI<>::factory().scheme(DEFAULT_REST_SCHEME).hostName("0.0.0.0").port(majordomo::DEFAULT_REST_PORT).build())
        : super_t(broker, vfs, restAddress), _serverRoot(std::move(serverRoot)) {
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

template<typename T>
concept Shutdownable = requires(T s) {
    s.run();
    s.shutdown();
};

template<Shutdownable T>
struct RunInThread {
    T           &_toRun;
    std::jthread _thread;

    explicit RunInThread(T &toRun)
        : _toRun(toRun)
        , _thread([this] { _toRun.run(); }) {
    }

    ~RunInThread() {
        _toRun.shutdown();
        _thread.join();
    }
};

inline opencmw::majordomo::Settings testSettings() {
    opencmw::majordomo::Settings settings;
    settings.heartbeatInterval = std::chrono::milliseconds(100);
    settings.dnsTimeout        = std::chrono::milliseconds(250);
    return settings;
}

template<typename MessageType>
class TestNode {
public:
    opencmw::zmq::Socket _socket;

    explicit TestNode(const opencmw::zmq::Context &context, int socket_type = ZMQ_DEALER)
        : _socket(context, socket_type) {
    }

    bool bind(const opencmw::URI<opencmw::STRICT> &address) {
        return opencmw::zmq::invoke(zmq_bind, _socket, opencmw::mdp::toZeroMQEndpoint(address).data()).isValid();
    }

    bool connect(const opencmw::URI<opencmw::STRICT> &address, std::string_view subscription = "") {
        auto result = opencmw::zmq::invoke(zmq_connect, _socket, opencmw::mdp::toZeroMQEndpoint(address).data());
        if (!result) return false;

        if (!subscription.empty()) {
            return subscribe(subscription);
        }

        return true;
    }

    bool subscribe(std::string_view subscription) {
        assert(!subscription.empty());
        return opencmw::zmq::invoke(zmq_setsockopt, _socket, ZMQ_SUBSCRIBE, subscription.data(), subscription.size()).isValid();
    }

    bool unsubscribe(std::string_view subscription) {
        assert(!subscription.empty());
        return opencmw::zmq::invoke(zmq_setsockopt, _socket, ZMQ_UNSUBSCRIBE, subscription.data(), subscription.size()).isValid();
    }

    bool sendRawFrame(const std::string &data) {
        opencmw::majordomo::MessageFrame f(data, opencmw::majordomo::MessageFrame::dynamic_bytes_tag{});
        return f.send(_socket, 0).isValid(); // blocking for simplicity
    }

    std::optional<MessageType> tryReadOne(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
        std::array<zmq_pollitem_t, 1> pollerItems;
        pollerItems[0].socket = _socket.zmq_ptr;
        pollerItems[0].events = ZMQ_POLLIN;

        const auto result     = opencmw::zmq::invoke(zmq_poll, pollerItems.data(), static_cast<int>(pollerItems.size()), timeout.count());
        if (!result.isValid() || result.value() == 0) {
            return {};
        }

        return MessageType::receive(_socket);
    }

    std::optional<MessageType> tryReadOneSkipHB(int retries, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
        int  i      = 0;
        auto result = tryReadOne(timeout);
        while (!result || !result->isValid() || result->command() == opencmw::majordomo::Command::Heartbeat) {
            if (i++ >= retries) {
                return {};
            }
            result = tryReadOne(timeout);
        }
        return result;
    }

    void send(MessageType &message) {
        message.send(_socket).assertSuccess();
    }
};

inline bool waitUntilServiceAvailable(const opencmw::zmq::Context &context, std::string_view serviceName, const opencmw::URI<opencmw::STRICT> &brokerAddress = opencmw::majordomo::INTERNAL_ADDRESS_BROKER) {
    TestNode<opencmw::majordomo::MdpMessage> client(context);
    if (!client.connect(brokerAddress)) {
        return false;
    }

    constexpr auto timeout   = std::chrono::seconds(3);
    const auto     startTime = std::chrono::system_clock::now();

    while (std::chrono::system_clock::now() - startTime < timeout) {
        auto request = opencmw::majordomo::MdpMessage::createClientMessage(opencmw::majordomo::Command::Get);
        request.setServiceName("mmi.service", opencmw::majordomo::MessageFrame::static_bytes_tag{});
        request.setBody(serviceName, opencmw::majordomo::MessageFrame::dynamic_bytes_tag{});
        client.send(request);

        auto reply = client.tryReadOne();
        if (!reply) { // no reply at all? something went seriously wrong
            return false;
        }

        if (reply->body() == "200") {
            return true;
        }
    }

    return false;
}

class TestIntHandler {
    int _x = 10;

public:
    explicit TestIntHandler(int initialValue)
        : _x(initialValue) {
    }

    void operator()(opencmw::majordomo::RequestContext &context) {
        if (context.request.command == opencmw::mdp::Command::Get) {
            const auto body = std::to_string(_x);
            context.reply.data = opencmw::IoBuffer(body.data(), body.size());
            return;
        }

        assert(context.request.command == opencmw::mdp::Command::Set);

        const auto request = context.request.data.asString();
        int        value   = 0;
        const auto result  = std::from_chars(request.begin(), request.end(), value);

        if (result.ec == std::errc::invalid_argument) {
            context.reply.error = "Not a valid int";
        } else {
            _x = value;
            context.reply.data = opencmw::IoBuffer("Value set. All good!");
        }
    }
};

class NonCopyableMovableHandler {
public:
    NonCopyableMovableHandler()                                      = default;
    NonCopyableMovableHandler(NonCopyableMovableHandler &&) noexcept = default;
    NonCopyableMovableHandler &operator=(NonCopyableMovableHandler &&) noexcept = default;

    void                       operator()(opencmw::majordomo::RequestContext &) {}
};

#endif // include guard
