#include "majordomo/Rest.hpp"
#include <majordomo/Broker.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>

#include <RestClient.hpp>

#include <MIME.hpp>
#include <opencmw.hpp>
#include <TimingCtx.hpp>

#include <catch2/catch.hpp>
#include <format>
#include <print>
#include <refl.hpp>

#include <string>
#include <thread>

// Concepts and tests use common types
#include <concepts/majordomo/helpers.hpp>

constexpr std::uint16_t kServerPort = 12348;

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
        super_t::setCallback([](majordomo::RequestContext & /*rawCtx*/, const ColorContext &inCtx, const majordomo::Empty &, ColorContext &outCtx, SingleString &out) {
            outCtx    = inCtx;
            out.value = std::format("red={}, green={}, blue={}\n", inCtx.red, inCtx.green, inCtx.blue);
            FAIL(std::format("Unexpected GET/SET request: {}", out.value));
        });
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
                out.value = std::format("You requested path='{}'\'n", v);
            } else {
                throw std::invalid_argument(std::format("Invalid endpoint '{}' (must start with '{}')", endpointPath, this->name));
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
    long             updateTimeµs;
    std::vector<int> payload;
};
ENABLE_REFLECTION_FOR(UpdateTime, updateTimeµs, payload)

template<units::basic_fixed_string serviceName, typename... Meta>
class WaitingWorker : public majordomo::Worker<serviceName, WaitingContext, SingleString, SingleString, Meta...> {
public:
    using super_t = majordomo::Worker<serviceName, WaitingContext, SingleString, SingleString, Meta...>;

    template<typename BrokerType>
    explicit WaitingWorker(const BrokerType &broker)
        : super_t(broker, {}) {
        super_t::setCallback([](majordomo::RequestContext &, const WaitingContext &inCtx, const SingleString &in, WaitingContext &outCtx, SingleString &out) {
            std::println("Sleep for {}", inCtx.timeoutMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(inCtx.timeoutMs));
            outCtx    = inCtx;
            out.value = std::format("You said: {}", in.value);
        });
    }
};

template<units::basic_fixed_string serviceName, int payloadSize, typename... Meta>
class ClockWorker : public majordomo::Worker<serviceName, SimpleContext, UpdateTime, UpdateTime, Meta...> {
public:
    using super_t = majordomo::Worker<serviceName, SimpleContext, UpdateTime, UpdateTime, Meta...>;
    std::jthread              _notifier;
    std::chrono::milliseconds _period;
    std::size_t               _nUpdates;
    std::atomic<bool>         _shutdownRequested;

    template<typename BrokerType>
    explicit ClockWorker(const BrokerType &broker, std::chrono::milliseconds period, std::size_t nUpdates)
        : super_t(broker, {}), _period(period), _nUpdates(nUpdates) {
        _notifier = std::jthread([this]() {
            auto updateTime = std::chrono::high_resolution_clock::now();
            while (_nUpdates > 0 && !_shutdownRequested) {
                std::this_thread::sleep_until(updateTime);
                std::print("publishing update\n");
                UpdateTime update{ std::chrono::duration_cast<std::chrono::microseconds>(updateTime.time_since_epoch()).count(), std::views::iota(0, payloadSize) | std::ranges::to<std::vector>() };
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

namespace {
template<typename T>
void waitFor(std::atomic<T> &responseCount, T expected, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto start = std::chrono::system_clock::now();
    while (responseCount.load() < expected && std::chrono::system_clock::now() - start < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const auto result = responseCount.load() == expected;
    if (!result) {
        FAIL(std::format("Expected {} responses, but got {}\n", expected, responseCount.load()));
    }
}
} // namespace

TEST_CASE("Simple REST example", "[majordomo][majordomoworker][simple_example][http2]") {
    // We run both broker and worker inproc
    majordomo::Broker             broker("/TestBroker", testSettings());
    majordomo::rest::Settings     rest;
    rest.port     = kServerPort;
    rest.protocols = majordomo::rest::Protocol::Http2;
    rest.handlers = { majordomo::rest::cmrcHandler("/assets/*", "", std::make_shared<cmrc::embedded_filesystem>(cmrc::assets::get_filesystem()), "") };

    if (auto bound = broker.bindRest(rest); !bound) {
        FAIL(std::format("Failed to bind REST server: {}", bound.error()));
        return;
    }

    // For subscription matching, it is necessary that broker knows how to handle the query params "ctx" and "contentType".
    // ("ctx" needs to use the TimingCtxFilter, and "contentType" compare the mime types (currently simply a string comparison))
    // Here we register the members of TestContext as query params, with the member names being the keys, and using the member types
    // for correct matching.
    //
    // Note that the worker uses the same settings for matching, but as it knows about TestContext, it does this registration automatically.
    opencmw::query::registerTypes(SimpleContext(), broker);

    // Create MajordomoWorker with our domain objects, and our TestHandler.
    majordomo::Worker<"/addressbook", SimpleContext, AddressRequest, AddressEntry> worker(broker, TestAddressHandler());

    RunInThread                                                                    brokerRun(broker);
    RunInThread                                                                    workerRun(worker);
    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    std::atomic<int>               responseCount      = 0;

    opencmw::client::RestClient    client;

    // Invalid port, assuming there's nobody listening on port 44444
    opencmw::client::Command cannotReach;
    cannotReach.command  = mdp::Command::Get;
    cannotReach.topic    = opencmw::URI<>("http://localhost:44444/");
    cannotReach.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "Connection refused");
        REQUIRE(msg.data.asString() == "");
        responseCount++;
    };
    client.request(std::move(cannotReach));

    auto serviceListCallback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString() == "/addressbook,/mmi.dns,/mmi.echo,/mmi.openapi,/mmi.service");
        responseCount++;
    };

    // path "" returns service list
    opencmw::client::Command serviceList;
    serviceList.command  = mdp::Command::Get;
    serviceList.topic    = opencmw::URI<>(std::format("http://localhost:{}", kServerPort));
    serviceList.callback = serviceListCallback;
    client.request(std::move(serviceList));

    // path "/" returns service list
    opencmw::client::Command serviceList2;
    serviceList2.command  = mdp::Command::Get;
    serviceList2.topic    = opencmw::URI<>(std::format("http://localhost:{}/", kServerPort));
    serviceList2.callback = serviceListCallback;
    client.request(std::move(serviceList2));

    // Asset file
    opencmw::client::Command getAsset;
    getAsset.command  = mdp::Command::Get;
    getAsset.topic    = opencmw::URI<>(std::format("http://localhost:{}/assets/main.css", kServerPort));
    getAsset.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains("body {"));
        responseCount++;
    };
    client.request(std::move(getAsset));

    // Asset file that does not exist
    opencmw::client::Command getAsset404;
    getAsset404.command  = mdp::Command::Get;
    getAsset404.topic    = opencmw::URI<>(std::format("http://localhost:{}/assets/does-not-exist", kServerPort));
    getAsset404.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "Not found");
        REQUIRE(msg.data.asString() == "");
        responseCount++;
    };
    client.request(std::move(getAsset404));

    waitFor(responseCount, 5);
    responseCount                    = 0;

    constexpr int kExpectedResponses = 4;

    // The following requests are all to the same service and thus must be processed in order

    // Get worker data as JSON
    opencmw::client::Command getJson;
    getJson.command  = mdp::Command::Get;
    getJson.topic    = opencmw::URI<>(std::format("http://localhost:{}/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=application%2Fjson", kServerPort));
    getJson.callback = [&responseCount](const auto &msg) {
        REQUIRE(responseCount == 0);
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains("\"name\": \"Santa Claus\""));
        REQUIRE(msg.topic == opencmw::URI<>("/addressbook?contentType=application%2Fjson&testFilter=&ctx=FAIR.SELECTOR.ALL"));
        responseCount++;
    };
    client.request(std::move(getJson));

    // Get worker data as HTML
    opencmw::client::Command getHtml;
    getHtml.command  = mdp::Command::Get;
    getHtml.topic    = opencmw::URI<>(std::format("http://localhost:{}/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=text%2Fhtml", kServerPort));
    getHtml.callback = [&responseCount](const auto &msg) {
        REQUIRE(responseCount == 1);
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains("<td class=\"propTable-fValue\">Elf Road</td>"));
        REQUIRE(msg.topic == opencmw::URI<>("/addressbook?contentType=text%2Fhtml&testFilter=&ctx=FAIR.SELECTOR.ALL"));
        responseCount++;
    };
    client.request(std::move(getHtml));

    // Set worker data
    opencmw::client::Command postJson;
    postJson.command  = mdp::Command::Set;
    postJson.topic    = opencmw::URI<>(std::format("http://localhost:{}/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=application%2Fjson", kServerPort));
    postJson.data     = opencmw::IoBuffer("{\"streetNumber\": 1882}");
    postJson.callback = [&responseCount](const auto &msg) {
        REQUIRE(responseCount == 2);
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.topic == opencmw::URI<>("/addressbook?contentType=application%2Fjson&testFilter=&ctx=FAIR.SELECTOR.ALL"));
        responseCount++;
    };
    client.request(std::move(postJson));

    // Test that the Set call is correctly applied
    opencmw::client::Command getJsonAfterSet;
    getJsonAfterSet.command  = mdp::Command::Get;
    getJsonAfterSet.topic    = opencmw::URI<>(std::format("http://localhost:{}/addressbook?ctx=FAIR.SELECTOR.ALL&contentType=application%2Fjson", kServerPort));
    getJsonAfterSet.callback = [&responseCount](const auto &msg) {
        REQUIRE(responseCount == 3);
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains("\"streetNumber\": 1882"));
        REQUIRE(msg.topic == opencmw::URI<>("/addressbook?contentType=application%2Fjson&testFilter=&ctx=FAIR.SELECTOR.ALL"));
        responseCount++;
    };
    client.request(std::move(getJsonAfterSet));

    waitFor(responseCount, kExpectedResponses);
}

TEST_CASE("Invalid paths", "[majordomo][majordomoworker][rest]") {
    majordomo::Broker                                          broker("/TestBroker", testSettings());
    majordomo::rest::Settings                                  rest;
    rest.port      = kServerPort;
    rest.protocols = majordomo::rest::Protocol::Http2;
    auto bound     = broker.bindRest(rest);
    REQUIRE(bound);

    opencmw::query::registerTypes(PathContext(), broker);

    PathWorker<"/paths"> worker(broker);

    RunInThread          brokerRun(broker);
    RunInThread          workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    constexpr int                           kExpectedResponses = 1;
    std::atomic<int>                        responseCount      = 0;

    opencmw::client::RestClient             client;

#if 0 // TODO Currently URI<>() throws on %20 in the path (which is a valid URI but not a valid service name)
    opencmw::client::Command                getSpace;
    getSpace.command  = mdp::Command::Get;
    getSpace.topic    = opencmw::URI<>(std::format("http://localhost:{}/paths/with%20space", kServerPort));
    getSpace.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "Invalid service name");
        REQUIRE(msg.data.empty());
        responseCount++;
    };
    client.request(std::move(getSpace));
#endif
    opencmw::client::Command invalidSubscription;
    invalidSubscription.command  = mdp::Command::Get;
    invalidSubscription.topic    = opencmw::URI<>(std::format("http://localhost:{}//p-a-t-h-s/?LongPollIdx=Next", kServerPort));
    invalidSubscription.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "Invalid service name '//p-a-t-h-s'");
        REQUIRE(msg.data.empty());
        responseCount++;
    };

    client.request(std::move(invalidSubscription));

    waitFor(responseCount, kExpectedResponses);
}

TEST_CASE("Get/Set with subpaths", "[majordomo][majordomoworker][rest]") {
    majordomo::Broker             broker("/TestBroker", testSettings());
    majordomo::rest::Settings     rest;
    rest.port  = kServerPort;
    rest.protocols = majordomo::rest::Protocol::Http2;
    auto bound = broker.bindRest(rest);
    if (!bound) {
        FAIL(std::format("Failed to bind REST server: {}", bound.error()));
        return;
    }
    opencmw::query::registerTypes(PathContext(), broker);

    PathWorker<"/paths"> worker(broker);

    RunInThread          brokerRun(broker);
    RunInThread          workerRun(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    constexpr int                           kExpectedResponses = 3;
    std::atomic<int>                        responseCount      = 0;

    opencmw::client::RestClient             client;

    opencmw::client::Command                getEmpty;
    getEmpty.command  = mdp::Command::Get;
    getEmpty.topic    = opencmw::URI<>(std::format("http://localhost:{}/paths", kServerPort));
    getEmpty.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains("path=''"));
        REQUIRE(msg.topic == opencmw::URI<>("/paths?contentType=application%2Fjson"));
        responseCount++;
    };
    client.request(std::move(getEmpty));

    opencmw::client::Command getOne;
    getOne.command  = mdp::Command::Get;
    getOne.topic    = opencmw::URI<>(std::format("http://localhost:{}/paths/a", kServerPort));
    getOne.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains("path='\\/a'"));
        REQUIRE(msg.topic == opencmw::URI<>("/paths/a?contentType=application%2Fjson"));
        responseCount++;
    };
    client.request(std::move(getOne));

    opencmw::client::Command getTwo;
    getTwo.command  = mdp::Command::Get;
    getTwo.topic    = opencmw::URI<>(std::format("http://localhost:{}/paths/a/b", kServerPort));
    getTwo.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains("path='\\/a\\/b'"));
        REQUIRE(msg.topic == opencmw::URI<>("/paths/a/b?contentType=application%2Fjson"));
        responseCount++;
    };
    client.request(std::move(getTwo));

    waitFor(responseCount, kExpectedResponses);
}

TEST_CASE("Subscriptions", "[majordomo][majordomoworker][subscription]") {
    majordomo::Broker             broker("/TestBroker", testSettings());
    majordomo::rest::Settings     rest;
    rest.port        = kServerPort;
    rest.protocols   = majordomo::rest::Protocol::Http2;
    const auto bound = broker.bindRest(rest);
    if (!bound) {
        FAIL(std::format("Failed to bind REST server: {}", bound.error()));
        return;
    }
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

    opencmw::client::RestClient             client;

    constexpr auto                          allExpected    = std::array{ "0", "1", "2", "3", "4", "5", "6" };
    constexpr auto                          redExpected    = std::array{ "0", "3", "4", "6" };
    constexpr auto                          yellowExpected = std::array{ "4", "6" };
    constexpr auto                          whiteExpected  = std::array{ "6" };

    std::atomic<std::size_t>                allReceived    = 0;
    std::atomic<std::size_t>                redReceived    = 0;
    std::atomic<std::size_t>                yellowReceived = 0;
    std::atomic<std::size_t>                whiteReceived1 = 0;
    std::atomic<std::size_t>                whiteReceived2 = 0;
    std::atomic<std::size_t>                whiteReceived3 = 0;

    opencmw::client::Command                allSub;
    allSub.command  = mdp::Command::Subscribe;
    allSub.topic    = opencmw::URI<>(std::format("http://localhost:{}/colors", kServerPort));
    allSub.callback = [&allReceived, &allExpected](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Notify);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains(allExpected[allReceived]));
        allReceived++;
    };
    client.request(std::move(allSub));

    opencmw::client::Command redSub;
    redSub.command  = mdp::Command::Subscribe;
    redSub.topic    = opencmw::URI<>(std::format("http://localhost:{}/colors?red", kServerPort));
    redSub.callback = [&redReceived, &redExpected](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Notify);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains(redExpected[redReceived]));
        redReceived++;
    };
    client.request(std::move(redSub));

    opencmw::client::Command yellowSub;
    yellowSub.command  = mdp::Command::Subscribe;
    yellowSub.topic    = opencmw::URI<>(std::format("http://localhost:{}/colors?red&green", kServerPort));
    yellowSub.callback = [&yellowReceived, &yellowExpected](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Notify);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains(yellowExpected[yellowReceived]));
        yellowReceived++;
    };
    client.request(std::move(yellowSub));

    opencmw::client::Command whiteSub1;
    whiteSub1.command  = mdp::Command::Subscribe;
    whiteSub1.topic    = opencmw::URI<>(std::format("http://localhost:{}/colors?red&green&blue", kServerPort));
    whiteSub1.callback = [&whiteReceived1, &whiteExpected](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Notify);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains(whiteExpected[whiteReceived1]));
        whiteReceived1++;
    };
    client.request(std::move(whiteSub1));

    opencmw::client::Command whiteSub2;
    whiteSub2.command  = mdp::Command::Subscribe;
    whiteSub2.topic    = opencmw::URI<>(std::format("http://localhost:{}/colors?green&red&blue", kServerPort));
    whiteSub2.callback = [&whiteReceived2, &whiteExpected](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Notify);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains(whiteExpected[whiteReceived2]));
        whiteReceived2++;
    };
    client.request(std::move(whiteSub2));

    opencmw::client::Command whiteSub3;
    whiteSub3.command  = mdp::Command::Subscribe;
    whiteSub3.topic    = opencmw::URI<>(std::format("http://localhost:{}/colors?blue&green&red", kServerPort));
    whiteSub3.callback = [&whiteReceived3, &whiteExpected](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Notify);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString().contains(whiteExpected[whiteReceived3]));
        whiteReceived3++;
    };

    client.request(std::move(whiteSub3));

    waitFor(allReceived, allExpected.size());
    waitFor(redReceived, redExpected.size());
    waitFor(yellowReceived, yellowExpected.size());
    waitFor(whiteReceived1, whiteExpected.size());
    waitFor(whiteReceived2, whiteExpected.size());
    waitFor(whiteReceived3, whiteExpected.size());

    std::vector<std::string> subscriptions;
    for (const auto &subscription : worker.activeSubscriptions()) {
        subscriptions.push_back(subscription.toZmqTopic());
    }
    std::ranges::sort(subscriptions);
    REQUIRE(subscriptions == std::vector<std::string>{ "/colors#", "/colors?blue&green&red#", "/colors?green&red#", "/colors?red#" });
}

TEST_CASE("Handler matching", "[majordomo][rest]") {
    majordomo::Broker         broker("/TestBroker", testSettings());
    majordomo::rest::Settings rest;
    rest.port        = kServerPort;
    rest.protocols   = majordomo::rest::Protocol::Http2;

    auto echoHandler
            = [](std::string method, std::string path) {
                  return majordomo::rest::Handler{
                      .method  = method,
                      .path    = path,
                      .handler = [path, method](const auto &) {
                          majordomo::rest::Response response;
                          response.code = 200;
                          response.body.put<opencmw::IoBuffer::WITHOUT>(std::format("{}|{}", method, path));
                          return response;
                      }
                  };
              };

    // Cannot test POST because Command::Set is also using "GET"
    rest.handlers = {
        echoHandler("GET", "/"),
        echoHandler("GET", "/assets/*"),
        echoHandler("GET", "/assets/subresource")
    };

    auto bound = broker.bindRest(rest);
    if (!bound) {
        FAIL(std::format("Failed to bind REST server: {}", bound.error()));
        return;
    }

    RunInThread                 brokerRun(broker);

    opencmw::client::RestClient client;

    std::atomic<int>            responseCount = 0;

    opencmw::client::Command    getRoot;
    getRoot.command  = mdp::Command::Get;
    getRoot.topic    = opencmw::URI<>(std::format("http://localhost:{}/", kServerPort));
    getRoot.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString() == "GET|/");
        responseCount++;
    };
    client.request(std::move(getRoot));

    opencmw::client::Command getAssets;
    getAssets.command  = mdp::Command::Get;
    getAssets.topic    = opencmw::URI<>(std::format("http://localhost:{}/assets/test.txt", kServerPort));
    getAssets.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString() == "GET|/assets/*");
        responseCount++;
    };
    client.request(std::move(getAssets));

    opencmw::client::Command getSubresource;
    getSubresource.command  = mdp::Command::Get;
    getSubresource.topic    = opencmw::URI<>(std::format("http://localhost:{}/assets/subresource", kServerPort));
    getSubresource.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString() == "GET|/assets/subresource");
        responseCount++;
    };
    client.request(std::move(getSubresource));

    opencmw::client::Command getSubresource2;
    getSubresource2.command  = mdp::Command::Get;
    getSubresource2.topic    = opencmw::URI<>(std::format("http://localhost:{}/assets/subresource/extra", kServerPort));
    getSubresource2.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString() == "GET|/assets/*");
        responseCount++;
    };
    client.request(std::move(getSubresource2));
    waitFor(responseCount, 4);
}

constexpr auto kExpectedFileContent = R"(-----BEGIN CERTIFICATE-----
MIIFiTCCA3GgAwIBAgIUDBxaxLvthSz4Knvh6R0/zDrxe3QwDQYJKoZIhvcNAQEL
BQAwVDELMAkGA1UEBhMCREUxEDAOBgNVBAgMB1Vua25vd24xEDAOBgNVBAcMB1Vu
a25vd24xITAfBgNVBAoMGEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDAeFw0yMTEy
MDUxMDA0MjFaFw0zMTEyMDMxMDA0MjFaMFQxCzAJBgNVBAYTAkRFMRAwDgYDVQQI
DAdVbmtub3duMRAwDgYDVQQHDAdVbmtub3duMSEwHwYDVQQKDBhJbnRlcm5ldCBX
aWRnaXRzIFB0eSBMdGQwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQDx
1Es3W/5OyMdOPUmCqQuAYV/4xjhP8Fhoi1gjnnlaCrBbBXJl1nW8DdMwVhXF9Yy8
wPP0SylqkbatiDnUwjviizL6v6DMNQbS+OES5OleCuwbCWAFH3vsDllRZl3LYAdB
6Ec4wNjX5EjE1RgIgT+GEkR13XqyHQi4ELOMEUxxpVcWeBjAFhgiTXvbpnBfBfJo
XPsMoCaWTWhRQksodKM4Mjfn/wxKAfbspNaX5zfPcr/5vNGY2CYiKbsZqwiM5VNq
ml9XoUc5BIWuj4liHerLOqdEj3zpBhn3i+RimGm+N2xDAXOKMlP4w5jyewd5FtLl
vmyLEakiqwSCODkjrP7rbmQ9hRohsF8V5Y4KwaYYEp+pZ4BFCBYdDv+drnVD8MOJ
/7f8LlCE3xN/MvEX2Um1xt5oT4gb3SdSTRZfUEFzkrQK5wodQBUZVhVTg0NRwiWx
hfeILR6/Qd8pciwSXbkT7JBEf1gyghKFDd+GfyDmm/+aIxNAItQ10KUDeLyavP3x
SN9ZR4zITDT9dwzd/yqsKudFB2mqioUv8zXah7lRpQDQmsrwkj4sKOrfS9BxtM8T
tUn56aOeyl7zdWvhI+wJOT07f7l7+c3WW06v9jPXk0IDjhvUdn11uL6aFQiO6cBE
4Jj2z2l4VHsLekcPdXzt6V1IhJuyGWSasOZBwb13tQIDAQABo1MwUTAdBgNVHQ4E
FgQUwA8sghflexohahmoZAFHcpgnJl0wHwYDVR0jBBgwFoAUwA8sghflexohahmo
ZAFHcpgnJl0wDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAgEAXlVh
oCF+7ywVU1ek2wcPIDayqAUA2JnPGvpjClSqqOfAvst07RspDftxTQ0/BVy7N1Ep
DX9uQP1YiuvZDxc4ySCYUnYiXmf7aeyjSGIwVHhWRx/uQQV880tJ+TIK+JJRLiJg
DLnixdyW/uPY0RkjUzHCADI1zZmrJyZpMAFbqwuoVOtEoMmCJkjJRrZzytRQkTbe
9cUAvHcdjbFADf1yZdTELgNTs9Xolb+aZhXb+DolrQiTpDQj9RSt/raaRyrlssFD
9V0ugW2e9nEEe7PlofDGOYqhaadka9s680xT47s6K8WjPVFwgDVKKojW186JCeAR
8W8AEd1tOmp8BQOY7OLY8hZ9kTnnd8XoLy+l8UH03kfIIPAulGARNCYYo7SJfQU7
1rQEf28BGi9mL0QhkY0xSSTvuLVbG5DAceUVix6Y+NsTgF+YCWsqXMZX6M/qHjiy
7qf5LeKrLqG1RQsYbi4UzakKfwG2uVH/lETc/j1PMZz4WPPJGeeg+urhHvzvZJ5C
xbuJH3fc9TsTET4FiwQwINSwdY4i/iNCt3kE+6EYNf1G13f3ffCMy3JYxakXjDZB
ido85zpB/BELE69ap2g/pHCNEd9y2ZHCYDvIZaxnJtdBBhWmHqAK00HYEwG5LZPU
I0uPpRG4pT+BvLUZo8rKVpFLHj2nnflS5dXqKPo=
-----END CERTIFICATE-----
)";

template<std::size_t MMapThreshold>
void runFilesystemTest() {
    majordomo::Broker         broker("/TestBroker", testSettings());
    majordomo::rest::Settings rest;
    rest.port      = kServerPort;
    rest.protocols = majordomo::rest::Protocol::Http2;
    rest.handlers  = { majordomo::rest::fileSystemHandler("/files/*", "/files/", std::filesystem::current_path(), {}, MMapThreshold) };
    auto bound     = broker.bindRest(rest);
    if (!bound) {
        FAIL(std::format("Failed to bind REST server: {}", bound.error()));
        return;
    }

    RunInThread                 brokerRun(broker);

    std::atomic<int>            responseCount = 0;
    opencmw::client::RestClient client;

    opencmw::client::Command    fileExists;
    fileExists.command  = mdp::Command::Get;
    fileExists.topic    = opencmw::URI<>(std::format("http://localhost:{}/files/demo_public.crt", kServerPort));
    fileExists.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString() == kExpectedFileContent);
        responseCount++;
    };
    client.request(std::move(fileExists));

    opencmw::client::Command fileDoesNotExist;
    fileDoesNotExist.command  = mdp::Command::Get;
    fileDoesNotExist.topic    = opencmw::URI<>(std::format("http://localhost:{}/files/does_not_exist", kServerPort));
    fileDoesNotExist.callback = [&responseCount](const auto &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "Not found");
        REQUIRE(msg.data.asString() == "");
        responseCount++;
    };
    client.request(std::move(fileDoesNotExist));

    waitFor(responseCount, 2);
};

TEST_CASE("File system handler (stream)", "[majordomo][majordomoworker][rest]") {
    runFilesystemTest<1024 * 1024 * 100>();
}

TEST_CASE("File system handler (mmap)", "[majordomo][majordomoworker][rest]") {
    runFilesystemTest<1>();
}

TEST_CASE("Subscription latencies", "[majordomo][majordomoworker][rest]") {
    std::atomic<int> nReceived = 0;
    std::atomic<int> msLatency = 0;
    {
        majordomo::Broker                                           broker("/TestBroker", testSettings());
        majordomo::rest::Settings                                   rest;
        rest.port             = kServerPort;
        rest.protocols        = majordomo::rest::Protocol::Http2;
        auto bound            = broker.bindRest(rest);
        if (!bound) {
            FAIL(std::format("Failed to bind REST server: {}", bound.error()));
            return;
        }
        ClockWorker<"/clock", 2550> worker(broker, 10ms, 70);
        RunInThread                brokerRun(broker);
        RunInThread                workerRun(worker);

        REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

        opencmw::client::RestClient  client;

        opencmw::client::Command     command;
        command.command  = opencmw::mdp::Command::Subscribe;
        command.topic    = opencmw::URI<>(std::format("http://localhost:{}/clock", kServerPort));
        command.callback = [&nReceived, &msLatency](const opencmw::mdp::Message &reply) {
            UpdateTime        replyData;
            opencmw::IoBuffer buffer = reply.data;
            opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::ALWAYS>(buffer, replyData);
            auto now     = std::chrono::high_resolution_clock::now();
            auto latency = now.time_since_epoch() - std::chrono::microseconds(replyData.updateTimeµs);
            nReceived++;
            nReceived.notify_all();
            msLatency.fetch_add(static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(latency).count()));
            std::print("Received {}th update with a latency of {} µs.\n", nReceived.load(), std::chrono::duration_cast<std::chrono::microseconds>(latency).count());
        };
        client.request(std::move(command));

        std::print("waiting for 40 samples to be received\n");
        int n = nReceived;
        while (n < 40) {
            nReceived.wait(n);
            n = nReceived;
        }
    }

    std::print("Received {} updates with an average latency of {} µs.\n", nReceived.load(), nReceived > 0 ? static_cast<double>(msLatency) / nReceived : 0.0);
    REQUIRE(nReceived > 10);
    REQUIRE(static_cast<double>(msLatency) / nReceived < 20000); // unit is µs
}
