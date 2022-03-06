#include <catch2/catch.hpp>

#include <cassert>
#include <utility>
#include <variant>

#include <disruptor/Disruptor.hpp>
#include <disruptor/RingBuffer.hpp>
#include <disruptor/RoundRobinThreadAffinedTaskScheduler.hpp>
#include <disruptor/WaitStrategy.hpp>

using namespace opencmw::disruptor;

template<typename... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

struct TestEvent {
    std::int32_t sequence = 0;
    std::int32_t cid      = 0;
    std::int32_t pid      = 0;

    struct ResetTo {
        int value;
    };
    struct Next {};
    struct Stop {};
    struct Check {
        int value;
    };

    std::variant<Next, ResetTo, Check, Stop> command;
};

template<std::size_t SIZE>
class Publisher {
private:
    std::shared_ptr<RingBuffer<TestEvent, SIZE>> m_ringBuffer;
    int                                    m_iterations;

public:
    Publisher(std::shared_ptr<RingBuffer<TestEvent, SIZE>> ringBuffer, int iterations)
        : m_ringBuffer(std::move(ringBuffer))
        , m_iterations(iterations) {}

    void run() {
        try {
            static constexpr int groupSize = 100;
            auto                 iteration = m_iterations;
            while (iteration != 0) {
                --iteration;
                auto  next      = m_ringBuffer->next();
                auto &testEvent = (*m_ringBuffer)[next];

                auto  value     = testEvent.sequence;
                if (iteration == 0) {
                    testEvent.command = TestEvent::Stop{};
                    std::cerr << "Stopping...\n";
                } else if (iteration % groupSize == 1) {
                    testEvent.command = TestEvent::ResetTo{ iteration };
                } else if (iteration % groupSize == 0) {
                    testEvent.command = TestEvent::Check{ iteration };
                } else {
                    testEvent.command = TestEvent::Next{};
                }

                testEvent.sequence = value;
                testEvent.cid      = value;
                testEvent.pid      = value;

                m_ringBuffer->publish(next);
            }
        } catch (...) {
            failed = true;
        }
    }

    bool failed = false;
};

template<size_t SIZE>
std::vector<std::shared_ptr<Publisher<SIZE>>> makePublishers(size_t size,
        const std::shared_ptr<RingBuffer<TestEvent, SIZE>>         &buffer,
        int                                                         messageCount) {
    std::vector<std::shared_ptr<Publisher<SIZE>>> result;

    result.reserve(size);
    for (auto i = 0u; i < size; i++) {
        result.push_back(std::make_shared<Publisher<SIZE>>(buffer, messageCount));
    }

    return result;
}

template<typename DisruptorPtr>
std::vector<std::shared_ptr<IEventHandler<TestEvent>>> makeHandlers(const DisruptorPtr &disruptor, size_t size) {
    std::vector<std::shared_ptr<IEventHandler<TestEvent>>> result;

    for (auto handlerIndex = 0U; handlerIndex < size; handlerIndex++) {
        auto handler = makeEventHandler<TestEvent>(
                [value = 0, handlerIndex](TestEvent &event, std::int64_t, bool) mutable {
                    std::visit(overloaded{
                                       [&](TestEvent::ResetTo resetTo) {
                                           value = resetTo.value;
                                           // REQUIRE(resetTo.value % 100 == 1);
                                           assert(resetTo.value % 100 == 1);
                                       },
                                       [&](TestEvent::Next) { value++; },
                                       [&](TestEvent::Stop) {
                                           value = -1;
                                           std::cerr << handlerIndex << "< - Got stop message.\n";
                                       },
                                       [&]([[maybe_unused]] TestEvent::Check check) {
                                           assert(check.value % 100 == 0);
                                           // REQUIRE(check.value % 100 == 0);
                                       } },
                            event.command);
                });
        disruptor->handleEventsWith(handler);
        result.push_back(handler);
    }

    return result;
}

TEST_CASE("Disruptor stress test", "[Disruptor]") {
    auto                                                                                                              processorsCount = std::max(std::thread::hardware_concurrency() / 2, 1U);

    constexpr auto                                                                                                    bufferSize      = 1 << 16;
    Disruptor<TestEvent, bufferSize, ProducerType::Multi, RoundRobinThreadAffinedTaskScheduler, BusySpinWaitStrategy> testDisruptor(processorsCount);

    auto                                                                                                              ringBuffer = testDisruptor->ringBuffer();
    testDisruptor->setDefaultExceptionHandler(std::make_shared<FatalExceptionHandler<TestEvent>>());

    const auto iterations     = 200000;

    auto       publisherCount = processorsCount;
    auto       handlerCount   = processorsCount;

    auto       handlers       = makeHandlers(testDisruptor, handlerCount);
    auto       publishers     = makePublishers(publisherCount, ringBuffer, iterations);

    testDisruptor->start();

    const auto time_start = std::chrono::system_clock::now();
    {
        std::vector<std::jthread> threads;
        threads.reserve(publishers.size());
        for (auto &&publisher : publishers) {
            threads.emplace_back([publisher] { publisher->run(); });
        }
        std::cerr << "Waiting for publisher threads to finish...\n";
    }
    std::cerr << "Joined threads.\n";
    const auto                                      time_end     = std::chrono::system_clock::now();
    std::chrono::duration<double, std::ratio<1, 1>> time_elapsed = time_end - time_start;
    std::cout << "seconds to finish: " << time_elapsed.count() << std::endl;
    const auto msgPerSeconds = iterations * processorsCount / time_elapsed.count();
    std::cout << "msgs per second: " << msgPerSeconds << std::endl;
}
