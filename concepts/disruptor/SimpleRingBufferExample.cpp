#include <barrier>
#include <fmt/format.h>
#include <thread>

#include <disruptor/BlockingQueue.hpp>
#include <disruptor/RingBuffer.hpp>

#include <ThreadAffinity.hpp>

int main() {
    using namespace opencmw;
    using namespace opencmw::disruptor;

    auto        ringBuffer = std::make_shared<RingBuffer<int, 8192>>(ProducerType::Single, std::make_shared<BusySpinWaitStrategy>());

    const auto &poller     = ringBuffer->newPoller();
    // register poller sequence and poller -- w/o it's just a free idling RingBuffer
    ringBuffer->addGatingSequences({ poller->sequence() });
    fmt::print("buffer info: {}\n", (*ringBuffer));

    int  counter   = 0;
    auto fillEvent = [&counter](int &&eventData, std::int64_t sequence) noexcept {
        eventData = 42 + counter++;
        fmt::print("created new eventData: {} at sequence ID: {}\n", eventData, sequence);
    };
    ringBuffer->publishEvent(fillEvent);
    ringBuffer->publishEvent(fillEvent);

    auto polling = [](int &event, std::int64_t sequence, bool moreEvts) noexcept {
        fmt::print("new polling event: {} sequence ID: {} more?: {:5}", event, sequence, moreEvts);
        return false;
    };
    for (int i = 0; i < 4; i++) {
        PollState result = poller->poll(polling);
        fmt::print(" - poll result: {}\n", result);
    }
    fmt::print("done polling data\n\nmini performance test:\n");

    auto fillEventHandler = [&counter](int &&eventData, std::int64_t) noexcept {
        eventData = ++counter;
    };
    const unsigned long nLoop = 1'000'000;
    {
        counter                            = 0;
        std::chrono::time_point time_start = std::chrono::system_clock::now();
        for (auto i = 0UL; i < nLoop; i++) {
            ringBuffer->publishEvent(fillEventHandler);
            poller->poll([&counter](const int &event, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) {
                if (counter != event) {
                    fmt::print("event-counter mismatch {} {}\n", counter, event);
                }
                return true; // true: can handle more events, false: pause and requery (slower)
            });
        }
        const std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
        fmt::print("RingBuffer read-write performance (single-thread):  {} events in {:6.2f} ms -> {:.1E} ops/s\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count());
    }
    {
        counter                               = 0;
        std::chrono::time_point time_start    = std::chrono::system_clock::now();
        auto                    on_completion = [&time_start]() noexcept { time_start = std::chrono::system_clock::now(); };
        std::barrier            sync_point(2, on_completion);
        std::jthread            publisher([&sync_point, &publisher, &ringBuffer, &fillEventHandler]() {
            opencmw::thread::setThreadName("publisher", publisher);
            opencmw::thread::setThreadAffinity(std::array{ true, false }, publisher);
            sync_point.arrive_and_wait();
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
                   });

        int                     received = 0;
        using opencmw::disruptor::PollState;
        std::jthread consumer([&sync_point, &consumer, &received, &poller]() {
            opencmw::thread::setThreadName("consumer", consumer);
            opencmw::thread::setThreadAffinity(std::array{ false, true }, consumer);
            const auto receiver = [&received](const int &event, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) {
                received++;
                if (event > static_cast<int>(nLoop + 5)) {
                    fmt::print("event: {}\n", event);
                }
                return true; // true: can handle more events, false: pause and requery (slower)
            };
            sync_point.arrive_and_wait();
            while (received < static_cast<int>(nLoop)) {
                [[maybe_unused]] PollState pollState = poller->poll(receiver);
            }
            // fmt::print("consumer finished {}\n", received)
        });
        publisher.join();
        consumer.join();
        const std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
        fmt::print("RingBuffer read-write performance (two-threads):    {} events in {:6.2f} ms -> {:.1E} ops/s\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count());
    }
    {
        using namespace opencmw::disruptor;

        BlockingQueue<int> queue;

        // simple through-put test
        counter                               = 0;
        int                     received      = 0;
        std::chrono::time_point time_start    = std::chrono::system_clock::now();
        auto                    on_completion = [&time_start]() noexcept { time_start = std::chrono::system_clock::now(); };
        std::barrier            sync_point(2, on_completion);
        std::jthread            publisher([&sync_point, &queue, &counter, &publisher]() {
            opencmw::thread::setThreadName("publisher", publisher);
            opencmw::thread::setThreadAffinity(std::array{ true, false }, publisher);
            sync_point.arrive_and_wait();
            for (auto i = 0U; i < nLoop; i++) {
                ++counter;
                queue.push(counter);
            } });

        std::jthread            consumer([&sync_point, &consumer, &received, &queue]() {
            opencmw::thread::setThreadName("consumer", consumer);
            opencmw::thread::setThreadAffinity(std::array{ false, true }, consumer);
            sync_point.arrive_and_wait();
            while (received < static_cast<int>(nLoop)) {
                received = queue.pop();
            }
                   });
        publisher.join();
        consumer.join();
        std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
        fmt::print("BlockingQueue read-write performance (two-threads): {} events in {:6.2f} ms -> {:.1E} ops/s\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count());
    }
}
