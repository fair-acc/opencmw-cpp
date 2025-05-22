#include <array>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <disruptor/BlockingQueue.hpp>
#include <disruptor/RingBuffer.hpp>
#include <ThreadAffinity.hpp>

static constexpr std::uint32_t maxCores   = 24U;
static const std::size_t       maxThreads = std::min(std::thread::hardware_concurrency(), maxCores);

double                         testBlockingQueue(const std::uint64_t nLoop, const std::uint64_t nProducer, const std::uint64_t nConsumer, bool printCheck = true) {
    using namespace opencmw;
    using namespace opencmw::disruptor;
    assert(isPower2(nLoop) && nLoop > 1);
    assert(nProducer > 0);
    assert(nConsumer > 0);
    std::atomic<std::uint64_t>           nProduced            = 0;
    std::atomic<std::uint64_t>           nConsumed            = 0;
    [[maybe_unused]] const std::uint64_t nEventsProducer      = nLoop / nProducer;
    [[maybe_unused]] const std::uint64_t nEventsTotalProducer = nEventsProducer * nProducer;
    [[maybe_unused]] const std::uint64_t nEventsConsumer      = nEventsTotalProducer;

    // init BlockingQueue -- need one per consumer
    std::vector<BlockingQueue<std::uint64_t>> blockingQueue(nConsumer);
    // start/stop barriers
    auto                                      time_start = std::chrono::system_clock::now();
    std::chrono::duration<double, std::milli> time_elapsed;
    auto                                      on_completion1 = [&time_start]() noexcept { time_start = std::chrono::system_clock::now(); };
    auto                                      on_completion2 = [&time_start, &time_elapsed]() noexcept { time_elapsed = std::chrono::system_clock::now() - time_start; };
    std::barrier                              startMark(static_cast<long>(nProducer + nConsumer), on_completion1);
    std::barrier                              stopMark(static_cast<long>(nProducer + nConsumer), on_completion2);
    // init producers
    std::vector<std::jthread> producers;
    for (auto i = 0U; i < nProducer; i++) {
        producers.emplace_back([&i, &nEventsProducer, &nProduced, &blockingQueue, &startMark, &stopMark]() {
            opencmw::thread::setThreadName(std::format("publisher{}", i));
            std::array<bool, maxCores> affinity{};
            affinity[(2 * i) % maxThreads] = true;
            opencmw::thread::setThreadAffinity(affinity);
            std::this_thread::yield();
            startMark.arrive_and_wait();
            for (std::uint64_t e = 0; e < nEventsProducer; e++) {
                const auto event = nProduced.fetch_add(1, std::memory_order_relaxed) + 1;
                std::ranges::for_each(blockingQueue, [&event](auto &queue) { queue.push(event); });
            }
            stopMark.arrive_and_wait();
        });
    }

    // init consumers
    std::vector<std::jthread> consumers;
    for (auto i = 0U; i < nConsumer; i++) {
        auto &queue = blockingQueue[i];
        consumers.emplace_back([&i, &nEventsConsumer, &nConsumed, &queue, &startMark, &stopMark]() {
            opencmw::thread::setThreadName(std::format("consumer{}", i));
            std::array<bool, maxCores> affinity{};
            affinity[(2 * i + 1) % maxThreads] = true;
            opencmw::thread::setThreadAffinity(affinity);
            startMark.arrive_and_wait();

            std::size_t   lCounter = 0U;
            std::uint64_t sum      = 0;
            for (auto e = 0U; e < nEventsConsumer; e++) {
                lCounter = queue.pop();
                sum += lCounter;
                lCounter++;
                nConsumed.fetch_add(1, std::memory_order_relaxed);
            }
            stopMark.arrive_and_wait();
            auto calcSum = nEventsConsumer * (nEventsConsumer + 1) / 2;
            if (sum != calcSum) {
                std::print("finished BlockingQueue consumer {} - nConsumed {} of {} sum-mismatch: {} vs {}\n", i, lCounter, nEventsConsumer, sum, calcSum);
            }
        });
    }
    std::ranges::for_each(producers, [](auto &thread) { thread.join(); });
    std::ranges::for_each(consumers, [](auto &thread) { thread.join(); });
    if (nConsumer != 0 && nProduced != nConsumed / nConsumer) {
        std::print("BlockingQueue producer {} vs. consumer {} mismatch\n", nProduced.load(), nConsumed.load() / nConsumer);
    }

    const auto opsPerSecond = static_cast<double>(nLoop) * (1e3 / time_elapsed.count());
    if (printCheck) {
        std::print("{:25} - {}P-{}C: {} events in {:6.2f} ms -> {:.1E} ops/s\n", "BlockingQueue", nProducer, nConsumer, nLoop, time_elapsed.count(), opsPerSecond);
    }
    return opsPerSecond;
}

template<opencmw::disruptor::ProducerType producerType>
double testDisruptor(const std::uint64_t nLoop, const std::uint64_t nProducer, const std::uint64_t nConsumer, bool printCheck = true) {
    using namespace opencmw;
    using namespace opencmw::disruptor;
    assert(isPower2(nLoop) && nLoop > 1);
    assert(nProducer > 0);
    assert(nConsumer > 0);
    std::atomic<std::uint64_t>           nProduced{ 0 };
    std::atomic<std::uint64_t>           nConsumed{ 0 };
    [[maybe_unused]] const std::uint64_t nEventsProducer      = nLoop / nProducer;
    [[maybe_unused]] const std::uint64_t nEventsTotalProducer = nEventsProducer * nProducer;
    [[maybe_unused]] const std::uint64_t nEventsConsumer      = nEventsTotalProducer;

    auto                                 ringBuffer           = newRingBuffer<std::uint64_t, 8192, BusySpinWaitStrategy, producerType>();

    // start/stop barriers
    auto                                      time_start = std::chrono::system_clock::now();
    std::chrono::duration<double, std::milli> time_elapsed;
    auto                                      on_completion1 = [&time_start]() noexcept { time_start = std::chrono::system_clock::now(); };
    auto                                      on_completion2 = [&time_start, &time_elapsed]() noexcept { time_elapsed = std::chrono::system_clock::now() - time_start; };
    std::barrier                              startMark(static_cast<long>(nProducer + nConsumer), on_completion1);
    std::barrier                              stopMark(static_cast<long>(nProducer + nConsumer), on_completion2);
    // init producers
    std::vector<std::jthread> producers;
    for (auto i = 0U; i < nProducer; i++) {
        producers.emplace_back([&i, &nEventsProducer, &nProduced, &ringBuffer, &startMark, &stopMark]() {
            opencmw::thread::setThreadName(std::format("publisher{}", i));
            std::array<bool, maxCores> affinity{};
            affinity[(2 * i) % maxThreads] = true;
            opencmw::thread::setThreadAffinity(affinity);
            startMark.arrive_and_wait();

            for (std::uint64_t e = 0; e < nEventsProducer; e++) {
                while (!ringBuffer->tryPublishEvent([&nProduced](std::uint64_t &&event, std::int64_t) {
                    event = nProduced.fetch_add(1, std::memory_order_relaxed) + 1;
                })) {
                    std::this_thread::yield(); // write-read contention
                }
            }
            stopMark.arrive_and_wait();
        });
    }

    // init consumers
    std::vector<std::jthread> consumers;
    for (auto i = 0U; i < nConsumer; i++) {
        consumers.emplace_back([&i, &nEventsConsumer, &nConsumed, &ringBuffer, &startMark, &stopMark]() {
            const auto &poller = ringBuffer->newPoller();
            ringBuffer->addGatingSequences({ poller->sequence() });
            opencmw::thread::setThreadName(std::format("consumer{}", i));
            std::array<bool, maxCores> affinity{};
            affinity[(2 * i + 1) % maxThreads] = true;
            opencmw::thread::setThreadAffinity(affinity);
            startMark.arrive_and_wait();

            std::uint64_t lCounter = 0;
            std::uint64_t sum      = 0;
            while (lCounter < nEventsConsumer) {
                if (poller->poll([&nConsumed, &lCounter, &sum](const std::uint64_t &event, std::int64_t, bool) noexcept {
                        sum += event;
                        lCounter++;
                        nConsumed.fetch_add(1, std::memory_order_relaxed);
                    }) != PollState::Processing) {
                    std::this_thread::yield(); // write-read contention
                }
            }
            stopMark.arrive_and_wait();
            auto calcSum = nEventsConsumer * (nEventsConsumer + 1) / 2;
            if (sum != calcSum) {
                std::print("finished RingBuffer consumer {} - nConsumed {} of {} sum-mismatch: {} vs {}\n", i, lCounter, nEventsConsumer, sum, calcSum);
            }
        });
    }
    std::ranges::for_each(producers, [](auto &thread) { thread.join(); });
    std::ranges::for_each(consumers, [](auto &thread) { thread.join(); });

    if (nConsumer != 0 && nProduced != nConsumed / nConsumer) {
        std::print("RingBuffer producer {} vs. consumer {} mismatch\n", nProduced.load(), nConsumed.load() / nConsumer);
    }

    const auto opsPerSecond = static_cast<double>(nLoop) * (1e3 / time_elapsed.count());
    if (printCheck) {
        std::print("{:25} - {}P-{}C: {} events in {:6.2f} ms -> {:.1E} ops/s\n", "Disruptor", nProducer, nConsumer, nLoop, time_elapsed.count(), opsPerSecond);
    }
    return opsPerSecond;
}

template<bool align = true, typename T, size_t mRow, size_t nCol>
void print_matrix(const std::array<std::array<T, mRow>, nCol> &M, std::format_string<const T &> str = "{}") {
    size_t columWidth = 0;
    for (size_t j = 0; j < nCol; ++j) {
        size_t max_len{};
        for (size_t i = 0; i < mRow; ++i) {
            auto strSize = std::format(str, M[i][j]).size();
            max_len      = std::max(max_len, strSize);
        }
        columWidth = std::max(max_len, columWidth);
    }

    std::print("┌{1:{0}}┐\n", nCol * (columWidth + 1) + 1, ' ');
    for (size_t i = 0; i < mRow; ++i) {
        for (size_t j = 0; j < nCol; ++j) {
            constexpr auto cellStr = align ? "{1:}{2:>{0}} {3:}" : "{1:}{2:^{0}} {3:}";
            std::print(cellStr, columWidth, j == 0 ? "│ " : "", std::format(str, M[i][j]), j == nCol - 1 ? "│" : "");
        }
        std::print("\n");
    }
    std::print("└{1:{0}}┘\n", nCol * (columWidth + 1) + 1, ' ');
}

int main() {
    using namespace opencmw::disruptor;
    const std::size_t                                                      maxProducerBit = 3;
    const std::size_t                                                      maxConsumerBit = 3;
    constexpr long                                                         nLoop          = 1 << 20;
    constexpr long                                                         nIter          = 1;

    std::array<std::array<double, maxConsumerBit + 1>, maxProducerBit + 1> opsBlockingQueue{};
    std::array<std::array<double, maxConsumerBit + 1>, maxProducerBit + 1> opsRingBuffer{};
    std::array<std::array<double, maxConsumerBit + 1>, maxProducerBit + 1> opsSpeedUp{};
    for (unsigned c = 0; c <= maxConsumerBit; c++) {
        for (unsigned p = 0; p <= maxProducerBit; p++) {
            for (unsigned i = 0; i < nIter; i++) {
                opsBlockingQueue[p][c] += testBlockingQueue(nLoop, 1 << p, 1 << c, i == 0);
                if (p == 0) {
                    opsRingBuffer[p][c] += testDisruptor<ProducerType::Single>(nLoop, 1 << p, 1 << c, i == 0);
                } else {
                    opsRingBuffer[p][c] += testDisruptor<ProducerType::Multi>(nLoop, 1 << p, 1 << c, i == 0);
                }
            }
        }
    }
    for (unsigned c = 0; c <= maxConsumerBit; c++) {
        for (unsigned p = 0; p <= maxProducerBit; p++) {
            opsSpeedUp[p][c] = (opsRingBuffer[p][c] / opsBlockingQueue[p][c] - 1.0) * 100.0 / static_cast<double>(nIter);
        }
    }

    std::print("finished test:\nBlockingQueue [ops/s]\n");
    print_matrix(opsBlockingQueue, "{:.1E}");
    std::print("RingBuffer [ops/s]\n");
    print_matrix(opsRingBuffer, "{:.1E}");
    std::print("RingBuffer > BlockingQueue [%]\n");
    print_matrix(opsSpeedUp, "{:.1f}");
}