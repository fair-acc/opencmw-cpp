#pragma once

#include <atomic>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

#include "BlockingQueue.hpp"
#include "Exceptions.hpp"
#include "ITaskScheduler.hpp"
#include "ThreadHelper.hpp"

namespace opencmw::disruptor {

/**
 * An implementation of TaskScheduler which creates an underlying thread pool and set processor affinity to each thread.
 */
class RoundRobinThreadAffinedTaskScheduler : public ITaskScheduler {
private:
    BlockingQueue<std::packaged_task<void()>> m_tasks;
    std::atomic<bool>                         m_started{ false };
    std::vector<std::jthread>                 m_threads;

public:
    void start(std::size_t numberOfThreads) override {
        if (m_started) {
            return;
        }

        m_started = true;

        if (numberOfThreads < 1) {
            DISRUPTOR_THROW_ARGUMENT_OUT_OF_RANGE_EXCEPTION(numberOfThreads);
        }

        createThreads(numberOfThreads);
    }
    void stop() override {
        if (!m_started) {
            return;
        }

        m_started = false;

        for (auto &&thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    std::future<void> scheduleAndStart(std::packaged_task<void()> &&task) override {
        auto future = task.get_future();
        m_tasks.push(std::move(task));

        return future;
    }

private:
    void createThreads(std::size_t numberOfThreads) {
        for (auto i = 0U; i < numberOfThreads; ++i) {
            m_threads.emplace_back([this, i]() { workingLoop(i); });
        }
    }

    void workingLoop(std::size_t threadId) {
        static const auto processorCount = std::thread::hardware_concurrency();

        const auto        processorIndex = threadId % processorCount;

        const auto        affinityMask   = ThreadHelper::AffinityMask(1ull << processorIndex);

        ThreadHelper::setThreadAffinity(affinityMask);

        while (m_started) {
            std::packaged_task<void()> task;
            while (m_tasks.timedWaitAndPop(task, std::chrono::milliseconds(100))) {
                tryExecuteTask(task);
            }
        }
    }

    void tryExecuteTask(std::packaged_task<void()> &task) {
        task();
    }
};

} // namespace opencmw::disruptor
