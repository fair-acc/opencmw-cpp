#ifndef OPENCMW_CPP_RESTCLIENT_EMSCRIPTEN_HPP
#define OPENCMW_CPP_RESTCLIENT_EMSCRIPTEN_HPP

#include <emscripten.h>
#include <emscripten/fetch.h>

#include <memory>
#include <string>
#include <unordered_set>

#include <ClientCommon.hpp>
#include <ClientContext.hpp>
#include <MIME.hpp>
#include <URI.hpp>

using namespace opencmw;

namespace opencmw::client {

namespace detail {

/***
 * Get the final URL of a possibly redirected HTTP fetch call.
 * Uses Javascript to return the the url as a string.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdollar-in-identifier-extension"
static std::string getFinalURL(std::uint32_t id) {
    auto finalURLChar = static_cast<char *>(EM_ASM_PTR({
                                                                   var fetch = Fetch.xhrs.get($0);
                                                                   if (fetch) {
                                                                       var finalURL = fetch.responseURL;
                                                                       var lengthBytes = lengthBytesUTF8(finalURL) + 1;
                                                                       var stringOnWasmHeap = _malloc(lengthBytes);
                                                                       stringToUTF8(finalURL, stringOnWasmHeap, lengthBytes);
                                                                       return stringOnWasmHeap;
                                                                   }
                                                                   return 0; }, id));
    if (finalURLChar == nullptr) {
        return {};
    }
    std::string finalURL{ finalURLChar, strlen(finalURLChar) };
    EM_ASM({ _free($0) }, finalURLChar);
    return finalURL;
}
#pragma GCC diagnostic pop

struct pointer_equals {
    using is_transparent = void;

    template<typename Left, typename Right>
    bool operator()(const Left &left, const Right &right) const {
        return std::to_address(left) == std::to_address(right);
    }
};

struct pointer_hash {
    using is_transparent = void;

    template<typename Pointer>
    std::size_t operator()(const Pointer &ptr) const {
        const auto *raw = std::to_address(ptr);
        return std::hash<decltype(raw)>{}(raw);
    }
};

auto checkedStringViewSize = [](auto numBytes) {
    if (numBytes > std::numeric_limits<std::string_view::size_type>::max()) {
        throw std::out_of_range(std::format("We received more data than we can handle {}", numBytes));
    }
    return static_cast<std::string_view::size_type>(numBytes);
};

std::array<std::string, 4> getPreferredContentTypeHeader(const URI<STRICT> &uri, auto _mimeType) {
    auto mimeType = std::string(_mimeType.typeName());
    if (const auto acceptHeader = uri.queryParamMap().find("contentType"); acceptHeader != uri.queryParamMap().end() && acceptHeader->second) {
        mimeType = acceptHeader->second->c_str();
    }
    return { "accept", mimeType, "content-type", mimeType };
}

struct FetchPayload {
    Command command;

    explicit FetchPayload(Command &&_command)
        : command(std::move(_command)) {}

    FetchPayload(const FetchPayload &other)                = delete;

    FetchPayload(FetchPayload &&other) noexcept            = default;

    FetchPayload &operator=(const FetchPayload &other)     = delete;

    FetchPayload &operator=(FetchPayload &&other) noexcept = default;

    void          returnMdpMessage(unsigned short status, std::string_view body, std::string_view errorMsgExt = "") noexcept {
        if (!command.callback) {
            return;
        }
        const bool msgOK = status >= 200 && status < 400;
        try {
            command.callback(mdp::Message{
                             .id              = 0,
                             .arrivalTime     = std::chrono::system_clock::now(),
                             .protocolName    = command.topic.scheme().value(),
                             .command         = mdp::Command::Final,
                             .clientRequestID = command.clientRequestID,
                             .topic           = command.topic,
                             .data            = msgOK ? IoBuffer(body.data(), body.size()) : IoBuffer(),
                             .error           = msgOK ? std::string(errorMsgExt) : std::format("{} - {}{}{}", status, errorMsgExt, body.empty() ? "" : ":", body),
                             .rbac            = IoBuffer() });
        } catch (const std::exception &e) {
            std::cerr
                    << std::format("caught exception '{}' in FetchPayload::returnMdpMessage(cmd={}, {}: {})", e.what(), command.topic, status,
                                        body)
                    << std::endl;
        } catch (...) {
            std::cerr
                    << std::format("caught unknown exception in FetchPayload::returnMdpMessage(cmd={}, {}: {})", command.topic, status, body)
                    << std::endl;
        }
    }

    void onsuccess(unsigned short status, std::string_view data) {
        returnMdpMessage(status, data);
    }

    void onerror(unsigned short status, std::string_view error, std::string_view data) {
        returnMdpMessage(status, data, error);
    }
};

static std::unordered_set<std::unique_ptr<detail::FetchPayload>, detail::pointer_hash, detail::pointer_equals> fetchPayloads;

struct SubscriptionPayload;
static std::unordered_set<std::unique_ptr<detail::SubscriptionPayload>, detail::pointer_hash, detail::pointer_equals> subscriptionPayloads;

struct SubscriptionPayload : FetchPayload {
    bool                         _live = true;
    MIME::MimeType               _mimeType;
    std::size_t                  _update                      = 0;

    static constexpr std::size_t kParallelLongPollingRequests = 1; // increasing this value could reduce latency but needs some more robust error handling for unexpected updates
    std::vector<std::uint64_t>   _requestedIndexes;

    SubscriptionPayload(Command &&_command, MIME::MimeType mimeType)
        : FetchPayload(std::move(_command)), _mimeType(std::move(mimeType)) {}

    SubscriptionPayload(const SubscriptionPayload &other)                = delete;

    SubscriptionPayload(SubscriptionPayload &&other) noexcept            = default;

    SubscriptionPayload &operator=(const SubscriptionPayload &other)     = delete;

    SubscriptionPayload &operator=(SubscriptionPayload &&other) noexcept = default;

    void                 sendFollowUpRequestsFor(std::uint64_t longPollingIdx) {
        auto it = std::ranges::find(_requestedIndexes, longPollingIdx);
        if (it != _requestedIndexes.end()) {
            _requestedIndexes.erase(it);
        }
        for (std::uint64_t i = longPollingIdx + 1; i <= longPollingIdx + kParallelLongPollingRequests; ++i) {
            if (std::ranges::find(_requestedIndexes, i) == _requestedIndexes.end()) {
                _requestedIndexes.push_back(i);
                request(std::to_string(i));
            }
        }
    }
    void request(std::string longPollingIndex) {
        auto                                                 uri             = opencmw::URI<opencmw::STRICT>::UriFactory(command.topic).addQueryParameter("LongPollingIdx", longPollingIndex).build();
        auto                                                 preferredHeader = detail::getPreferredContentTypeHeader(command.topic, _mimeType);

        std::array<const char *, preferredHeader.size() + 1> preferredHeaderEmscripten;
        std::transform(preferredHeader.cbegin(), preferredHeader.cend(), preferredHeaderEmscripten.begin(),
                [](const auto &str) { return str.c_str(); });
        preferredHeaderEmscripten[preferredHeaderEmscripten.size() - 1] = nullptr;

        emscripten_fetch_attr_t attr{};

        emscripten_fetch_attr_init(&attr);

        strcpy(attr.requestMethod, "GET");

        attr.userData            = this;
        static auto getPayloadIt = [](emscripten_fetch_t *fetch) {
            auto *rawPayload = fetch->userData;
            auto  it         = detail::subscriptionPayloads.find(rawPayload);
            if (it == detail::subscriptionPayloads.end()) {
                std::print("RestClientEmscripten::payloadError: url: {}, bytes: {}\n", fetch->url, fetch->numBytes);
                throw std::format("Unknown payload for a resulting subscription");
            }
            return it;
        };

        attr.attributes     = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attr.requestHeaders = preferredHeaderEmscripten.data();
        attr.onsuccess      = [](emscripten_fetch_t *fetch) {
            auto          payloadIt      = getPayloadIt(fetch);
            auto         &payload        = *payloadIt;
            std::uint64_t longPollingIdx = 0;
            if (payload->_live) {
                std::string finalURL             = getFinalURL(fetch->id);
                std::string longPollingIdxString = opencmw::URI<>(finalURL).queryParamMap().at("LongPollingIdx").value_or("0");

                char       *end                  = nullptr;
                longPollingIdx                   = strtoull(longPollingIdxString.data(), &end, 10);
                if (end != longPollingIdxString.data() + longPollingIdxString.size()) {
                    std::println(std::cerr, "RestClientEmscripten::payloadError: url: {}, bytes: {}\n", fetch->url, fetch->numBytes);
                    return;
                }

                const long indexDiff = static_cast<long>(longPollingIdx) - static_cast<long>(payload->_update + 1);
                if (payload->_update != 0 && indexDiff != 0) {
                    std::print("received unexpected update: {}, expected {}\n", longPollingIdx, payload->_update + 1);
                }
                payload->onsuccess(fetch->status, std::string_view(fetch->data, detail::checkedStringViewSize(fetch->numBytes)), indexDiff);
                emscripten_fetch_close(fetch);

                payload->_update = longPollingIdx;
                payload->sendFollowUpRequestsFor(longPollingIdx);
            } else {
                detail::subscriptionPayloads.erase(payloadIt);
            }
        };
        attr.onerror = [](emscripten_fetch_t *fetch) {
            auto  payloadIt = getPayloadIt(fetch);
            auto &payload   = *payloadIt;
            payload->onerror(fetch->status, std::string_view(fetch->data, detail::checkedStringViewSize(fetch->numBytes)), fetch->statusText);
            emscripten_fetch_close(fetch);
        };
        emscripten_fetch(&attr, uri.str().data());
    }

    void onsuccess(unsigned short status, std::string_view data, long idxDifference = 0) {
        std::string skippedWarning;
        if (idxDifference != 0) {
            skippedWarning = std::format("Warning: skipped {} samples", idxDifference);
        }
        returnMdpMessage(status, data, skippedWarning);
    }

    void onerror(unsigned short status, std::string_view error, std::string_view data) {
        returnMdpMessage(status, data, error);
    }
};
} // namespace detail

class RestClient : public ClientBase {
    std::string       _name;
    MIME::MimeType    _mimeType = MIME::BINARY;
    std::atomic<bool> _run      = true;
    std::string       _caCertificate;

public:
    static bool CHECK_CERTIFICATES;

    /**
     * Initialises a basic RestClient
     *
     * usage example:
     * RestClient client("clientName", DefaultContentTypeHeader(MIME::HTML), ClientCertificates(testCertificate))
     *
     * @tparam Args see argument example above. Order is arbitrary.
     * @param initArgs
     */
    template<typename... Args>
    explicit(false) RestClient(Args... initArgs)
        : _name(detail::find_argument_value<false, std::string>([] { return "RestClient"; }, initArgs...))
        , _mimeType(detail::find_argument_value<true, DefaultContentTypeHeader>([] { return MIME::BINARY; }, initArgs...)) {
    }
    ~RestClient() { RestClient::stop(); }

    void                      stop() override {}

    std::vector<std::string>  protocols() noexcept override { return { "http", "https" }; }

    [[nodiscard]] std::string name() const noexcept { return _name; }
    // [[nodiscard]] ThreadPoolType threadPool() const noexcept { return _thread_pool; }
    [[nodiscard]] MIME::MimeType defaultMimeType() const noexcept { return _mimeType; }
    [[nodiscard]] std::string    clientCertificate() const noexcept { return _caCertificate; }

    void                         request(Command cmd) override {
        switch (cmd.command) {
        case mdp::Command::Get:
        case mdp::Command::Set:
            executeCommand(std::move(cmd));
            return;
        case mdp::Command::Subscribe:
            startSubscription(std::move(cmd));
            return;
        case mdp::Command::Unsubscribe: // deregister existing subscription URI is key
            stopSubscription(std::move(cmd));
            return;
        default:
            throw std::invalid_argument("command type is undefined");
        }
    }

private:
    void executeCommand(Command &&cmd) const {
        auto                                                 preferredHeader = detail::getPreferredContentTypeHeader(cmd.topic, _mimeType);
        std::array<const char *, preferredHeader.size() + 1> preferredHeaderEmscripten;
        std::transform(preferredHeader.cbegin(), preferredHeader.cend(), preferredHeaderEmscripten.begin(),
                [](const auto &str) { return str.c_str(); });
        preferredHeaderEmscripten[preferredHeaderEmscripten.size() - 1] = nullptr;

        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);

        auto payload  = std::make_unique<detail::FetchPayload>(std::move(cmd));
        attr.userData = payload.get();

        if (payload->command.command == opencmw::mdp::Command::Set) {
            strcpy(attr.requestMethod, "POST");
            auto body            = payload->command.data.asString();
            attr.requestData     = body.data();
            attr.requestDataSize = body.size();
        } else {
            strcpy(attr.requestMethod, "GET");
        }

        static auto getPayload = [](emscripten_fetch_t *fetch) {
            auto *rawPayload = fetch->userData;
            auto  it         = detail::fetchPayloads.find(rawPayload);
            if (it == detail::fetchPayloads.end()) {
                throw std::format("Unknown payload for a resulting fetch call");
            }
            auto extracted_node = detail::fetchPayloads.extract(it);
            return std::move(extracted_node.value());
        };

        attr.attributes     = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attr.requestHeaders = preferredHeaderEmscripten.data();
        attr.onsuccess      = [](emscripten_fetch_t *fetch) {
            getPayload(fetch)->onsuccess(fetch->status, std::string_view(fetch->data, detail::checkedStringViewSize(fetch->numBytes)));
            emscripten_fetch_close(fetch);
        };
        attr.onerror = [](emscripten_fetch_t *fetch) {
            getPayload(fetch)->onerror(fetch->status, std::string_view(fetch->data, detail::checkedStringViewSize(fetch->numBytes)), fetch->statusText);
            emscripten_fetch_close(fetch);
        };

        // TODO: Pass the payload as POST body: emscripten_fetch(&attr, uri.relativeRef()->data());

        emscripten_fetch(&attr, payload->command.topic.str().data());
        detail::fetchPayloads.insert(std::move(payload));
    }

    void startSubscription(Command &&cmd) {
        auto payload    = std::make_unique<detail::SubscriptionPayload>(std::move(cmd), _mimeType);
        auto rawPayload = payload.get();
        detail::subscriptionPayloads.insert(std::move(payload));
        std::print("starting subscription: {}, existing subscriptions: {}, from main thread: \n", cmd.topic.str(), detail::subscriptionPayloads.size(), emscripten_is_main_runtime_thread());
        if (emscripten_is_main_runtime_thread()) {
            try {
                rawPayload->request("Next");
            } catch (std::runtime_error &e) {
                rawPayload->onerror(500, e.what(), "");
            } catch (...) {
                rawPayload->onerror(500, "failed to set up subscription", "");
            }
        } else {
            emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_IP, +[](void *data) {
                auto subPayload = reinterpret_cast<opencmw::client::detail::SubscriptionPayload *>(data);
                try {
                    subPayload->request("Next");
                } catch (std::runtime_error &e) {
                    subPayload->onerror(500, e.what(), "");
                } catch (...) {
                    subPayload->onerror(500, "failed to set up subscription", "");
                }
                return 0; }, rawPayload);
        }
    }

    void stopSubscription(Command &&cmd) {
        auto payloadIt = std::ranges::find_if(detail::subscriptionPayloads,
                [&](const auto &ptr) {
                    return ptr->command.topic == cmd.topic;
                });
        if (payloadIt == detail::subscriptionPayloads.end()) {
            return;
        }
        std::print("stopping subscription: {}, existing subscriptions: {}\n", cmd.topic.str(), detail::subscriptionPayloads.size());

        auto &payload  = *payloadIt;
        payload->_live = false;
    }
};

} // namespace opencmw::client

#endif // OPENCMW_CPP_RESTCLIENT_EMSCRIPTEN_HPP
