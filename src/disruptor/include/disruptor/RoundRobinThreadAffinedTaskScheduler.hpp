#pragma once

#include <atomic>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

#include "BlockingQueue.hpp"
#include "ITaskScheduler.hpp"
#include "ThreadHelper.hpp"

namespace opencmw::disruptor {

/**
 * An implementation of TaskScheduler which creates an underlying thread pool and set processor affinity to each thread.
 */
class RoundRobinThreadAffinedTaskScheduler : public ITaskScheduler {
private:
    BlockingQueue<std::packaged_task<void()>> _tasks;
    std::atomic<bool>                         _started{ false };
    std::vector<std::jthread>                 _threads;

public:
    void start(std::size_t numberOfThreads) override {
        if (_started) {
            return;
        }

        _started = true;

        if (numberOfThreads < 1) {
            throw std::out_of_range("number of threads must be at least 1"); // TODO: replace by concept restriction
        }

        createThreads(numberOfThreads);
    }
    void stop() override {
        if (!_started) {
            return;
        }

        _started = false;

        for (auto &&thread : _threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    std::future<void> scheduleAndStart(std::packaged_task<void()> &&task) override {
        auto future = task.get_future();
        _tasks.push(std::move(task));

        return future;
    }

private:
    void createThreads(std::size_t numberOfThreads) {
        for (auto i = 0U; i < numberOfThreads; ++i) {
            _threads.emplace_back([this, i]() { workingLoop(i); });
        }
    }

    void workingLoop(std::size_t threadId) {
        static const auto processorCount = std::thread::hardware_concurrency();

        const auto        processorIndex = threadId % processorCount;

        const auto        affinityMask   = thread_helper::AffinityMask(1ULL << processorIndex);

        thread_helper::setThreadAffinity(affinityMask);

        while (_started) {
            std::packaged_task<void()> task;
            while (_tasks.timedWaitAndPop(task, std::chrono::milliseconds(100))) {
                tryExecuteTask(task);
            }
        }
    }

    void tryExecuteTask(std::packaged_task<void()> &task) const {
        task();
    }
};

} // namespace opencmw::disruptor
