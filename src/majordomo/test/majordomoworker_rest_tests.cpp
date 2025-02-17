#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>

#include <RestClient.hpp>

#include <MIME.hpp>
#include <opencmw.hpp>
#include <TimingCtx.hpp>

#include <catch2/catch.hpp>
#include <fmt/format.h>
#include <refl.hpp>

#include <exception>
#include <string>
#include <thread>
#include <unordered_map>

// Concepts and tests use common types
#include <concepts/majordomo/helpers.hpp>

std::jthread makeGetRequestResponseCheckerThread(const std::string &address, const std::vector<std::string> &requiredResponses, const std::vector<int> &requiredStatusCodes = {}, [[maybe_unused]] std::source_location location = std::source_location::current()) {
    return std::jthread([=] {
        httplib::Client http("localhost", majordomo::DEFAULT_REST_PORT);
        http.set_follow_location(true);
        http.set_keep_alive(true);
#define requireWithSource(arg) \
    if (!(arg)) opencmw::zmq::debug::withLocation(location) << "<- call got a failed requirement:"; \
    REQUIRE(arg)
        for (std::size_t i = 0; i < requiredResponses.size(); ++i) {
            const auto response = http.Get(address);
            requireWithSource(response);
            const auto requiredStatusCode = i < requiredStatusCodes.size() ? requiredStatusCodes[i] : 200;
            requireWithSource(response->status == requiredStatusCode);
            requireWithSource(response->body.find(requiredResponses[i]) != std::string::npos);
        }
#undef requireWithSource
    });
}

std::jthread makeLongPollingRequestResponseCheckerThread(const std::string &address, const std::vector<std::string> &requiredResponses, const std::vector<int> &requiredStatusCodes = {}, [[maybe_unused]] std::source_location location = std::source_location::current()) {
    return std::jthread([=] {
        httplib::Client http("localhost", majordomo::DEFAULT_REST_PORT);
        http.set_follow_location(true);
        http.set_keep_alive(true);
#define requireWithSource(arg) \
    if (!(arg)) opencmw::zmq::debug::withLocation(location) << "<- call got a failed requirement:"; \
    REQUIRE(arg)
        for (std::size_t i = 0; i < requiredResponses.size(); ++i) {
            const std::string url      = fmt::format("{}{}LongPollingIdx={}", address, address.contains('?') ? "&" : "?", i == 0 ? "Next" : fmt::format("{}", i));
            const auto        response = http.Get(url);
            if (i == 0) { // check forwarding to the explicit index
                REQUIRE(response->location.find("LongPollingIdx=0") != std::string::npos);
            }
            requireWithSource(response);
            const auto requiredStatusCode = i < requiredStatusCodes.size() ? requiredStatusCodes[i] : 200;
            requireWithSource(response->status == requiredStatusCode);
            requireWithSource(response->body.find(requiredResponses[i]) != std::string::npos);
        }
#undef requireWithSource
    });
}

struct ColorContext {
    bool                    red         = false;
    bool                    green       = false;
    bool                    blue        = false;
    opencmw::MIME::MimeType contentType = opencmw::MIME::JSON;
};

ENABLE_REFLECTION_FOR(ColorContext, red, green, blue, contentType)

struct SingleString {
    std::string value;
};
ENABLE_REFLECTION_FOR(SingleString, value)

template<units::basic_fixed_string serviceName, typename... Meta>
class ColorWorker : public majordomo::Worker<serviceName, ColorContext, majordomo::Empty, SingleString, Meta...> {
    std::jthread notifyThread;

public:
    using super_t = majordomo::Worker<serviceName, ColorContext, majordomo::Empty, SingleString, Meta...>;

    template<typename BrokerType>
    explicit ColorWorker(const BrokerType &broker, std::vector<ColorContext> notificationContexts)
        : super_t(broker, {}) {
        notifyThread = std::jthread([this, contexts = std::move(notificationContexts)]() {
            int counter = 0;
            for (const auto &context : contexts) {
                std::this_thread::sleep_for(150ms);
                super_t::notify(context, { std::to_string(counter) });
                counter++;
            }
        });
    }
};

struct PathContext {
    opencmw::MIME::MimeType contentType = opencmw::MIME::JSON;
};

ENABLE_REFLECTION_FOR(PathContext, contentType)

template<units::basic_fixed_string serviceName, typename... Meta>
class PathWorker : public majordomo::Worker<serviceName, PathContext, majordomo::Empty, SingleString, Meta...> {
public:
    using super_t = majordomo::Worker<serviceName, PathContext, majordomo::Empty, SingleString, Meta...>;

    template<typename BrokerType>
    explicit PathWorker(const BrokerType &broker)
        : super_t(broker, {}) {
        super_t::setCallback([this](majordomo::RequestContext &rawCtx, const PathContext &inCtx, const majordomo::Empty &, PathContext &outCtx, SingleString &out) {
            outCtx                        = inCtx;
            const auto       endpointPath = rawCtx.request.topic.path().value_or("");
            std::string_view v(endpointPath);
            if (v.starts_with(this->name)) {
                v.remove_prefix(this->name.size());
                out.value = fmt::format("You requested path='{}'\'n", v);
            } else {
                throw std::invalid_argument(fmt::format("Invalid endpoint '{}' (must start with '{}')", endpointPath, this->name));
            }
        });
    }
};

struct WaitingContext {
    int32_t                 timeoutMs   = 0;
    opencmw::MIME::MimeType contentType = opencmw::MIME::JSON;
};
ENABLE_REFLECTION_FOR(WaitingContext, timeoutMs, contentType)

struct UpdateTime {
    long updateTime;
    std::vector<int> payload;
};
ENABLE_REFLECTION_FOR(UpdateTime, updateTime, payload)

template<units::basic_fixed_string serviceName, typename... Meta>
class WaitingWorker : public majordomo::Worker<serviceName, WaitingContext, SingleString, SingleString, Meta...> {
public:
    using super_t = majordomo::Worker<serviceName, WaitingContext, SingleString, SingleString, Meta...>;

    template<typename BrokerType>
    explicit WaitingWorker(const BrokerType &broker)
        : super_t(broker, {}) {
        super_t::setCallback([](majordomo::RequestContext &, const WaitingContext &inCtx, const SingleString &in, WaitingContext &outCtx, SingleString &out) {
            fmt::println("Sleep for {}", inCtx.timeoutMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(inCtx.timeoutMs));
            outCtx    = inCtx;
            out.value = fmt::format("You said: {}", in.value);
        });
    }
};

template<units::basic_fixed_string serviceName, int payloadSize, typename... Meta>
class ClockWorker : public majordomo::Worker<serviceName, SimpleContext, UpdateTime, UpdateTime, Meta...> {
public:
    using super_t = majordomo::Worker<serviceName, SimpleContext, UpdateTime, UpdateTime, Meta...>;
    std::jthread               _notifier;
    std::chrono::milliseconds _period;
    std::size_t               _nUpdates;
    std::atomic<bool>         _shutdownRequested;

    template<typename BrokerType>
    explicit ClockWorker(const BrokerType &broker, std::chrono::milliseconds period, std::size_t nUpdates)
            : super_t(broker, {}), _period(period), _nUpdates(nUpdates) {
        _notifier = std::jthread([this]() {
            std::chrono::system_clock::time_point updateTime = std::chrono::system_clock::now();
            while (_nUpdates > 0 && !_shutdownRequested) {
                std::this_thread::sleep_until(updateTime);
                fmt::print("publishing update\n");
                UpdateTime update{std::chrono::duration_cast<std::chrono::milliseconds>(updateTime.time_since_epoch()).count(), std::views::iota(0, payloadSize) | std::ranges::to<std::vector>() };
                this->notify(SimpleContext(), update);
                updateTime += _period;
                _nUpdates--;
            }
        });
    }

    void shutdown() {
        super_t::shutdown();
        _shutdownRequested = true;
    }

};

TEST_CASE("Simple MajordomoWorker example showing its usage", "[majordomo][majordomoworker][simple_example]") {
    // We run both broker and worker inproc
    majordomo::Broker                                          broker("/TestBroker", testSettings());
    auto                                                       fs = cmrc::assets::get_filesystem();
    FileServerRestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest(broker, fs);
    RunInThread                                                restServerRun(rest);

    // For subscription matching, it is necessary that broker knows how to handle the query params "ctx" and "contentType".
    // ("ctx" needs to use the TimingCtxFilter, and "contentType" compare the mime types (currently simply a string comparison))
    // Here we register the members of TestContext as query params, with the member names being the keys, and using the member types
    // for correct matching.
    //
    // Note that the worker uses the same settings for matching, but as it knows about TestContext, it does this registration automatically.
    opencmw::query::registerTypes(SimpleContext(), broker);

    // Create MajordomoWorker with our domain objects, and our TestHandler.
    majordomo::Worker<"/addressbook", SimpleContext, AddressRequest, AddressEntry> worker(broker, TestAddressHandler());

    // Run worker and broker in separate threads
    RunInThread brokerRun(broker);
    RunInThread workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    SECTION("request Address information as JSON and as HTML") {
        auto httpThreadJSON = makeGetRequestResponseCheckerThread("/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=application%2Fjavascript", { "Santa Claus" });

        auto httpThreadHTML = makeGetRequestResponseCheckerThread("/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=text%2Fhtml", { "<td class=\"propTable-fValue\">Elf Road</td>" });
    }

    SECTION("post data") {
        httplib::Client postData{ "http://localhost:8080" };
        postData.Post("/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=application%json", "{\"streetNumber\": 1882}", "application/json");

        auto httpThreadJSON = makeGetRequestResponseCheckerThread("/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=application%json", { "1882" });
    }

    SECTION("post data as multipart") {
        std::jthread putRequestThread{
            [] {
                // set a value on the server
                httplib::Client postRequest("http://localhost:8080");
                postRequest.set_keep_alive(true);

                httplib::MultipartFormDataItems items{
                    { "name", "Kalle", "name_file", "text" },
                    { "street", "calle", "street_file", "text" },
                    // { "streetNumber", "8", "number_file", "number" }, // `error(22) parsing number at buffer position: 41"` , deserialiser finds "8" instead of 8
                    { "postalCode", "14005", "postal_code_file", "text" },
                    { "city", "ciudad", "city_file", "text" }
                    // "isCurrent", "true", "is_current_file", "text" }, // does not work because true will be quoted. which is not a valid boolean
                    //{ "isCurrent", "false", "is_current_file", "boolean" }, // content type boolean might not exist, anyway, content_type is not taken into account anyway
                };

                auto r = postRequest.Put("/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=application%2Fjavascript", items);

                REQUIRE(r);
                CAPTURE(r->reason);
                CAPTURE(r->body);
                REQUIRE(r->status == 200);

                auto httpThreadJSON = makeGetRequestResponseCheckerThread("/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=application%2Fjavascript", { "Kalle" });
            }
        };
    }
}
TEST_CASE("Invalid paths", "[majordomo][majordomoworker][rest]") {
    majordomo::Broker                                          broker("/TestBroker", testSettings());
    auto                                                       fs = cmrc::assets::get_filesystem();
    FileServerRestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest(broker, fs);
    RunInThread                                                restServerRun(rest);

    opencmw::query::registerTypes(PathContext(), broker);

    PathWorker<"/paths"> worker(broker);

    RunInThread          brokerRun(broker);
    RunInThread          workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    auto space               = makeGetRequestResponseCheckerThread("/paths/with%20space", { "Invalid service name" }, { 500 });
    auto invalidSubscription = makeGetRequestResponseCheckerThread("/p-a-t-h-s/?LongPollIdx=Next", { "Invalid service name" }, { 500 });
}

TEST_CASE("Get/Set with subpaths", "[majordomo][majordomoworker][rest]") {
    majordomo::Broker                                          broker("/TestBroker", testSettings());
    auto                                                       fs = cmrc::assets::get_filesystem();
    FileServerRestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest(broker, fs);
    RunInThread                                                restServerRun(rest);

    opencmw::query::registerTypes(PathContext(), broker);

    PathWorker<"/paths"> worker(broker);

    RunInThread          brokerRun(broker);
    RunInThread          workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    auto empty = makeGetRequestResponseCheckerThread("/paths", { "path=''" });
    auto one   = makeGetRequestResponseCheckerThread("/paths/a", { "path='\\/a'" });
    auto two   = makeGetRequestResponseCheckerThread("/paths/a/b", { "path='\\/a\\/b'" });
}

TEST_CASE("Subscriptions", "[majordomo][majordomoworker][subscription]") {
    majordomo::Broker                                          broker("/TestBroker", testSettings());
    auto                                                       fs = cmrc::assets::get_filesystem();
    FileServerRestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest(broker, fs);
    RunInThread                                                restServerRun(rest);

    opencmw::query::registerTypes(ColorContext(), broker);

    constexpr auto         red     = ColorContext{ .red = true };
    constexpr auto         green   = ColorContext{ .green = true };
    constexpr auto         blue    = ColorContext{ .blue = true };
    constexpr auto         magenta = ColorContext{ .red = true, .blue = true };
    constexpr auto         yellow  = ColorContext{ .red = true, .green = true };
    constexpr auto         black   = ColorContext{};
    constexpr auto         white   = ColorContext{ .red = true, .green = true, .blue = true };

    ColorWorker<"/colors"> worker(broker, { red, green, blue, magenta, yellow, black, white });

    RunInThread            brokerRun(broker);
    RunInThread            workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    auto allListener    = makeLongPollingRequestResponseCheckerThread("/colors", { "0", "1", "2", "3", "4", "5", "6" });
    auto redListener    = makeLongPollingRequestResponseCheckerThread("/colors?red", { "0", "3", "4", "6" });
    auto yellowListener = makeLongPollingRequestResponseCheckerThread("/colors?red&green", { "4", "6" });
    auto whiteListener1 = makeLongPollingRequestResponseCheckerThread("/colors?red&green&blue", { "6" });
    auto whiteListener2 = makeLongPollingRequestResponseCheckerThread("/colors?green&red&blue", { "6" });
    auto whiteListener3 = makeLongPollingRequestResponseCheckerThread("/colors?blue&green&red", { "6" });

    std::this_thread::sleep_for(50ms); // give time for subscriptions to happen

    std::vector<std::string> subscriptions;
    for (const auto &subscription : worker.activeSubscriptions()) {
        subscriptions.push_back(subscription.toZmqTopic());
    }
    std::ranges::sort(subscriptions);
    REQUIRE(subscriptions == std::vector<std::string>{ "/colors", "/colors?blue&green&red", "/colors?green&red", "/colors?red" });
}

TEST_CASE("Subscription latencies", "[majordomo][majordomoworker][rest]") {
    std::atomic<int> nReceived = 0;
    std::atomic<int> msLatency = 0;
    {
        majordomo::Broker                                           broker("/TestBroker", testSettings());
        auto                                                        fs = cmrc::assets::get_filesystem();
        majordomo::RestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest(broker, fs, opencmw::URI<>("http://localhost:12346"));

        ClockWorker<"/clock", 2550> worker(broker, 10ms, 70);
        RunInThread                restServerRun(rest);
        RunInThread                brokerRun(broker);
        RunInThread                workerRun(worker);

        REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

        rest.setMajordomoTimeout(800ms); // set timeout to unit-test friendly interval

        opencmw::client::RestClient client{std::string("RestSubLatencyClient")};

        opencmw::client::Command _command;
        _command.command  = opencmw::mdp::Command::Subscribe;
        _command.topic    = opencmw::URI<>("http://localhost:12346/clock");
        _command.callback = [&nReceived, &msLatency](const opencmw::mdp::Message &reply) {
            UpdateTime replyData;
            opencmw::IoBuffer buffer = reply.data;
            opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::ALWAYS>(buffer, replyData);
            auto now = std::chrono::system_clock::now();
            auto latency = now.time_since_epoch() - std::chrono::milliseconds(replyData.updateTime);
            nReceived++;
            nReceived.notify_all();
            msLatency.fetch_add(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(latency).count()));
            fmt::print("Received {}th update with a latency of {} ms.\n", nReceived.load(), std::chrono::duration_cast<std::chrono::milliseconds>(latency).count());
        };
        client.request(_command);

        fmt::print("waiting for 40 samples to be received\n");
        int n = nReceived;
        while (n < 40) {
            nReceived.wait(n);
            n = nReceived;
        }
    }

    fmt::print("Received {} updates with an average latency of {} ms.\n", nReceived.load(), nReceived > 0 ? static_cast<double>(msLatency)/nReceived : 0.0);
    REQUIRE(nReceived > 10);
    REQUIRE(static_cast<double>(msLatency)/nReceived < 20);
}

TEST_CASE("Majordomo timeouts", "[majordomo][majordomoworker][rest]") {
    majordomo::Broker                                          broker("/TestBroker", testSettings());
    auto                                                       fs = cmrc::assets::get_filesystem();
    FileServerRestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest(broker, fs);
    RunInThread                                                restServerRun(rest);

    opencmw::query::registerTypes(WaitingContext(), broker);

    WaitingWorker<"/waiter"> worker(broker);

    RunInThread              brokerRun(broker);
    RunInThread              workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    // set timeout to unit-test friendly interval
    rest.setMajordomoTimeout(800ms);

    SECTION("Waiting for notification that doesn't happen in time returns 504 message") {
        std::vector<std::jthread> clientThreads;
        for (int i = 0; i < 16; ++i) {
            clientThreads.push_back(makeGetRequestResponseCheckerThread("/waiter?LongPollingIdx=Next", { "Timeout" }, { 504 }));
        }
    }

    SECTION("Waiting for notification that happens in time gives expected response") {
        auto client = makeGetRequestResponseCheckerThread("/waiter?LongPollingIdx=Next", { "This is a notification" });
        std::this_thread::sleep_for(400ms);
        worker.notify({}, { "This is a notification" });
    }

    SECTION("Response to request takes too long, timeout status is returned") {
        httplib::Client postData{ "http://localhost:8080" };
        auto            reply = postData.Post("/waiter?contentType=application%2Fjson&timeoutMs=1200", "{\"value\": \"Hello!\"}", "application/json");
        REQUIRE(reply);
        REQUIRE(reply->status == 504);
        REQUIRE(reply->body.find("No response") != std::string::npos);
    }

    SECTION("Response to request arrives in time") {
        httplib::Client postData{ "http://localhost:8080" };
        auto            reply = postData.Post("/waiter?contentType=application%2Fjson&timeoutMs=0", "{\"value\": \"Hello!\"}", "application/json");
        REQUIRE(reply);
        REQUIRE(reply->status == 200);
        REQUIRE(reply->body.find("You said: Hello!") != std::string::npos);
    }
}
