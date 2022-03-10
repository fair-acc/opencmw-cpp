#include <catch2/catch.hpp>

#include <cassert>

#include <utility>
#include <variant>

#include <disruptor/Disruptor.hpp>
#include <disruptor/RingBuffer.hpp>
#include <disruptor/RoundRobinThreadAffinedTaskScheduler.hpp>
#include <disruptor/WaitStrategy.hpp>

#include <disruptor/Rx.hpp>

using namespace opencmw::disruptor;
using namespace std::string_literals;
using namespace std::chrono_literals;

template<typename... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

constexpr std::size_t timeUnitMilliseconds  = 100;
constexpr std::size_t longPauseMilliseconds = 1000;
constexpr std::size_t heartbeatMilliseconds = timeUnitMilliseconds;

// A custom aggregation policy that has a time limit
// and groups events based on their IDs
template<std::size_t TimeLimit, template<typename...> typename Wrapper = std::type_identity>
struct TestAggregatorPolicy : rx::Aggreagate::Timed<TimeLimit, Wrapper> {
    using IdMatcherPolicyTag = std::true_type;

    static std::size_t idForEvent(const auto &event) {
        return event.index;
    }

    TestAggregatorPolicy()
        : rx::Aggreagate::Timed<TimeLimit, Wrapper>() {
    }
};
static_assert(rx::Aggreagate::IdMatcherPolicy<TestAggregatorPolicy<1>>);

struct TestEvent {
    char                 device                                    = 'X';
    std::size_t          index                                     = 0;
    bool                 isValid                                   = false;

    std::strong_ordering operator<=>(const TestEvent &other) const = default;
};

using TestEvents       = std::vector<TestEvent>;
using AggregatedEvent  = std::tuple<TestEvent, TestEvent, TestEvent>;
using AggregatedEvents = std::vector<AggregatedEvent>;

std::ostream &operator<<(std::ostream &out, const TestEvent &event) {
    if (!event.isValid) {
        return out << 'X';
    }
    return out << event.device << event.index;
}

// clang-format off
// clang format can not format this:
template<typename... Events>
requires(std::is_same_v<Events, TestEvent> &&...)
std::ostream & operator<<(std::ostream &out, const std::tuple<Events...> &events) {
    rx::detail::tuple_for_each(events,
            [&out, first = true] <typename Index> (Index, const auto&event) mutable {
                if (!first) {
                    out << " ";
                }
                out << event;
                first = false;
                });
    return out;
}
// clang-format on

std::istream &operator>>(std::istream &input, TestEvent &event) {
    input >> event.device;
    if (event.device == 'X') {
        event.isValid = false;

    } else {
        event.isValid = true;
        input >> event.index;
    }
    return input;
}

template<typename... Events>
requires(std::is_same_v<Events, TestEvent> &&...)
        std::istream &
        operator>>(std::istream &input, std::tuple<Events...> &events) {
    rx::detail::tuple_for_each(events,
            [&input]<typename Index>(Index, auto &event) mutable {
                input >> event;
            });
    return input;
}

std::ostream &operator<<(std::ostream &out, const std::vector<TestEvent> &events) {
    bool first = true;
    for (const auto &event : events) {
        if (!first) {
            out << "; ";
        }
        out << event;
        first = false;
    }
    return out;
}

template<typename T>
requires(std::is_same_v<T, TestEvent> || std::is_same_v<T, AggregatedEvent>)
        std::istream &
        operator>>(std::istream &input, std::vector<T> &events) {
    events.clear();
    T event;
    while (input >> event) {
        events.push_back(event);

        if constexpr (std::is_same_v<T, AggregatedEvent>) {
            char skipSemicolon = 0;
            if (input >> skipSemicolon) {
                assert(skipSemicolon == ';');
            }
        }
    }
    return input;
}

template<size_t RbSize>
class Publisher {
private:
    std::shared_ptr<RingBuffer<TestEvent, RbSize>> m_ringBuffer;
    TestEvents                             m_events;
    std::size_t                            m_patternRepeat;

public:
    Publisher(std::shared_ptr<RingBuffer<TestEvent, RbSize>> ringBuffer,
            TestEvents                                      events,
            std::size_t                                     patternRepeatCount)
        : m_ringBuffer(std::move(ringBuffer))
        , m_events(std::move(events))
        , m_patternRepeat(patternRepeatCount) {}

    void run() {
        const auto timeUnit  = std::chrono::milliseconds(timeUnitMilliseconds);
        const auto longPause = std::chrono::milliseconds(longPauseMilliseconds);
        try {
            for (std::size_t repeat = 0; repeat < m_patternRepeat; repeat++) {
                for (const auto &event : m_events) {
                    if (event.device == 'P') {
                        std::this_thread::sleep_for(event.index * timeUnit);
                    } else {
                        auto                next                     = m_ringBuffer->next();
                        auto               &testEvent                = (*m_ringBuffer)[next];

                        constexpr const int repeatIterationIncrement = 1000;
                        testEvent                                    = event;
                        testEvent.index += repeat * repeatIterationIncrement;
                        m_ringBuffer->publish(next);

                        std::this_thread::sleep_for(timeUnit);
                    }
                }
            }

            // Wait for all events to get propagated
            std::this_thread::sleep_for(longPause);

        } catch (...) {
            failed = true;
        }
    }

    bool failed = false;
};

template<std::size_t RbSize>
std::shared_ptr<Publisher<RbSize>> makePublisher(
        const std::shared_ptr<RingBuffer<TestEvent, RbSize>> &buffer,
        const TestEvents                             &events,
        std::size_t                                   patternRepeatCount) {
    return std::make_shared<Publisher<RbSize>>(buffer, events, patternRepeatCount);
}

bool test(std::string_view name, const TestEvents &input, const AggregatedEvents &expectedResult, const AggregatedEvents &expectedTimedOut, std::size_t patternRepeatCount) {
    constexpr std::size_t rbSize = 1 << 16;
    std::cerr << "TEST: " << name << '\n';
    auto processorsCount = std::max(std::thread::hardware_concurrency() / 2, 1U);

    using TestDisruptor  = Disruptor<TestEvent, rbSize, ProducerType::Multi, RoundRobinThreadAffinedTaskScheduler, BusySpinWaitStrategy>;
    TestDisruptor testDisruptor(processorsCount);

    auto          ringBuffer = testDisruptor->ringBuffer();
    testDisruptor->setDefaultExceptionHandler(std::make_shared<FatalExceptionHandler<TestEvent>>());

    // auto       handlers       = makeHandlers(testDisruptor, handlerCount);
    auto publisher = makePublisher(ringBuffer, input, patternRepeatCount);

    // Rx events from the Disruptor
    rx::Source<TestDisruptor> disruptorSource(testDisruptor);

    // Splitting stream
    auto equalTo = [](char device) {
        return [=](const TestEvent &event) { return event.device == device; };
    };

    // Taking the events out from the disruptor and splitting them into
    // per-device streams (filter part). The distinct_until_changed is not
    // needed, but serves as a demonstration of transforation chaining.
    auto             deviceAStream  = disruptorSource.stream() | rxcpp::operators::filter(equalTo('a')) | rxcpp::operators::distinct_until_changed();
    auto             deviceBStream  = disruptorSource.stream() | rxcpp::operators::filter(equalTo('b')) | rxcpp::operators::distinct_until_changed();
    auto             deviceCStream  = disruptorSource.stream() | rxcpp::operators::filter(equalTo('c')) | rxcpp::operators::distinct_until_changed();

    constexpr int    expirationTime = 1000;
    auto             aggregate      = rx::aggreagate<TestAggregatorPolicy<expirationTime>>(deviceAStream, deviceBStream, deviceCStream);

    AggregatedEvents result;
    AggregatedEvents timedOut;
    aggregate.stream()
            .subscribe(
                    [&result, &timedOut](const auto &event) {
                        bool isValid = true;
                        rx::detail::tuple_for_each(event, [&isValid]<typename Index>(Index, const auto &component) {
                            isValid = isValid && component.isValid;
                        });
                        if (isValid) {
                            result.push_back(event);
                        } else {
                            timedOut.push_back(event);
                        }
                    },
                    [] { std::cerr << "Rx: Stream ended.\n"; });

    // Start the disruptor and wait for everything to get processed
    testDisruptor->start();

    std::jthread heartbeat([&aggregate](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(heartbeatMilliseconds));
            aggregate.sendHeartbeat();
        }
    });

    {
        std::jthread thread([publisher] { publisher->run(); });
    }

    heartbeat.request_stop();
    aggregate.flush();

    std::ranges::sort(result);
    std::ranges::sort(timedOut);
    const bool resultMatches  = result == expectedResult;
    const bool skippedMatches = timedOut == expectedTimedOut;

    if (!resultMatches || !skippedMatches) {
        std::cerr << "TEST FAILED: " << name << '\n';
        std::cerr << "-- input\n";
        for (const auto &event : input) {
            std::cerr << event << ' ';
        }
        std::cerr << std::endl;
    }

    if (!resultMatches) {
        std::cerr << "-- expected result\n";
        for (const auto &event : expectedResult) {
            std::cerr << event << std::endl;
        }
        std::cerr << "-- result\n";
        for (const auto &event : result) {
            std::cerr << event << std::endl;
        }
        std::cerr << "-----------------\n";
    }

    if (!skippedMatches) {
        std::cerr << "-- expected timed out\n";
        for (const auto &event : expectedTimedOut) {
            std::cerr << event << std::endl;
        }
        std::cerr << "-- timed out\n";
        for (const auto &event : timedOut) {
            std::cerr << event << std::endl;
        }
        std::cerr << "-----------------\n";
    }

    return resultMatches && skippedMatches;
}

bool test(std::string_view name, const std::string &input, const std::string &expectedResult, const std::string &expectedTimedOut, std::size_t patternRepeatCount) {
    TestEvents parsedInput;
    {
        std::stringstream source(input);
        source >> parsedInput;
    }
    AggregatedEvents parsedExpectedResult;
    {
        std::stringstream source(expectedResult);
        source >> parsedExpectedResult;
    }
    AggregatedEvents parsedExpectedTimedOut;
    {
        std::stringstream source(expectedTimedOut);
        source >> parsedExpectedTimedOut;
    }

    return test(name, parsedInput, parsedExpectedResult, parsedExpectedTimedOut, patternRepeatCount);
}

TEST_CASE("Disruptor Rx basic tests", "[Disruptor][Rx][basic]") {
    REQUIRE(test("ordinary"s, "a1 b1 c1 a2 b2 c2 a3 b3 c3"s, "a1 b1 c1; a2 b2 c2; a3 b3 c3"s, ""s, 1));
    REQUIRE(test("reordered", "a1 c1 b1 a2 b2 c2 a3 b3 c3", "a1 b1 c1; a2 b2 c2; a3 b3 c3", "", 1));
    REQUIRE(test("duplicate events", "a1 b1 c1 b1 a2 b2 c2 a2 a3 b3 c3 c3", "a1 b1 c1; a2 b2 c2; a3 b3 c3", "", 1));
    REQUIRE(test("interleaved", "a1 b1 a2 b2 c1 a3 b3 c2 c3", "a1 b1 c1; a2 b2 c2; a3 b3 c3", "", 1));
}

TEST_CASE("Disruptor Rx missing event tests", "[Disruptor][Rx][missing]") {
    REQUIRE(test("missing event", "a1 b1 a2 b2 c2 a3 b3 c3", "a2 b2 c2; a3 b3 c3", "a1 b1 X", 1));
    REQUIRE(test("missing event at end", "a1 b1 c1 a2 b2 c2 a3 b3", "a1 b1 c1; a2 b2 c2", "a3 b3 X", 1));
    REQUIRE(test("missing device", "a1 b1 a2 b2 a3 b3", "", "a1 b1 X; a2 b2 X; a3 b3 X", 1));
}

TEST_CASE("Disruptor Rx timeout tests", "[Disruptor][Rx][timeout]") {
    REQUIRE(test("late", "a1 b1 a2 b2 c2 a3 b3 c3 c1", "a1 b1 c1; a2 b2 c2; a3 b3 c3", "", 1));
    REQUIRE(test("timeout without event", "a1 b1 c1 a2 b2", "a1 b1 c1", "a2 b2 X", 1));
    REQUIRE(test("single event timeout", "a1 b1 P4", "", "a1 b1 X", 1));

    constexpr std::size_t patternRepeatCount = 5;
    REQUIRE(test("long queue", "a1 b1 c1 a2 b2", "a1 b1 c1; a1001 b1001 c1001; a2001 b2001 c2001; a3001 b3001 c3001; a4001 b4001 c4001", "a2 b2 X; a1002 b1002 X; a2002 b2002 X; a3002 b3002 X; a4002 b4002 X", patternRepeatCount));
    REQUIRE(test("simple broken long queue", "a1 b1", "", "a1 b1 X; a1001 b1001 X; a2001 b2001 X; a3001 b3001 X; a4001 b4001 X", patternRepeatCount));
}
