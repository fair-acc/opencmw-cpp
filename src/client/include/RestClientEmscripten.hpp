#ifndef OPENCMW_CPP_RESTCLIENT_EMSCRIPTEN_HPP
#define OPENCMW_CPP_RESTCLIENT_EMSCRIPTEN_HPP

#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/fetch.h>
#include <emscripten/proxying.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ClientCommon.hpp>
#include <ClientContext.hpp>
#include <MIME.hpp>
#include <URI.hpp>

using namespace opencmw;

namespace opencmw::client {

namespace detail {

struct RestWorkerState;

inline std::string_view responseBody(const emscripten_fetch_t *fetch) noexcept {
    if (fetch->data == nullptr || fetch->numBytes == 0) {
        return {};
    }
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::string_view::size_type>::max());
    return { fetch->data, static_cast<std::string_view::size_type>(std::min(fetch->numBytes, maximum)) };
}

inline std::optional<std::uint64_t> parseLongPollingIndex(std::string_view responseUrl) noexcept {
    if (responseUrl.empty()) {
        return std::nullopt;
    }
    try {
        const auto params = URI<>(std::string{ responseUrl }).queryParamMap();
        const auto entry  = params.find("LongPollingIdx");
        if (entry == params.end() || !entry->second || entry->second->empty()) {
            return std::nullopt;
        }
        const std::string &value = *entry->second;
        std::uint64_t      index{};
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), index);
        return error == std::errc{} && end == value.data() + value.size() ? std::optional{ index } : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

struct SubscriptionState {
    Command                      command{};
    std::optional<std::uint64_t> lastDeliveredIndex{};
    std::optional<std::uint64_t> activeFetchId{};
};

struct ActiveFetch {
    RestWorkerState             *owner{ nullptr };
    std::uint64_t                id{};
    std::optional<std::uint64_t> subscriptionId{}; // absent for GET/SET
    std::optional<Command>       command{};        // present for GET/SET
    std::string                  body{};           // must outlive the fetch
    emscripten_fetch_t          *fetch{ nullptr };
    bool                         closing{ false };
};

struct RestWorkerState {
    std::atomic<bool>                                               _acceptWork{ true };
    std::shared_ptr<RestWorkerState>                                _selfKeepAlive{};

    MIME::MimeType                                                  _mimeType;

    std::unordered_map<std::uint64_t, SubscriptionState>            _subscriptions{};
    std::unordered_map<std::uint64_t, std::unique_ptr<ActiveFetch>> _activeFetches{};
    std::uint64_t                                                   _nextSubscriptionId{ 1 };
    std::uint64_t                                                   _nextFetchId{ 1 };

    explicit RestWorkerState(MIME::MimeType mimeType)
        : _mimeType(mimeType) {}

    void dispatchCommand(Command &&cmd) noexcept {
        if (!_acceptWork.load(std::memory_order_acquire)) {
            return;
        }
        Command failure;
        try {
            failure.topic           = cmd.topic;
            failure.clientRequestID = cmd.clientRequestID;
            failure.callback        = cmd.callback;

            switch (cmd.command) {
            case mdp::Command::Get:
            case mdp::Command::Set: startGetOrSet(std::move(cmd)); return;
            case mdp::Command::Subscribe: startSubscription(std::move(cmd)); return;
            case mdp::Command::Unsubscribe: stopSubscription(cmd); return;
            default:
                reportFailure(failure, "command type is undefined");
                return;
            }
        } catch (const std::exception &e) {
            reportFailure(failure, e.what());
        } catch (...) {
            reportFailure(failure, "failed to start command");
        }
    }

    void startSubscription(Command &&cmd) {
        const std::uint64_t id = _nextSubscriptionId++;
        _subscriptions.emplace(id, SubscriptionState{ .command = std::move(cmd) });
        startNextLongPoll(id, std::nullopt);
    }

    void stopSubscription(const Command &cmd) {
        const auto entry = std::ranges::find_if(_subscriptions,
                [&](const auto &pair) { return pair.second.command.topic == cmd.topic; });
        if (entry == _subscriptions.end()) {
            return;
        }
        const std::optional<std::uint64_t> outstandingFetchId = entry->second.activeFetchId;
        _subscriptions.erase(entry);
        if (outstandingFetchId.has_value()) {
            closeFetch(*outstandingFetchId);
        }
    }

    void startNextLongPoll(std::uint64_t subscriptionId, std::optional<std::uint64_t> index) noexcept {
        try {
            if (!_acceptWork.load(std::memory_order_acquire)) {
                return;
            }
            const auto entry = _subscriptions.find(subscriptionId);
            if (entry == _subscriptions.end()) {
                return;
            }
            const std::string longPollingIndex = index.has_value() ? std::to_string(*index) : "Next";

            auto              activeFetch      = std::make_unique<ActiveFetch>();
            activeFetch->owner                 = this;
            activeFetch->id                    = _nextFetchId++;
            activeFetch->subscriptionId        = subscriptionId;
            entry->second.activeFetchId        = activeFetch->id;
            startFetch(std::move(activeFetch), URI<STRICT>::UriFactory(entry->second.command.topic).addQueryParameter("LongPollingIdx", longPollingIndex).build());
        } catch (const std::exception &e) {
            endSubscription(subscriptionId, nullptr, 500, {}, e.what());
        } catch (...) {
            endSubscription(subscriptionId, nullptr, 500, {}, "failed to start long-poll request");
        }
    }

    void startGetOrSet(Command &&cmd) {
        const URI<STRICT> uri         = cmd.topic;

        auto              activeFetch = std::make_unique<ActiveFetch>();
        activeFetch->owner            = this;
        activeFetch->id               = _nextFetchId++;
        if (cmd.command == mdp::Command::Set) {
            activeFetch->body = cmd.data.asString();
        }
        activeFetch->command = std::move(cmd);

        startFetch(std::move(activeFetch), uri);
    }

    void startFetch(std::unique_ptr<ActiveFetch> activeFetch, const URI<STRICT> &uri) {
        std::string contentType{ _mimeType.typeName() };
        const auto &query = uri.queryParamMap();
        if (const auto entry = query.find("contentType"); entry != query.end() && entry->second) {
            contentType = *entry->second;
        }
        const std::string_view      method = activeFetch->command.has_value() && activeFetch->command->command == mdp::Command::Set ? "POST" : "GET";
        std::array<const char *, 5> headers{ "accept", contentType.c_str(), nullptr, nullptr, nullptr };
        if (method == "POST") {
            headers[2] = "content-type";
            headers[3] = contentType.c_str();
        }

        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        method.copy(attr.requestMethod, method.size());
        attr.requestMethod[method.size()] = '\0';
        attr.attributes                   = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_REPLACE;
        attr.requestHeaders               = headers.data();
        attr.onsuccess                    = &RestWorkerState::onFetchSuccess;
        attr.onerror                      = &RestWorkerState::onFetchError;
        attr.userData                     = activeFetch.get();
        if (!activeFetch->body.empty()) {
            attr.requestData     = activeFetch->body.data();
            attr.requestDataSize = activeFetch->body.size();
        }

        const std::uint64_t fetchId = activeFetch->id;
        _activeFetches.emplace(fetchId, std::move(activeFetch));

        emscripten_fetch_t *fetch = emscripten_fetch(&attr, uri.str().c_str());

        if (fetch == nullptr) {
            const auto entry = _activeFetches.find(fetchId);
            if (entry == _activeFetches.end()) {
                return;
            }
            const std::optional<std::uint64_t> subscriptionId = entry->second->subscriptionId;
            const std::optional<Command>       command        = std::move(entry->second->command);
            _activeFetches.erase(entry);

            if (subscriptionId.has_value()) {
                endSubscription(*subscriptionId, nullptr, 500, {}, "emscripten_fetch() returned null");
            } else if (command.has_value()) {
                reportFailure(*command, "emscripten_fetch() returned null");
            }
            return;
        }

        if (const auto entry = _activeFetches.find(fetchId); entry != _activeFetches.end() && !entry->second->closing) {
            entry->second->fetch = fetch;
        }
    }

    static void onFetchSuccess(emscripten_fetch_t *fetch) noexcept { completeFetch(fetch, true); }
    static void onFetchError(emscripten_fetch_t *fetch) noexcept { completeFetch(fetch, false); }

    static void completeFetch(emscripten_fetch_t *fetch, bool succeeded) noexcept {
        auto *activeFetch = static_cast<ActiveFetch *>(fetch->userData);
        if (activeFetch == nullptr || activeFetch->owner == nullptr || activeFetch->closing) {
            return;
        }
        RestWorkerState    *owner          = activeFetch->owner;
        const std::uint64_t fetchId        = activeFetch->id;
        const auto          subscriptionId = activeFetch->subscriptionId;
        try {
            // This view must be consumed before the handler closes the fetch.
            std::optional<std::string_view> fetchError;
            if (!succeeded) {
                fetchError = fetch->statusText[0] != '\0' ? std::string_view{ fetch->statusText } : std::string_view{ "fetch failed" };
            }
            if (subscriptionId.has_value()) {
                owner->handleSubscriptionCompletion(fetchId, *subscriptionId, fetch, std::move(fetchError));
            } else {
                owner->handleGetOrSetCompletion(fetchId, fetch, std::move(fetchError));
            }
        } catch (const std::exception &e) {
            owner->discardFetch(fetchId, subscriptionId, fetch, e.what());
            std::println(std::cerr, "RestClientEmscripten: fetch callback failed: {}", e.what());
        } catch (...) {
            owner->discardFetch(fetchId, subscriptionId, fetch, "fetch callback failed");
            std::println(std::cerr, "RestClientEmscripten: fetch callback failed");
        }
    }

    void handleSubscriptionCompletion(std::uint64_t fetchId, std::uint64_t subscriptionId, emscripten_fetch_t *fetch, std::optional<std::string_view> fetchError) {
        if (!_acceptWork.load(std::memory_order_acquire)) {
            closeFetch(fetchId, fetch);
            return;
        }
        const auto entry = _subscriptions.find(subscriptionId);
        if (entry == _subscriptions.end()) {
            closeFetch(fetchId, fetch);
            return;
        }
        SubscriptionState     &state  = entry->second;

        const unsigned short   status = fetch->status;
        const std::string_view body   = responseBody(fetch);
        const auto             index  = parseLongPollingIndex(fetch->responseUrl != nullptr ? std::string_view{ fetch->responseUrl } : std::string_view{});

        // Server timeout on long-poll, resend the same request.
        if (status == 504) {
            if (!index.has_value()) {
                endSubscription(subscriptionId, fetch, status, body, "missing or unparsable LongPollingIdx in the response URL");
                return;
            }
            closeFetch(fetchId, fetch);
            startNextLongPoll(subscriptionId, *index);
            return;
        }

        if (fetchError.has_value()) {
            endSubscription(subscriptionId, fetch, status, body, *fetchError);
            return;
        }

        if (!index.has_value()) {
            endSubscription(subscriptionId, fetch, status, body, "missing or unparsable LongPollingIdx in the response URL");
            return;
        }

        if (state.lastDeliveredIndex.has_value() && *index <= *state.lastDeliveredIndex) {
            const std::uint64_t expected = *state.lastDeliveredIndex + 1;
            closeFetch(fetchId, fetch);
            startNextLongPoll(subscriptionId, expected);
            return;
        }

        std::string skippedWarning;
        if (state.lastDeliveredIndex.has_value() && *index - *state.lastDeliveredIndex > 1) {
            skippedWarning = std::format("Warning: skipped {} samples", *index - *state.lastDeliveredIndex - 1);
        }

        const mdp::Message message = buildMessage(state.command, status, body, skippedWarning);
        state.lastDeliveredIndex   = *index;

        closeFetch(fetchId, fetch);
        invokeGuarded(state.command.callback, message);
        startNextLongPoll(subscriptionId, *index + 1);
    }

    void handleGetOrSetCompletion(std::uint64_t fetchId, emscripten_fetch_t *fetch, std::optional<std::string_view> fetchError) {
        if (!_acceptWork.load(std::memory_order_acquire)) {
            closeFetch(fetchId, fetch);
            return;
        }
        const auto entry = _activeFetches.find(fetchId);
        if (entry == _activeFetches.end() || !entry->second->command.has_value()) {
            closeFetch(fetchId, fetch);
            return;
        }
        const unsigned short        status  = fetch->status;
        const Command               command = std::move(*entry->second->command);

        std::optional<mdp::Message> message;
        try {
            message = buildMessage(command, status, responseBody(fetch), fetchError.has_value() ? std::string_view{ *fetchError } : std::string_view{});
        } catch (const std::exception &e) {
            std::println(std::cerr, "RestClientEmscripten: could not build the GET/SET response: {}", e.what());
        }

        closeFetch(fetchId, fetch);
        if (message.has_value()) {
            invokeGuarded(command.callback, *message);
        }
    }

    void discardFetch(std::uint64_t fetchId, std::optional<std::uint64_t> subscriptionId, emscripten_fetch_t *callbackFetch, std::string_view error) noexcept {
        if (subscriptionId.has_value() && _subscriptions.contains(*subscriptionId)) {
            endSubscription(*subscriptionId, callbackFetch, 500, {}, error);
        } else {
            closeFetch(fetchId, callbackFetch);
        }
    }

    void endSubscription(std::uint64_t subscriptionId, emscripten_fetch_t *callbackFetch, unsigned short status, std::string_view body, std::string_view error) noexcept {
        const auto entry = _subscriptions.find(subscriptionId);
        if (entry == _subscriptions.end()) {
            return;
        }
        const Command                      command            = std::move(entry->second.command);
        const std::optional<std::uint64_t> outstandingFetchId = entry->second.activeFetchId;
        _subscriptions.erase(entry);

        std::optional<mdp::Message> message;
        try {
            message = buildMessage(command, status, body, error);
        } catch (const std::exception &e) {
            std::println(std::cerr, "RestClientEmscripten: could not report '{}': {}", error, e.what());
        } catch (...) {
            std::println(std::cerr, "RestClientEmscripten: could not report '{}'", error);
        }

        if (outstandingFetchId.has_value()) {
            closeFetch(*outstandingFetchId, callbackFetch);
        }
        if (message.has_value()) {
            invokeGuarded(command.callback, *message);
        }
    }

    void closeFetch(std::uint64_t fetchId, emscripten_fetch_t *callbackFetch = nullptr) noexcept {
        const auto entry = _activeFetches.find(fetchId);
        if (entry == _activeFetches.end() || entry->second->closing) {
            return;
        }
        entry->second->closing = true;
        if (emscripten_fetch_t *fetch = callbackFetch != nullptr ? callbackFetch : entry->second->fetch; fetch != nullptr) {
            (void) emscripten_fetch_close(fetch);
        }
        _activeFetches.erase(fetchId);
    }

    void cleanup() noexcept {
        while (!_activeFetches.empty()) {
            closeFetch(_activeFetches.begin()->first);
        }
        _subscriptions.clear();
        _selfKeepAlive.reset();
        emscripten_runtime_keepalive_pop();
    }

    void        reportFailure(const Command &command, std::string_view error) noexcept {
        if (!command.callback) {
            std::println(std::cerr, "RestClientEmscripten: {}", error);
            return;
        }
        try {
            invokeGuarded(command.callback, buildMessage(command, 500, {}, error));
        } catch (const std::exception &e) {
            std::println(std::cerr, "RestClientEmscripten: could not report '{}': {}", error, e.what());
        } catch (...) {
            std::println(std::cerr, "RestClientEmscripten: could not report '{}'", error);
        }
    }

    void invokeGuarded(const std::function<void(const mdp::Message &)> &callback, const mdp::Message &message) noexcept {
        if (!callback || !_acceptWork.load(std::memory_order_acquire)) {
            return;
        }
        try {
            callback(message);
        } catch (const std::exception &e) {
            std::println(std::cerr, "RestClientEmscripten: callback threw '{}'", e.what());
        } catch (...) {
            std::println(std::cerr, "RestClientEmscripten: callback threw");
        }
    }

    static mdp::Message buildMessage(const Command &command, unsigned short status, std::string_view body, std::string_view error) {
        const bool ok = status >= 200 && status < 400;
        return mdp::Message{
            .id              = 0,
            .arrivalTime     = std::chrono::system_clock::now(),
            .protocolName    = command.topic.scheme().value_or(""),
            .command         = mdp::Command::Final,
            .clientRequestID = command.clientRequestID,
            .topic           = command.topic,
            .data            = ok ? IoBuffer(body.data(), body.size()) : IoBuffer(),
            .error           = ok ? std::string(error) : std::format("{} - {}{}{}", status, error, body.empty() ? "" : ":", body),
            .rbac            = IoBuffer()
        };
    }
};

class FetchWorker {
    std::shared_ptr<emscripten::ProxyingQueue> _queue{ std::make_shared<emscripten::ProxyingQueue>() };
    std::shared_ptr<RestWorkerState>           _state;
    std::mutex                                 _enqueueMutex{}; // keeps cleanup behind accepted commands
    pthread_t                                  _worker{};

public:
    explicit FetchWorker(MIME::MimeType mimeType)
        : _state(std::make_shared<RestWorkerState>(mimeType)) {
        if (_queue->queue == nullptr) {
            throw std::runtime_error("RestClient: proxying queue allocation failed");
        }

        // Keep the detached pthread runtime alive to process proxied work.
        std::thread worker{ [] { emscripten_runtime_keepalive_push(); } };
        _worker = worker.native_handle();
        worker.detach();
        _state->_selfKeepAlive = _state;
    }

    ~FetchWorker() { stop(); }

    FetchWorker(const FetchWorker &)            = delete;
    FetchWorker &operator=(const FetchWorker &) = delete;
    FetchWorker(FetchWorker &&)                 = delete;
    FetchWorker &operator=(FetchWorker &&)      = delete;

    void         submit(Command &&cmd) {
        std::shared_ptr<Command> pendingCommand;
        try {
            {
                std::lock_guard lock(_enqueueMutex);
                if (!_state->_acceptWork.load(std::memory_order_acquire)) {
                    return;
                }

                pendingCommand = std::make_shared<Command>(std::move(cmd));
                if (_queue->proxyAsync(_worker, [state = _state, command = pendingCommand]() mutable { state->dispatchCommand(std::move(*command)); })) {
                    return;
                }
            }
            _state->reportFailure(*pendingCommand, "request was not queued on the REST worker");
        } catch (const std::exception &e) {
            _state->reportFailure(pendingCommand ? *pendingCommand : cmd, e.what());
        } catch (...) {
            _state->reportFailure(pendingCommand ? *pendingCommand : cmd, "could not queue request on the REST worker");
        }
    }

    void stop() noexcept {
        if (!_state->_acceptWork.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        try {
            std::lock_guard lock(_enqueueMutex);
            if (_queue->proxyAsync(_worker, [state = _state, queue = _queue] { state->cleanup(); })) {
                return;
            }
        } catch (...) { // locking failed, or proxyAsync could not allocate the task
        }
        std::fputs("RestClientEmscripten: could not queue REST worker cleanup; leaving the worker alive\n", stderr);
    }
};
} // namespace detail

class RestClient : public ClientBase {
    std::string         _name;
    MIME::MimeType      _mimeType;
    std::string         _caCertificate;
    detail::FetchWorker _worker;

public:
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
        requires(!(std::same_as<std::remove_cvref_t<Args>, RestClient> || ...))
    explicit(false) RestClient(Args... initArgs)
        : _name(detail::find_argument_value<false, std::string>([] { return "RestClient"; }, initArgs...))
        , _mimeType(detail::find_argument_value<true, DefaultContentTypeHeader>([] { return MIME::BINARY; }, initArgs...))
        , _worker(_mimeType) {}

    ~RestClient() override                                = default;

    RestClient(const RestClient &)                        = delete;
    RestClient &operator=(const RestClient &)             = delete;
    RestClient(RestClient &&)                             = delete;
    RestClient                  &operator=(RestClient &&) = delete;

    void                         stop() override { _worker.stop(); }

    std::vector<std::string>     protocols() noexcept override { return { "http", "https" }; }

    [[nodiscard]] std::string    name() const noexcept { return _name; }
    [[nodiscard]] MIME::MimeType defaultMimeType() const noexcept { return _mimeType; }
    [[nodiscard]] std::string    clientCertificate() const noexcept { return _caCertificate; }

    void                         request(Command cmd) override { _worker.submit(std::move(cmd)); }
};

} // namespace opencmw::client

#endif // OPENCMW_CPP_RESTCLIENT_EMSCRIPTEN_HPP
