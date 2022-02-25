#include <fmt/format.h>
#include <queue>

#include <disruptor/ISequence.hpp>
#include <disruptor/RingBuffer.hpp>

#include <ThreadAffinity.hpp>

namespace opencmw::alt::detail {

template<class T>
class ThreadSafeQueue { // not production code -- just meant for performance comparison
    std::queue<T>           queue;
    mutable std::mutex      mutex;
    std::condition_variable condition;

public:
    void enqueue(T t) {
        std::lock_guard lock(mutex);
        queue.push(t);
        condition.notify_one();
    }

    T dequeue() {
        std::unique_lock lock(mutex);
        condition.wait(lock, [this]() { return !queue.empty(); });
        T val = queue.front();
        queue.pop();
        return val;
    }
};

} // namespace opencmw::alt::detail

int main() {
    using namespace opencmw;
    using namespace opencmw::disruptor;

    auto ringBuffer = std::make_shared<RingBuffer<int, 8192>>(ProducerType::Single, std::make_shared<BusySpinWaitStrategy>());

    // register poller sequence and poller -- w/o it's just a free idling RingBuffer
    auto pollerSequence = std::make_shared<Sequence>();
    ringBuffer->addGatingSequences({ pollerSequence });
    const std::shared_ptr<opencmw::disruptor::EventPoller<int, 8192>> &poller = ringBuffer->newPoller();

    ringBuffer->getSequencer().writeDescriptionTo(std::cout);
    std::cout << std::endl;
    fmt::print("buffer info2: {}\n", (*ringBuffer));

    int  counter   = 0;
    auto fillEvent = [&counter](int &&eventData, std::int64_t sequence) noexcept {
        eventData = 42 + counter++;
        fmt::print("created new eventData: {} at sequence ID: {}\n", eventData, sequence);
    };
    ringBuffer->publishEvent(fillEvent);
    ringBuffer->publishEvent(fillEvent);

    auto polling = [&pollerSequence](int &event, std::int64_t sequence, bool moreEvts) noexcept {
        fmt::print("new polling event: {} sequence ID: {} more?: {:5}", event, sequence, moreEvts);
        pollerSequence->setValue(sequence);
        return false;
    };
    for (int i = 0; i < 4; i++) {
        PollState result = poller->poll(polling);
        fmt::print(" - poll result: {}\n", result);
    }
    fmt::print("done polling data\n");

    auto fillEventHandler = [&counter](int &&eventData, std::int64_t) noexcept {
        eventData = ++counter;
    };
    const unsigned long nLoop = 10'000'000;
    {
        counter                                  = 0;
        const std::chrono::time_point time_start = std::chrono::system_clock::now();
        for (auto i = 0UL; i < nLoop; i++) {
            ringBuffer->publishEvent(fillEventHandler);
            poller->poll([&counter, &pollerSequence](const int &event, std::int64_t nextSequence, bool /*moreEvts*/) {
                if (counter != event) {
                    fmt::print("event-counter mismatch {} {}\n", counter, event);
                }
                pollerSequence->setValue(nextSequence);
                return false;
            });
        }
        const std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
        fmt::print("RingBuffer read-write performance (single-thread): {} events in {:3.2} ms -> {:.1E} ops/s\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count());
    }
    {
        counter                                  = 0;
        const std::chrono::time_point time_start = std::chrono::system_clock::now();
        std::jthread                  publisher([&publisher, &ringBuffer, &fillEventHandler]() {
            opencmw::thread::setThreadName("publisher", publisher);
            for (auto i = 0U; i < nLoop; i++) {
                bool once = true;
                while (!ringBuffer->tryPublishEvent(fillEventHandler)) {
                    std::this_thread::yield();
                    if (once) {
                        fmt::print("buffer full at event {}\n", i);
                        once = false;
                    }
                }
            }
            // fmt::print("publisher finished {} ringBuffer: {} capacity {}\n", counter, ringBuffer->cursor(), ringBuffer->getRemainingCapacity())
                         });

        int                           received = 0;
        using opencmw::disruptor::PollState;
        std::jthread consumer([&consumer, &received, &pollerSequence, &poller]() {
            opencmw::thread::setThreadName("consumer", consumer);
            const auto receiver = [&received, &pollerSequence](const int &event, std::int64_t nextSequence, bool /*moreEvts*/) {
                received++;
                if (event > static_cast<int>(nLoop + 5)) {
                    fmt::print("event: {}\n", event);
                }
                pollerSequence->setValue(nextSequence);
                return true;
            };
            while (received < static_cast<int>(nLoop)) {
                [[maybe_unused]] PollState pollState = poller->poll(receiver);
            }
            // fmt::print("consumer finished {}\n", received)
        });
        publisher.join();
        consumer.join();
        const std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
        fmt::print("RingBuffer read-write performance (two-threads): {} events in {:3.2} ms -> {:.1E} ops/s\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count());
    }
    {
        using namespace opencmw::alt::detail;

        ThreadSafeQueue<int> queue;

        // simple through-put test
        counter                                  = 0;
        int                           received   = 0;
        const std::chrono::time_point time_start = std::chrono::system_clock::now();
        std::jthread                  publisher([&queue, &counter, &publisher]() {
            opencmw::thread::setThreadName("publisher", publisher);
            for (auto i = 0U; i < nLoop; i++) {
                queue.enqueue(++counter);
            } });

        using opencmw::disruptor::PollState;
        std::jthread consumer([&consumer, &received, &queue]() {
            opencmw::thread::setThreadName("consumer", consumer);

            while (received < static_cast<int>(nLoop)) {
                received = queue.dequeue();
            }
        });
        publisher.join();
        consumer.join();
        std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
        fmt::print("RingBuffer read-write performance (two-threads, classic): {} events in {:3.2} ms -> {:.1E} ops/s\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count());
    }
}
