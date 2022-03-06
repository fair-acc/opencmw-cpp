#pragma once

#include <memory>

#include "IExecutor.hpp"
#include "ITaskScheduler.hpp"

namespace opencmw::disruptor {

class ITaskScheduler;

/**
 * TaskScheduler implementation for IExecutor
 */
class BasicExecutor : public IExecutor {
private:
    std::shared_ptr<ITaskScheduler> _taskScheduler;

public:
    /**
     * Create a new BasicExecutor with a given TaskScheduler that will handle low-level queuing of commands execution.
     */
    explicit BasicExecutor(const std::shared_ptr<ITaskScheduler> &taskScheduler)
        : _taskScheduler(taskScheduler) {
    }

    /**
     * Start a new task executiong the given command in the current taskscheduler
     * \param command
     */
    std::future<void> execute(const std::function<void()> &command) override {
        return _taskScheduler->scheduleAndStart(std::packaged_task<void()>([command] {
            const std::function<void()> &command1 = command;
            try {
                command1();
            } catch (...) {

            } }));
    }
};

} // namespace opencmw::disruptor
