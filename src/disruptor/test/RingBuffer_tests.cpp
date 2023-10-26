#include <catch2/catch.hpp>

#include <barrier>
#include <fmt/format.h>
#include <iostream>
#include <queue>

#include <ThreadAffinity.hpp>

#include "disruptor/BlockingQueue.hpp"
#include "disruptor/RingBuffer.hpp"
#include "disruptor/Sequence.hpp"
#include "disruptor/WaitStrategy.hpp"

TEST_CASE("RingBuffer basic tests", "[Disruptor]") {
    using namespace opencmw;
    using namespace opencmw::disruptor;

    REQUIRE_NOTHROW(newRingBuffer<int, 1024, BusySpinWaitStrategy>());

    auto ringBuffer = std::make_shared<RingBuffer<int, 8192, BusySpinWaitStrategy, SingleThreadedStrategy>>();
    REQUIRE(ringBuffer->bufferSize() == 8192);
    REQUIRE(ringBuffer->hasAvailableCapacity(8192));
    REQUIRE(!ringBuffer->hasAvailableCapacity(8193));
    REQUIRE_NOTHROW(ringBuffer->getMinimumGatingSequence());
    REQUIRE(ringBuffer->getMinimumGatingSequence() == -1);

    auto pollerSequence = std::make_shared<Sequence>();
    REQUIRE_NOTHROW(ringBuffer->addGatingSequences({ pollerSequence }));
    REQUIRE(ringBuffer->removeGatingSequence(pollerSequence));
    const auto &poller = ringBuffer->newPoller();
    // register poller sequence and poller -- w/o it's just a free idling RingBuffer
    REQUIRE_NOTHROW(ringBuffer->addGatingSequences({ poller->sequence() }));

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
    const auto pollingCallback = [](const int &event, std::int64_t sequenceID, bool /*endOfBatch*/) noexcept {
        REQUIRE(event == sequenceID);
        return false;
    };
    for (int i = 0; i < 4; i++) {
        PollState result = poller->poll(pollingCallback);
        REQUIRE(result == (i < 2 ? PollState::Processing : PollState::Idle));
    }
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());
    const auto pollAllParallel = [](const int & /*event*/, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) { /* poll all in one go */ };
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
    REQUIRE_NOTHROW(poller->poll(pollAllParallel));
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());

    REQUIRE((ringBuffer->cursor() + 1) == ringBuffer->next());
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize() - 1);
    REQUIRE_NOTHROW(ringBuffer->publish(ringBuffer->cursor() + 1));
    REQUIRE_NOTHROW(poller->poll(pollAllParallel));
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());

    REQUIRE_NOTHROW(poller->poll(pollAllParallel));
    REQUIRE_NOTHROW(ringBuffer->publishEvent([](int &&, std::int64_t) { throw std::exception(); }));
    counter = 0;
    REQUIRE_NOTHROW(poller->poll([&counter](const int & /*event*/, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) { counter++; /* no return -> defaulting true*/ }));
    REQUIRE(counter == 1);

    REQUIRE_NOTHROW(ringBuffer->publishEvent([](int &&, std::int64_t) { throw std::exception(); }));
    REQUIRE_NOTHROW(ringBuffer->tryPublishEvent([](int &&, std::int64_t) { throw std::exception(); }));
    REQUIRE_NOTHROW(poller->poll(pollAllParallel));
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());

    REQUIRE((ringBuffer->cursor() + 1) == ringBuffer->next());
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize() - 1);
    REQUIRE((ringBuffer->cursor() + 3) == ringBuffer->next(2));
    REQUIRE_NOTHROW(ringBuffer->publish(ringBuffer->cursor()));

    REQUIRE_NOTHROW(ringBuffer->publishEvent([](int &&, std::int64_t) { /* empty notify */ }));
    REQUIRE_THROWS(poller->poll([](const int & /*event*/, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) { throw std::exception(); }));

    std::stringstream ss;
    ss << ringBuffer;
    REQUIRE(ss.str().size() != 0);
}

TEST_CASE("RingBuffer low-level operation", "[Disruptor]") {
    using namespace opencmw;
    using namespace opencmw::disruptor;

    auto    ringBuffer = std::make_shared<RingBuffer<int, 8192, BusySpinWaitStrategy, SingleThreadedStrategy>>();

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

    auto        ringBuffer = std::make_shared<RingBuffer<int, 8192, BusySpinWaitStrategy, MultiThreadedStrategy>>();

    const auto &poller     = ringBuffer->newPoller();
    // register poller sequence and poller -- w/o it's just a free idling RingBuffer
    ringBuffer->addGatingSequences({ poller->sequence() });

    // simple through-put test
    int        counter          = 0;
    const auto fillEventHandler = [&counter](int &&eventData, std::int64_t) noexcept {
        eventData = ++counter;
    };
    const auto pollEventHandler = [&counter](const int &event, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) {
        REQUIRE(counter == event);
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
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());
    fmt::print("RingBuffer read-write performance (single-thread):    {} events in {:6.2f} ms -> {:.1E} ops/s\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count());
}

template<std::size_t SIZE, template<size_t, typename> typename CLAIM_STRATEGY>
void ringBufferPollerTest() {
    using namespace opencmw;
    using namespace opencmw::disruptor;

    auto        ringBuffer = std::make_shared<RingBuffer<int, SIZE, BusySpinWaitStrategy, CLAIM_STRATEGY>>();

    const auto &poller     = ringBuffer->newPoller();
    // register poller sequence and poller -- w/o it's just a free idling RingBuffer
    ringBuffer->addGatingSequences({ poller->sequence() });

    // simple through-put test
    int        counter          = 0;
    int        received         = 0;
    int        waitingForReader = 0;
    const auto fillEventHandler = [&counter](int &&eventData, std::int64_t) noexcept {
        eventData = ++counter;
    };

    const int               nLoop         = 1'000'000;
    std::chrono::time_point time_start    = std::chrono::system_clock::now();
    auto                    on_completion = [&time_start]() noexcept { time_start = std::chrono::system_clock::now(); };
    std::barrier            sync_point(2, on_completion);
    std::thread             publisher([&sync_point, &publisher, &ringBuffer, &waitingForReader, &fillEventHandler]() {
        opencmw::thread::setThreadName("publisher", publisher);
        opencmw::thread::setThreadAffinity(std::array{ true, false}, publisher);
        sync_point.arrive_and_wait();
        for (int i = 0; i < nLoop; i++) {
            bool once = true;
            while (!ringBuffer->tryPublishEvent(fillEventHandler)) {
                std::this_thread::yield();
                if (once) {
                    waitingForReader++;
                    once = false;
                }
            }
        } });

    using opencmw::disruptor::PollState;
    std::thread consumer([&sync_point, &consumer, &received, &poller]() {
        opencmw::thread::setThreadName("consumer", consumer);
        opencmw::thread::setThreadAffinity(std::array{ false, true }, consumer);
        const auto pollEventHandler = [&received](const int &event, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) {
            received = event;
            return true;
        };
        sync_point.arrive_and_wait();
        while (received < nLoop) {
            if (poller->poll(pollEventHandler) != PollState::Processing) {
                std::this_thread::yield();
            }
        }
    });
    publisher.join();
    consumer.join();
    std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
    REQUIRE(counter == nLoop);
    REQUIRE(received == nLoop);
    REQUIRE(ringBuffer->getRemainingCapacity() == ringBuffer->bufferSize());
    if (is_same_template<CLAIM_STRATEGY<SIZE, BusySpinWaitStrategy>, SingleThreadedStrategy<SIZE, BusySpinWaitStrategy>>::value) {
        fmt::print("RingBuffer read-write performance (two-threads,SPSC): {} events in {:6.2f} ms -> {:.1E} ops/s (waitingForReader: {})\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count(), waitingForReader);
    } else {
        fmt::print("RingBuffer read-write performance (two-threads,MPSC): {} events in {:6.2f} ms -> {:.1E} ops/s (waitingForReader: {})\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count(), waitingForReader);
    }
}

TEST_CASE("RingBuffer two-thread poller SPSC", "[Disruptor]") {
    using namespace opencmw::disruptor;
    ringBufferPollerTest<8192, SingleThreadedStrategy>();
}

TEST_CASE("RingBuffer two-thread poller MPSC", "[Disruptor]") {
    using namespace opencmw::disruptor;
    ringBufferPollerTest<8192, MultiThreadedStrategy>();
}

TEST_CASE("BlockingQueue two-thread poller (classic)", "[Disruptor]") {
    using namespace opencmw::disruptor;

    BlockingQueue<int> queue;

    // simple through-put test
    int                     counter       = 0;
    int                     received      = 0;
    const int               nLoop         = 1'000'000;
    std::chrono::time_point time_start    = std::chrono::system_clock::now();
    auto                    on_completion = [&time_start]() noexcept { time_start = std::chrono::system_clock::now(); };
    std::barrier            sync_point(2, on_completion);
    std::thread             publisher([&sync_point, &queue, &counter, &publisher]() {
        opencmw::thread::setThreadName("publisher", publisher);
        opencmw::thread::setThreadAffinity(std::array{ true, false }, publisher);
        sync_point.arrive_and_wait();
        for (int i = 0; i < nLoop; i++) {
            ++counter;
            queue.push(counter);
        } });

    using opencmw::disruptor::PollState;
    std::thread consumer([&sync_point, &consumer, &received, &queue]() {
        opencmw::thread::setThreadName("consumer", consumer);
        opencmw::thread::setThreadAffinity(std::array{ false, true }, consumer);
        sync_point.arrive_and_wait();
        while (received < nLoop) {
            received = queue.pop();
        }
    });
    publisher.join();
    consumer.join();
    std::chrono::duration<double, std::milli> time_elapsed = std::chrono::system_clock::now() - time_start;
    REQUIRE(counter == nLoop);
    REQUIRE(received == nLoop);
    fmt::print("BlockingQueue read-write performance (two-threads):   {} events in {:6.2f} ms -> {:.1E} ops/s\n", nLoop, time_elapsed.count(), 1e3 * nLoop / time_elapsed.count());
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
