#pragma once

#include <atomic>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

#include "BlockingQueue.hpp"
#include "ITaskScheduler.hpp"

namespace opencmw::disruptor {

/**
 * An implementation of TaskScheduler which creates an underlying thread pool and set processor affinity to each thread.
 */
class RoundRobinThreadAffinedTaskScheduler : public ITaskScheduler {
private:
    BlockingQueue<std::packaged_task<void()>> m_tasks;
    std::atomic<bool>                         m_started{ false };
    std::vector<std::thread>                  m_threads;

public:
    void              start(std::int32_t numberOfThreads) override;
    void              stop() override;

    std::future<void> scheduleAndStart(std::packaged_task<void()> &&task) override;

private:
    void createThreads(std::int32_t numberOfThreads);
    void workingLoop(std::int32_t threadId);
    void tryExecuteTask(std::packaged_task<void()> &task);
};

} // namespace opencmw::disruptor
