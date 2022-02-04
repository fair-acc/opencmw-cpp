#pragma once

#include <future>

namespace opencmw::disruptor {

class ITaskScheduler {
public:
    virtual ~ITaskScheduler()                                                     = default;

    virtual void              start(std::int32_t numberOfThreads)                 = 0;
    virtual void              stop()                                              = 0;

    virtual std::future<void> scheduleAndStart(std::packaged_task<void()> &&task) = 0;
};

} // namespace opencmw::disruptor
