#include <catch2/catch.hpp>

#include <cassert>

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

// A custom aggregation policy that has a time limit
// and groups events based on their IDs
template<std::size_t TimeLimit, template<typename...> typename Wrapper = std::type_identity>
struct TestAggregatorPolicy : rx::Aggreagate::Timed<TimeLimit, Wrapper> {
    using IdMatcherPolicyTag = std::true_type;
    std::size_t idForEvent(const auto &event) const {
        return event.index;
    }

    TestAggregatorPolicy()
        : rx::Aggreagate::Timed<TimeLimit, Wrapper>() {
    }
};
static_assert(rx::Aggreagate::IdMatcherPolicy<TestAggregatorPolicy<1>>);

struct TestEvent {
    char        device;
    std::size_t index;
    bool        isValid                                   = true;

    auto        operator<=>(const TestEvent &other) const = default;
};

using TestEvents       = std::vector<TestEvent>;
using AggregatedEvent  = std::tuple<TestEvent, TestEvent, TestEvent>;
using AggregatedEvents = std::vector<AggregatedEvent>;

std::ostream &operator<<(std::ostream &out, const TestEvent &event) {
    if (event.isValid) {
        return out << event.device << event.index;
    } else {
        return out << 'X';
    }
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

std::istream &operator>>(std::istream &in, TestEvent &event) {
    in >> event.device;
    if (event.device == 'X') {
        event.isValid = false;

    } else {
        event.isValid = true;
        in >> event.index;
    }
    return in;
}

template<typename... Events>
requires(std::is_same_v<Events, TestEvent> &&...)
        std::istream &
        operator>>(std::istream &in, std::tuple<Events...> &events) {
    rx::detail::tuple_for_each(events,
            [&in]<typename Index>(Index, auto &event) mutable {
                in >> event;
            });
    return in;
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
        operator>>(std::istream &in, std::vector<T> &events) {
    events.clear();
    T event;
    while (in >> event) {
        events.push_back(event);

        if constexpr (std::is_same_v<T, AggregatedEvent>) {
            char skipSemicolon;
            if (in >> skipSemicolon) {
                assert(skipSemicolon == ';');
            }
        }
    }
    return in;
}

class Publisher {
private:
    std::shared_ptr<RingBuffer<TestEvent>> m_ringBuffer;
    TestEvents                             m_events;
    std::size_t                            m_patternRepeat;

public:
    Publisher(const std::shared_ptr<RingBuffer<TestEvent>> &ringBuffer,
            TestEvents                                      events,
            std::size_t                                     patternRepeat)
        : m_ringBuffer(ringBuffer)
        , m_events(std::move(events))
        , m_patternRepeat(patternRepeat) {}

    void run() {
        const auto timeUnit = 100ms;
        try {
            for (std::size_t repeat = 0; repeat < m_patternRepeat; repeat++) {
                for (const auto &event : m_events) {
                    if (event.device == 'P') {
                        std::this_thread::sleep_for(event.index * timeUnit);
                    } else {
                        auto  next      = m_ringBuffer->next();
                        auto &testEvent = (*m_ringBuffer)[next];
                        testEvent       = event;
                        testEvent.index += repeat * 1000;
                        m_ringBuffer->publish(next);

                        std::this_thread::sleep_for(timeUnit);
                    }
                }
            }

            // Wait for all events to get propagated
            std::this_thread::sleep_for(10 * timeUnit);
        } catch (...) {
            failed = true;
        }
    }

    bool failed = false;
};

std::shared_ptr<Publisher> makePublisher(
        const std::shared_ptr<RingBuffer<TestEvent>> &buffer,
        const TestEvents                             &events,
        std::size_t                                   patternRepeat) {
    return std::make_shared<Publisher>(buffer, events, patternRepeat);
}

bool test(std::string name, const TestEvents &input, const AggregatedEvents &expectedResult, const AggregatedEvents &expectedTimedOut, std::size_t patternRepeat) {
    std::cerr << "TEST: " << name << '\n';
    auto processorsCount = std::max(std::thread::hardware_concurrency() / 2, 1u);

    using TestDisruptor  = Disruptor<TestEvent, ProducerType::Multi, RoundRobinThreadAffinedTaskScheduler, BusySpinWaitStrategy>;
    TestDisruptor testDisruptor(processorsCount, 1 << 16);

    auto          ringBuffer = testDisruptor->ringBuffer();
    testDisruptor->setDefaultExceptionHandler(std::make_shared<FatalExceptionHandler<TestEvent>>());

    // auto       handlers       = makeHandlers(testDisruptor, handlerCount);
    auto publisher = makePublisher(ringBuffer, input, patternRepeat);

    // Rx events from the Disruptor
    rx::Source<TestDisruptor> disruptorSource(testDisruptor);

    // Splitting stream
    auto equalTo = [](char device) {
        return [=](const TestEvent &event) { return event.device == device; };
    };
    auto             as        = disruptorSource.stream() | rxcpp::operators::filter(equalTo('a')) | rxcpp::operators::distinct_until_changed();
    auto             bs        = disruptorSource.stream() | rxcpp::operators::filter(equalTo('b')) | rxcpp::operators::distinct_until_changed();
    auto             cs        = disruptorSource.stream() | rxcpp::operators::filter(equalTo('c')) | rxcpp::operators::distinct_until_changed();

    auto             aggregate = rx::aggreagate<TestAggregatorPolicy<1000>>(as, bs, cs);

    AggregatedEvents result;
    AggregatedEvents timedOut;
    aggregate.stream().subscribe(
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

    {
        std::jthread thread([publisher] { publisher->run(); });
    }

    std::ranges::sort(result);
    std::ranges::sort(timedOut);
    const bool resultMatches  = result == expectedResult;
    const bool skippedMatches = timedOut == expectedTimedOut;

    if (!resultMatches) {
        std::cerr << "TEST FAILED: " << name << '\n';
        std::cerr << "-- input\n";
        for (const auto &event : input) {
            std::cerr << event << std::endl;
        }
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
        std::cerr << "TEST FAILED: " << name << '\n';
        std::cerr << "-- input\n";
        for (const auto &event : input) {
            std::cerr << event << std::endl;
        }
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

    return resultMatches;
}

bool test(std::string name, const std::string &input, const std::string &expectedResult, const std::string &expectedTimedOut, std::size_t patternRepeat) {
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

    return test(name, parsedInput, parsedExpectedResult, parsedExpectedTimedOut, patternRepeat);
}

TEST_CASE("Disruptor Rx basic tests", "[Disruptor][Rx][basic]") {
    test("ordinary"s, "a1 b1 c1 a2 b2 c2 a3 b3 c3"s, "a1 b1 c1; a2 b2 c2; a3 b3 c3"s, ""s, 1);
    test("reordered", "a1 c1 b1 a2 b2 c2 a3 b3 c3", "a1 b1 c1; a2 b2 c2; a3 b3 c3", "", 1);
    test("duplicate events", "a1 b1 c1 b1 a2 b2 c2 a2 a3 b3 c3 c3", "a1 b1 c1; a2 b2 c2; a3 b3 c3", "", 1);
    test("interleaved", "a1 b1 a2 b2 c1 a3 b3 c2 c3", "a1 b1 c1; a2 b2 c2; a3 b3 c3", "", 1);
}

TEST_CASE("Disruptor Rx missing event tests", "[Disruptor][Rx][missing]") {
    test("missing event", "a1 b1 a2 b2 c2 a3 b3 c3", "a2 b2 c2; a3 b3 c3", "1", 1);
    test("missing device", "a1 b1 a2 b2 a3 b3", "", "1 2 3", 1);
}

TEST_CASE("Disruptor Rx timeout tests", "[Disruptor][Rx][timeout]") {
    test("late", "a1 b1 a2 b2 c2 a3 b3 c3 c1", "a1 b1 c1; a2 b2 c2; a3 b3 c3", "", 1);
    test("timeout without event", "a1 b1 c1 a2 b2", "a1 b1 c1", "2", 1);
    test("long queue", "a1 b1 c1 a2 b2", "a1 b1 c1; a1001 b1001 c1001; a2001 b2001 c2001; a3001 b3001 c3001; a4001 b4001 c4001", "", 5);
    test("simple broken long queue", "a1 b1", "", "", 5);
    test("single event timeout", "a1 b1 P4", "", "1", 1);
}
