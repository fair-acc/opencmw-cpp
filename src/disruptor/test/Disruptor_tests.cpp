
#include <catch2/catch.hpp>

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
        std::int32_t value;
    };
    struct Next {};
    struct Stop {};
    struct Check {
        std::int32_t value;
    };

    std::variant<Next, ResetTo, Check, Stop> command;
};

class Publisher {
public:
    Publisher(const std::shared_ptr<RingBuffer<TestEvent>> &ringBuffer,
            std::int32_t                                    iterations)
        : m_ringBuffer(ringBuffer)
        , m_iterations(iterations) {}

    void run() {
        try {
            auto i = m_iterations;
            while (i != 0) {
                --i;
                auto  next      = m_ringBuffer->next();
                auto &testEvent = (*m_ringBuffer)[next];

                auto  value     = testEvent.sequence;
                if (i == 0) {
                    testEvent.command = TestEvent::Stop{};
                    std::cerr << "Stopping...\n";
                } else if (i % 100 == 1) {
                    testEvent.command = TestEvent::ResetTo{ i };
                } else if (i % 100 == 0) {
                    testEvent.command = TestEvent::Check{ i };
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

private:
    std::shared_ptr<RingBuffer<TestEvent>> m_ringBuffer;
    std::int32_t                           m_iterations;
};

std::vector<std::shared_ptr<Publisher>> makePublishers(size_t size,
        const std::shared_ptr<RingBuffer<TestEvent>>         &buffer,
        int                                                   messageCount) {
    std::vector<std::shared_ptr<Publisher>> result;

    for (auto i = 0u; i < size; i++) {
        result.push_back(std::make_shared<Publisher>(buffer, messageCount));
    }

    return result;
}

template<typename DisruptorPtr>
std::vector<std::shared_ptr<IEventHandler<TestEvent>>> makeHandlers(const DisruptorPtr &disruptor, size_t size) {
    std::vector<std::shared_ptr<IEventHandler<TestEvent>>> result;

    for (auto i = 0u; i < size; i++) {
        auto handler = makeEventHandler<TestEvent>(
                [value = 0, id = i](TestEvent &event, std::int64_t, bool) mutable {
                    std::visit(overloaded{
                                       [&](TestEvent::ResetTo resetTo) {
                                           value = resetTo.value;
                                           // REQUIRE(resetTo.value % 100 == 1);
                                           assert(resetTo.value % 100 == 1);
                                       },
                                       [&](TestEvent::Next) { value++; },
                                       [&](TestEvent::Stop) {
                                           value = -1;
                                           std::cerr << id << "< - Got stop message.\n";
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
    auto processorsCount = std::max(std::thread::hardware_concurrency() / 2, 1u);

    Disruptor<TestEvent, ProducerType::Multi, RoundRobinThreadAffinedTaskScheduler, BusySpinWaitStrategy>
            testDisruptor(
                    processorsCount, [] { return TestEvent(); }, 1 << 16);

    auto ringBuffer = testDisruptor->ringBuffer();
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
