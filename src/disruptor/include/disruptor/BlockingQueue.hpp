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
    TQueue                  _queue;
    mutable std::mutex      _mutex;
    std::condition_variable _conditionVariable;

public:
    void push(const T &value) {
        {
            std::unique_lock lock(_mutex);
            _queue.push_back(value);
        }
        _conditionVariable.notify_one();
    }

    void push(T &&value) {
        {
            std::unique_lock lock(_mutex);
            _queue.push_back(std::move(value));
        }
        _conditionVariable.notify_one();
    }

    bool empty() const {
        std::unique_lock lock(_mutex);
        return _queue.empty();
    }

    template<typename TDuration>
    bool timedWaitAndPop(T &value, const TDuration &duration) {
        std::unique_lock lock(_mutex);

        if (!_conditionVariable.wait_for(lock, duration, [this]() -> bool { return !_queue.empty(); }))
            return false;

        value = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }
};

} // namespace opencmw::disruptor
