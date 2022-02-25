#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

#include <opencmw.hpp>

namespace opencmw::disruptor {

template<class T, class TQueue = std::deque<T>>
class BlockingQueue {
    TQueue                  _queue;
    mutable std::mutex      _mutex;
    std::condition_variable _condition;

public:
    void push(const T &value) {
        std::unique_lock lock(_mutex);
        _queue.push_back(value);
        _condition.notify_one();
    }

    void push(T &&value) {
        std::unique_lock lock(_mutex);
        _queue.push_back(std::move(value));
        _condition.notify_one();
    }

    [[nodiscard]] bool empty() const {
        std::unique_lock lock(_mutex);
        return _queue.empty();
    }

    [[nodiscard]] T pop() {
        std::unique_lock lock(_mutex);
        _condition.wait(lock, [this]() { return !_queue.empty(); });
        T val = _queue.front();
        _queue.pop_front();
        return val;
    }

    [[nodiscard]] bool timedWaitAndPop(T &value, Duration auto &&duration) {
        std::unique_lock lock(_mutex);
        if (!_condition.wait_for(lock, duration, [this]() { return !_queue.empty(); })) { // NOSONAR
            return false;
        }
        value = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }
};

} // namespace opencmw::disruptor
