#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

namespace opencmw::disruptor {

template<
        class T,
        class TQueue = std::deque<T>>
class BlockingQueue {
private:
    TQueue                  m_queue;
    mutable std::mutex      m_mutex;
    std::condition_variable m_conditionVariable;

public:
    void push(const T &value) {
        {
            std::unique_lock lock(m_mutex);
            m_queue.push_back(value);
        }
        m_conditionVariable.notify_one();
    }

    void push(T &&value) {
        {
            std::unique_lock lock(m_mutex);
            m_queue.push_back(std::move(value));
        }
        m_conditionVariable.notify_one();
    }

    bool empty() const {
        std::unique_lock lock(m_mutex);
        return m_queue.empty();
    }

    template<typename TDuration>
    bool timedWaitAndPop(T &value, const TDuration &duration) {
        std::unique_lock lock(m_mutex);

        if (!m_conditionVariable.wait_for(lock, duration, [this]() -> bool { return !m_queue.empty(); }))
            return false;

        value = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }
};

} // namespace opencmw::disruptor
