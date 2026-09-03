#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/threading.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <iterator>
#include <optional>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include <RestClient.hpp>

using namespace opencmw;
using namespace opencmw::client;

namespace {

constexpr int kStreamACount = 5;
constexpr int kStreamBCount = 1;
constexpr int kStreamCCount = 5;
constexpr int kStreamAFirst = 7;
constexpr int kStreamBFirst = 5;
constexpr int kStreamCFirst = 5;
constexpr int kStreamCStart = 3;

struct TestState {
    std::atomic_int                       failures{};
    int                                   initialRunningWorkers{};
    int                                   initialUnusedWorkers{};
    std::optional<RestClient>             client;
    std::atomic_int                       messagesA{};
    std::atomic_int                       messagesB{};
    std::atomic_int                       messagesC{};
    std::atomic_int                       errorsC{};
    std::atomic_bool                      sawMainThread{};
    std::atomic_bool                      sawWorkerThread{};
    std::string                           receivedA;
    std::string                           receivedB;
    std::string                           receivedC;
    int                                   callbackCountAtCleanup{};
    std::chrono::steady_clock::time_point deadline;
};

TestState &testState(void *data) {
    return *static_cast<TestState *>(data);
}

void check(TestState &state, bool condition, std::string_view failure, std::source_location location = std::source_location::current()) {
    if (!condition) {
        std::println("{}:{}: FAIL: {}", location.file_name(), location.line(), failure);
        ++state.failures;
    }
}

void checkBatchSubscriptionOverlap(TestState &state) {
    for (const auto &[firstQuery, secondQuery] : {
                 std::pair{ "?LongPollingBatch=2", "?LongPollingIdx=5&LongPollingBatch=3" },
                 std::pair{ "?LongPollingBatch=2", "?LongPollingIdx=5" },
                 std::pair{ "", "?LongPollingIdx=5&LongPollingBatch=3" } }) {
        client::detail::RestWorkerState worker{ MIME::BINARY };
        const URI<STRICT>              firstTopic(std::format("http://localhost/stream{}", firstQuery));
        Command                        first;
        first.command = mdp::Command::Subscribe;
        first.topic   = firstTopic;
        worker._subscriptions.emplace(1, client::detail::SubscriptionState{ .command = std::move(first) });
        worker._nextSubscriptionId = 2;

        int     errors = 0;
        Command second;
        second.command  = mdp::Command::Subscribe;
        second.topic    = URI<STRICT>(std::format("http://localhost/stream{}", secondQuery));
        second.callback = [&](const mdp::Message &message) {
            check(state, !message.error.empty(), "overlapping subscription did not report an error");
            ++errors;
        };
        worker.startSubscription(std::move(second));

        check(state, errors == 1, "overlapping subscription was not rejected exactly once");
        check(state, worker._subscriptions.size() == 1, "overlapping subscription was added");
        check(state, worker._subscriptions.at(1).command.topic == firstTopic, "overlapping subscription changed the active topic");
        check(state, worker._activeFetches.empty(), "overlapping subscription started a request");
        while (!worker._activeFetches.empty()) {
            worker.closeFetch(worker._activeFetches.begin()->first);
        }
    }
}

// PThread worker counts are Emscripten internals used only for this leak check.
int runningWorkerCount() noexcept {
    return EM_ASM_INT({ return PThread.runningWorkers.length; });
}

int unusedWorkerCount() noexcept {
    return EM_ASM_INT({ return PThread.unusedWorkers.length; });
}

bool workerPoolRestored(const TestState &state) noexcept {
    return runningWorkerCount() == state.initialRunningWorkers && unusedWorkerCount() == state.initialUnusedWorkers;
}

std::string testPayload(int index) {
    std::string expected = std::format("{}:", index);
    for (int i = 0; i < 100; ++i) {
        std::format_to(std::back_inserter(expected), "{}", i);
    }
    return expected;
}

std::string expectedPayloads(int first, int count) {
    std::string expected;
    for (int index = first; index < first + count; ++index) {
        expected += testPayload(index);
    }
    return expected;
}

void recordCallbackThread(TestState &state) {
    if (emscripten_is_main_runtime_thread()) {
        state.sawMainThread.store(true, std::memory_order_relaxed);
    } else {
        state.sawWorkerThread.store(true, std::memory_order_relaxed);
    }
}

void reportAndExit(const TestState &state) {
    const int failures = state.failures.load();
    std::println("=== {} ({} failure{}) ===", failures == 0 ? "PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

void finishTest(void *data) {
    auto     &state     = testState(data);
    const int messagesA = state.messagesA.load(std::memory_order_acquire);
    const int messagesB = state.messagesB.load(std::memory_order_acquire);
    const int messagesC = state.messagesC.load(std::memory_order_acquire);
    const int errorsC   = state.errorsC.load(std::memory_order_acquire);
    const int callbacks = messagesA + messagesB + messagesC + errorsC;

    check(state, state.callbackCountAtCleanup == callbacks, std::format("callback count changed after cleanup ({} to {})", state.callbackCountAtCleanup, callbacks));
    check(state, workerPoolRestored(state), "worker count changed after cleanup");
    check(state, messagesA == kStreamACount, std::format("stream A delivered {} messages, expected {}", messagesA, kStreamACount));
    check(state, messagesB == kStreamBCount, std::format("stream B delivered {} messages, expected {}", messagesB, kStreamBCount));
    check(state, messagesC == kStreamCCount, std::format("stream C delivered {} messages, expected {}", messagesC, kStreamCCount));
    check(state, errorsC == 1, std::format("stream C reported {} errors, expected one malformed batch", errorsC));
    check(state, !state.sawMainThread.load(std::memory_order_relaxed), "a callback ran on the browser main thread");
    check(state, state.sawWorkerThread.load(std::memory_order_relaxed), "no callback ran on the REST worker");

    check(state, state.receivedA == expectedPayloads(kStreamAFirst, kStreamACount), "stream A payload differs");
    check(state, state.receivedB == expectedPayloads(kStreamBFirst, kStreamBCount), "stream B payload differs");
    check(state, state.receivedC == expectedPayloads(kStreamCFirst, kStreamCCount), "stream C payload differs");

    reportAndExit(state);
}

void waitForCleanup(void *);

void waitForDelivery(void *data) {
    // Must exceed PROBE_DELAY_SECONDS in run.py.
    constexpr auto kUnsubscribeSettle = std::chrono::milliseconds{ 1500 };

    auto          &state              = testState(data);
    const auto     now                = std::chrono::steady_clock::now();
    if (state.messagesA.load(std::memory_order_acquire) >= kStreamACount
            && state.messagesB.load(std::memory_order_acquire) >= kStreamBCount
            && state.messagesC.load(std::memory_order_acquire) >= kStreamCCount) {
        emscripten_cancel_main_loop();
        // Allow delayed A/C responses to expose failed unsubscribes; B keeps its poll open.
        emscripten_set_timeout([](void *callbackData) {
            constexpr auto kCleanupTimeout = std::chrono::seconds{ 10 };

            auto          &callbackState   = testState(callbackData);
            const auto     begin           = std::chrono::steady_clock::now();
            callbackState.client->stop();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);

            check(callbackState, elapsed < std::chrono::seconds{ 5 }, std::format("stop() took {} ms with a long poll open", elapsed.count()));
            callbackState.client.reset();

            callbackState.deadline = std::chrono::steady_clock::now() + kCleanupTimeout;
            waitForCleanup(callbackData);
        },
                kUnsubscribeSettle.count(), data);
        return;
    }
    if (now > state.deadline) {
        check(state, false, "timed out waiting for subscription messages");
        reportAndExit(state);
    }
}

void waitForCleanup(void *data) {
    constexpr auto kPollInterval    = std::chrono::milliseconds{ 20 };
    constexpr auto kStabilityWindow = std::chrono::milliseconds{ 100 };

    auto          &state            = testState(data);
    const auto     now              = std::chrono::steady_clock::now();
    if (workerPoolRestored(state)) {
        state.callbackCountAtCleanup = state.messagesA.load(std::memory_order_acquire) + state.messagesB.load(std::memory_order_acquire) + state.messagesC.load(std::memory_order_acquire) + state.errorsC.load(std::memory_order_acquire);
        emscripten_set_timeout(&finishTest, kStabilityWindow.count(), data);
        return;
    }
    if (now > state.deadline) {
        check(state, false, std::format("worker pool not restored (running {}/{}, unused {}/{})", runningWorkerCount(), state.initialRunningWorkers, unusedWorkerCount(), state.initialUnusedWorkers));
        reportAndExit(state);
        return;
    }
    emscripten_set_timeout(&waitForCleanup, kPollInterval.count(), data);
}

} // namespace

int main(int argc, char **argv) {
    constexpr std::string_view portFlag         = "--port=";
    constexpr auto             kDeliveryTimeout = std::chrono::seconds{ 15 };
    // Main-loop callbacks retain this state for the process lifetime.
    static TestState state;

    int              port = 0;
    for (int i = 1; i < argc; ++i) {
        if (const std::string_view arg{ argv[i] }; arg.starts_with(portFlag)) {
            port = std::atoi(arg.data() + portFlag.size());
        }
    }
    if (port == 0) {
        std::println("no server port: start this through emscripten_rest_client/run.py");
        return 2;
    }

    const URI<STRICT> topicA(std::format("http://127.0.0.1:{}/streamA?b=2&LongPollingIdx=Next&value=%41&a=1", port));
    const URI<STRICT> topicB(std::format("http://127.0.0.1:{}/streamB", port));
    const URI<STRICT> topicC(std::format("http://127.0.0.1:{}/streamC?LongPollingIdx={}&LongPollingBatch=AllAvailable", port, kStreamCStart));

    std::println("Emscripten RestClient integration test (server on port {})", port);

    checkBatchSubscriptionOverlap(state);

    state.initialRunningWorkers = runningWorkerCount();
    state.initialUnusedWorkers  = unusedWorkerCount();
    state.deadline              = std::chrono::steady_clock::now() + kDeliveryTimeout;

    state.client.emplace();

    Command subscribeA;
    subscribeA.command  = mdp::Command::Subscribe;
    subscribeA.topic    = topicA;
    subscribeA.callback = [test = &state, topicA](const mdp::Message &message) {
        recordCallbackThread(*test);
        test->receivedA += message.data.asString();
        if (test->messagesA.load(std::memory_order_relaxed) == kStreamACount - 1) {
            Command unsubscribe;
            unsubscribe.command = mdp::Command::Unsubscribe;
            unsubscribe.topic   = URI<STRICT>(std::format("http://127.0.0.1:{}/streamA?value=%41&a=1&b=2", topicA.port().value()));
            test->client->request(std::move(unsubscribe));
        }
        test->messagesA.fetch_add(1, std::memory_order_release);
    };
    state.client->request(std::move(subscribeA));

    Command subscribeB;
    subscribeB.command  = mdp::Command::Subscribe;
    subscribeB.topic    = topicB;
    subscribeB.callback = [test = &state](const mdp::Message &message) {
        recordCallbackThread(*test);
        test->receivedB += message.data.asString();
        test->messagesB.fetch_add(1, std::memory_order_release);
    };
    state.client->request(std::move(subscribeB));

    Command subscribeC;
    subscribeC.command  = mdp::Command::Subscribe;
    subscribeC.topic    = topicC;
    subscribeC.callback = [test = &state, port](const mdp::Message &message) {
        recordCallbackThread(*test);
        if (!message.error.empty() && message.data.empty()) {
            check(*test, test->messagesC.load(std::memory_order_relaxed) == 2, "stream C error did not follow its first batch");
            test->errorsC.fetch_add(1, std::memory_order_release);
            return;
        }
        const auto index = kStreamCFirst + test->messagesC.load(std::memory_order_relaxed);
        if (index == kStreamCFirst) {
            check(*test, !message.error.empty(), "stream C did not report its initial index gap");
        } else {
            check(*test, message.error.empty(), "stream C reported an unexpected warning");
        }
        check(*test, message.topic == URI<STRICT>(std::format("/streamC?sample={}", index)), "stream C received the wrong topic");
        check(*test, message.serviceName == std::format("/streamC-service-{}", index), "stream C received the wrong service name");
        test->receivedC += message.data.asString();
        if (test->messagesC.load(std::memory_order_relaxed) == kStreamCCount - 1) {
            Command unsubscribe;
            unsubscribe.command = mdp::Command::Unsubscribe;
            unsubscribe.topic   = URI<STRICT>(std::format("http://127.0.0.1:{}/streamC", port));
            test->client->request(std::move(unsubscribe));
        }
        test->messagesC.fetch_add(1, std::memory_order_release);
    };
    state.client->request(std::move(subscribeC));

    emscripten_set_main_loop_arg(&waitForDelivery, &state, 0, EM_TRUE);
}
