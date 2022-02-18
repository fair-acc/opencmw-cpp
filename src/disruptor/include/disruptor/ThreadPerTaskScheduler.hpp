#pragma once

#include "ITaskScheduler.hpp"

#include <thread>
#include <vector>

namespace opencmw::disruptor {

class ThreadPerTaskScheduler : public ITaskScheduler {
private:
    std::atomic<bool>         _started{ false };
    std::vector<std::jthread> _threads;

public:
    void start(std::size_t numberOfThreads = 0U) override {
        if (_started) {
            return;
        }

        _started = true;
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
        auto result = task.get_future();

        _threads.emplace_back([this, task{ move(task) }]() mutable {
            while (_started) {
                try {
                    task();
                } catch (...) {
                }
            }
        });

        return result;
    }
};

} // namespace opencmw::disruptor
