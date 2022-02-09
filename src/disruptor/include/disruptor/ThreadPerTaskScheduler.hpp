#pragma once

#include "ITaskScheduler.hpp"

#include <thread>
#include <vector>

namespace opencmw::disruptor {

class ThreadPerTaskScheduler : public ITaskScheduler {
private:
    std::atomic<bool>         m_started{ false };
    std::vector<std::jthread> m_threads;

public:
    void start(std::size_t numberOfThreads = 0U) override {
        if (m_started) {
            return;
        }

        m_started = true;
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
        auto result = task.get_future();

        m_threads.emplace_back([this, task{ move(task) }]() mutable {
            while (m_started) {
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
