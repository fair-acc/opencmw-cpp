#include <catch2/catch.hpp>

#include <fmt/format.h>
#include <iostream>
#include <queue>

#include <ThreadAffinity.hpp>

#include "disruptor/RingBuffer.hpp"
#include "disruptor/Sequence.hpp"
#include "disruptor/WaitStrategy.hpp"

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

TEST_CASE("RingBuffer basic tests", "[Disruptor]") {
    using namespace opencmw;
    using namespace opencmw::disruptor;

    auto ringBuffer = std::make_shared<RingBuffer<int, 8192>>(ProducerType::Single, std::make_shared<disruptor::BusySpinWaitStrategy>());
    REQUIRE(ringBuffer->bufferSize() == 8192);
    REQUIRE(ringBuffer->getSequencer().bufferSize() == 8192);
    REQUIRE(ringBuffer->hasAvailableCapacity(8192));
    REQUIRE(!ringBuffer->hasAvailableCapacity(8193));
    REQUIRE(ringBuffer->getMinimumGatingSequence() == -1);

    // register poller sequence and poller -- w/o it's just a free idling RingBuffer
    auto pollerSequence = std::make_shared<Sequence>();
    REQUIRE_NOTHROW(ringBuffer->addGatingSequences({ pollerSequence }));
    REQUIRE_NOTHROW(ringBuffer->removeGatingSequence({ pollerSequence }));
    REQUIRE_NOTHROW(ringBuffer->addGatingSequences({ pollerSequence }));
    const std::shared_ptr<opencmw::disruptor::EventPoller<int, 8192>> &poller = ringBuffer->newPoller();

    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());
    REQUIRE(ringBuffer->cursor() == -1);
    int        counter     = 0;
    const auto notifyEvent = [&counter](int &&eventData, std::int64_t sequence) noexcept {
        eventData = static_cast<int>(sequence);
        counter++;
    };
    REQUIRE_NOTHROW(ringBuffer->publishEvent(notifyEvent));
    REQUIRE(counter == 1);
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize() - 1);
    REQUIRE(ringBuffer->cursor() == 0);
    REQUIRE_NOTHROW(ringBuffer->publishEvent(notifyEvent));
    REQUIRE(counter == 2);
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize() - 2);
    REQUIRE(ringBuffer->cursor() == 1);

    // test simple poller
    const auto pollingCallback = [&pollerSequence](const int &event, std::int64_t sequence, bool /*moreEvts*/) noexcept {
        REQUIRE(event == sequence);
        pollerSequence->setValue(sequence);
        return false;
    };
    for (int i = 0; i < 4; i++) {
        PollState result = poller->poll(pollingCallback);
        REQUIRE(result == (i < 2 ? PollState::Processing : PollState::Idle));
    }
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());
    const auto pollAllParallel = [&pollerSequence](const int &, std::int64_t sequence, bool) {
        pollerSequence->setValue(sequence);
        return true; }; // poll all in one go.
    REQUIRE_NOTHROW(poller->poll(pollAllParallel));
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());
    for (int i = 0; i < ringBuffer->bufferSize() + 2; i++) {
        if (i < ringBuffer->bufferSize()) {
            REQUIRE(ringBuffer->tryPublishEvent(notifyEvent));
        } else {
            REQUIRE(!ringBuffer->tryPublishEvent(notifyEvent));
        }
    }
    REQUIRE_NOTHROW(poller->poll(pollAllParallel));
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());

    REQUIRE_NOTHROW(ringBuffer->publishEvent([](int &&, std::int64_t) { throw std::exception(); }));
    REQUIRE_NOTHROW(ringBuffer->tryPublishEvent([](int &&, std::int64_t) { throw std::exception(); }));
    //    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());
    //
    //    REQUIRE((ringBuffer->cursor() + 1) == ringBuffer->next());
    //    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize() - 1);
    //    REQUIRE((ringBuffer->cursor() + 3) == ringBuffer->next(2));
    //    REQUIRE_NOTHROW(ringBuffer->publish(ringBuffer->cursor()));
}

TEST_CASE("RingBuffer low-level operation", "[Disruptor]") {
    using namespace opencmw;
    using namespace opencmw::disruptor;

    auto    ringBuffer = std::make_shared<RingBuffer<int, 8192>>(ProducerType::Single, std::make_shared<disruptor::BusySpinWaitStrategy>());

    int64_t sequenceID = ringBuffer->next();
    REQUIRE(sequenceID == 0);
    REQUIRE_NOTHROW((*ringBuffer)[sequenceID] == 5);
    REQUIRE_NOTHROW(ringBuffer->publish(sequenceID));
    REQUIRE(ringBuffer->isPublished(sequenceID));

    sequenceID = ringBuffer->tryNext();
    REQUIRE(sequenceID == 1);
    REQUIRE_NOTHROW((*ringBuffer)[sequenceID] == 5);
    REQUIRE_NOTHROW(ringBuffer->publish(sequenceID));
    REQUIRE(ringBuffer->isPublished(sequenceID));
}

TEST_CASE("RingBuffer single-thread poller", "[Disruptor]") {
    using namespace opencmw;
    using namespace opencmw::disruptor;

    using namespace opencmw;
    using namespace opencmw::disruptor;

    auto ringBuffer = std::make_shared<RingBuffer<int, 8192>>(ProducerType::Multi, std::make_shared<disruptor::BusySpinWaitStrategy>());

    // register poller sequence and poller -- w/o it's just a free idling RingBuffer
    auto pollerSequence = std::make_shared<Sequence>();
    ringBuffer->addGatingSequences({ pollerSequence });
    const std::shared_ptr<opencmw::disruptor::EventPoller<int, 8192>> &poller = ringBuffer->newPoller();

    // simple through-put test
    int        counter          = 0;
    const auto fillEventHandler = [&counter](int &&eventData, std::int64_t) noexcept {
        eventData = ++counter;
    };
    const auto pollEventHandler = [&counter, &pollerSequence](const int &event, std::int64_t nextSequence, bool /*moreEvts*/) {
        REQUIRE(counter == event);
        pollerSequence->setValue(nextSequence);
        return false;
    };

    const unsigned long           nLoop      = 1'000'000;
    const std::chrono::time_point time_start = std::chrono::system_clock::now();
    for (auto i = 0UL; i < nLoop; i++) {
        ringBuffer->publishEvent(fillEventHandler);
        poller->poll(pollEventHandler);
    }
    std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
    REQUIRE(counter == nLoop);
    REQUIRE(ringBuffer->getSequencer().getRemainingCapacity() == ringBuffer->bufferSize());
    fmt::print("RingBuffer read-write performance (single-thread): {} events in {} ms -> {:.1E} ops/s\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count());
}

TEST_CASE("RingBuffer two-thread poller", "[Disruptor]") {
    using namespace opencmw;
    using namespace opencmw::disruptor;

    using namespace opencmw;
    using namespace opencmw::disruptor;

    auto ringBuffer = std::make_shared<RingBuffer<int, 8192>>(ProducerType::Multi, std::make_shared<disruptor::BusySpinWaitStrategy>());

    // register poller sequence and poller -- w/o it's just a free idling RingBuffer
    auto pollerSequence = std::make_shared<Sequence>();
    ringBuffer->addGatingSequences({ pollerSequence });
    const std::shared_ptr<opencmw::disruptor::EventPoller<int, 8192>> &poller = ringBuffer->newPoller();

    // simple through-put test
    int        counter          = 0;
    int        received         = 0;
    int        waitingForReader = 0;
    const auto fillEventHandler = [&counter](int &&eventData, std::int64_t) noexcept {
        eventData = ++counter;
    };
    const auto pollEventHandler = [&received, &pollerSequence](const int &event, std::int64_t nextSequence, bool /*moreEvts*/) {
        received++;
        REQUIRE(received == event);

        pollerSequence->setValue(nextSequence);
        return false;
    };

    const int                     nLoop      = 100'000;
    const std::chrono::time_point time_start = std::chrono::system_clock::now();
    std::jthread                  publisher([&publisher, &ringBuffer, &waitingForReader, &fillEventHandler]() {
        opencmw::thread::setThreadName("publisher", publisher);
        for (int i = 0; i < nLoop; i++) {
            while (!ringBuffer->tryPublishEvent(fillEventHandler)) {
                std::this_thread::yield();
                waitingForReader++;
            }
        } });

    using opencmw::disruptor::PollState;
    std::jthread consumer([&consumer, &received, &poller, &pollEventHandler]() {
        opencmw::thread::setThreadName("consumer", consumer);

        while (received < nLoop) {
            [[maybe_unused]] PollState pollState = poller->poll(pollEventHandler);
        }
    });
    publisher.join();
    consumer.join();
    std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
    REQUIRE(counter == nLoop);
    REQUIRE(received == nLoop);
    REQUIRE(ringBuffer->getSequencer().getRemainingCapacity() == ringBuffer->bufferSize());
    fmt::print("RingBuffer read-write performance (two-threads): {} events in {:2.2} ms -> {:.1E} ops/s (waitingForReader: {})\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count(), waitingForReader);
}

TEST_CASE("RingBuffer two-thread poller (classic)", "[Disruptor]") {
    using namespace opencmw::alt::detail;

    ThreadSafeQueue<int> queue;

    // simple through-put test
    int                           counter    = 0;
    int                           received   = 0;

    const int                     nLoop      = 1'000'000;
    const std::chrono::time_point time_start = std::chrono::system_clock::now();
    std::jthread                  publisher([&queue, &counter, &publisher]() {
        opencmw::thread::setThreadName("publisher", publisher);
        for (int i = 0; i < nLoop; i++) {
            queue.enqueue(++counter);
        } });

    using opencmw::disruptor::PollState;
    std::jthread consumer([&consumer, &received, &queue]() {
        opencmw::thread::setThreadName("consumer", consumer);

        while (received < nLoop) {
            received = queue.dequeue();
        }
    });
    publisher.join();
    consumer.join();
    std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
    REQUIRE(counter == nLoop);
    REQUIRE(received == nLoop);
    fmt::print("RingBuffer read-write performance (two-threads, classic): {} events in {:2.2} ms -> {:.1E} ops/s\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count());
}

TEST_CASE("RingBuffer helper", "[Disruptor]") {
    using namespace opencmw;
    using namespace opencmw::disruptor;

    REQUIRE(!fmt::format("{}", PollState::Idle).empty());
    REQUIRE(!fmt::format("{}", PollState::Processing).empty());
    REQUIRE(!fmt::format("{}", PollState::Gating).empty());
    REQUIRE(!fmt::format("{}", PollState::UNKNOWN).empty());
    const auto testStream = [](PollState state) {
        std::stringstream stream;
        stream << state;
        return stream.str();
    };
    REQUIRE(!testStream(PollState::Idle).empty());
    REQUIRE(!testStream(PollState::Processing).empty());
    REQUIRE(!testStream(PollState::Gating).empty());
    REQUIRE(!testStream(PollState::UNKNOWN).empty());
}