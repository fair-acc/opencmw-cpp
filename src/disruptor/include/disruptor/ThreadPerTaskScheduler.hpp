#pragma once

#include "ITaskScheduler.hpp"

#include <thread>
#include <vector>

namespace opencmw::disruptor {

class ThreadPerTaskScheduler : public ITaskScheduler {
private:
    std::atomic<bool>        m_started{ false };
    std::vector<std::thread> m_threads;

public:
    void              start(std::int32_t numberOfThreads = 0) override;
    void              stop() override;

    std::future<void> scheduleAndStart(std::packaged_task<void()> &&task) override;
};

} // namespace opencmw::disruptor
