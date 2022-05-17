#ifndef OPENCMW_CPP_THREADPOOL_HPP
#define OPENCMW_CPP_THREADPOOL_HPP

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <list>
#include <mutex>
#include <span>
#include <string>
#include <thread>

#include <fmt/format.h>

#include <SpinWait.hpp>
#include <ThreadAffinity.hpp>

namespace opencmw {

namespace thread_pool::detail {
struct Task {
    uint64_t              id;
    std::function<void()> func;
    Task                 *next = nullptr;
    Task                 *self = this;
    bool                  operator==(const Task &other) const { return self == other.self; }
};

class TaskQueue {
    mutable AtomicMutex<> _lock;
    Task                 *_head = nullptr;
    Task                 *_tail = nullptr;
    uint32_t              _size = 0;

public:
    TaskQueue()                       = default;
    TaskQueue(const TaskQueue &queue) = delete;
    TaskQueue &operator=(const TaskQueue &queue) = delete;
    ~TaskQueue() { clear(); }

    uint32_t clear() {
        std::scoped_lock lock(_lock);
        const uint32_t res = _size;
        for (Task *job = pop(); job != nullptr; job = pop()) {
            delete job;
        }
        assert(_size == 0 && "TaskQueue::clear() failed");
        return res;
    }

    uint32_t size() const {
        std::scoped_lock lock(_lock);
        return _size;
    }

    void push(Task *job) {
        std::scoped_lock lock(_lock);
        job->next = nullptr;
        if (_head == nullptr) {
            _head = job;
        }
        if (_tail == nullptr) {
            _tail = job;
        } else {
            _tail->next = job;
            _tail       = job;
        }
        _size++;
    };

    Task *pop() {
        if (_head == nullptr) return nullptr;
        std::scoped_lock lock(_lock);

        Task            *head = _head;
        if (head != nullptr) {
            _head = head->next;
            _size--;
            if (head == _tail) {
                _tail = nullptr;
            }
        }
        return head;
    };
};

} // namespace thread_pool::detail

template<typename T>
concept ThreadPool = requires(T t, std::function<void()> &&func) {
    { t.enqueue(std::move(func)) } -> std::same_as<void>;
    { t.execute(std::move(func)) } -> std::same_as<void>;
};

/**
 * <h2>Basic thread pool that can optionally grow/shrink between a [min, max] number of threads.</h2>
 * The growth policy is controlled by:
 * <ol type="a">
 *   <li> the minimum/maximum thresholds, and</li>
 *   <li> whether a task is scheduled via:</li>
 *   <ol type="1">
 *     <li> <code>enqueue(...)</code> i.e. re-using and queuing on an existing thread, or</li>
 *     <li> <code>execute(...)</code> which may spawn a new thread if no free one is available.</li>
 *   </ol>
 * </ol>
 * Unused threads are kept alive for a pre-defined amount of time to be reused and gracefully shut down to the
 * minimum number of threads when unused.
 * <br>
 * The CPU affinity and OS scheduling policy and priorities are controlled by:
 * <ul>
 *  <li> <code>setAffinityMask(std::vector&lt;bool&gt; threadAffinityMask);</code> </li>
 *  <li> <code>setThreadSchedulingPolicy(const thread::Policy schedulingPolicy, const int schedulingPriority)</code> </li>
 * </ul>
 */
class BasicThreadPool {
    using Task      = thread_pool::detail::Task;
    using TaskQueue = thread_pool::detail::TaskQueue;
    static std::atomic<uint64_t> _globalPoolId;
    static std::atomic<uint64_t> _taskID;
    static std::string           getName() { return fmt::format("BasicThreadPool#{}", _globalPoolId.fetch_add(1)); }

    std::atomic<bool>            _initialised = ATOMIC_FLAG_INIT;
    bool                         _shutdown    = { false };

    std::condition_variable      _condition;
    TaskQueue                    _taskQueue;
    TaskQueue                    _blockingTaskQueue;
    std::atomic<std::size_t>     _numTaskedQueued{ 0U };
    std::atomic<std::size_t>     _numTasksRunning{ 0U };
    TaskQueue                    _recycledTasks;

    std::mutex                   _threadListMutex;
    std::atomic<std::size_t>     _numThreads{ 0U };
    std::list<std::jthread>      _threads;

    std::vector<bool>            _affinityMask{};
    thread::Policy               _schedulingPolicy   = thread::Policy::OTHER;
    int                          _schedulingPriority = 0;

    const std::string            _poolName;
    const uint32_t               _minThreads;
    const uint32_t               _maxThreads;
    std::chrono::microseconds    _sleepDuration     = std::chrono::milliseconds(1);
    std::chrono::seconds         _keepAliveDuration = std::chrono::seconds(10);

public:
    BasicThreadPool()
        : BasicThreadPool(getName(), std::thread::hardware_concurrency(), std::thread::hardware_concurrency()) {}
    BasicThreadPool(std::string_view name, uint32_t min, uint32_t max)
        : _poolName(name), _minThreads(min), _maxThreads(max) {
        assert(min > 0 && "minimum number of threads must be > 0");
        assert(min <= max && "minimum number of threads must be <= maximum number of threads");
        for (uint32_t i = 0; i < _minThreads; ++i) {
            createWorkerThread();
        }
    }

    ~BasicThreadPool() {
        _shutdown = true;
        _condition.notify_all();
        while (_numThreads > 0) {
            std::this_thread::sleep_for(_sleepDuration);
        }
        _threads.clear();
        _recycledTasks.clear();
        [[maybe_unused]] const auto queueSize1 = _blockingTaskQueue.clear();
        [[maybe_unused]] const auto queueSize2 = _taskQueue.clear();
        assert(queueSize1 == 0 && "blocking task queue not empty");
        assert(queueSize2 == 0 && "task queue not empty");
    }
    BasicThreadPool(const BasicThreadPool &) = delete;
    BasicThreadPool(BasicThreadPool &&)      = delete;
    BasicThreadPool           &operator=(const BasicThreadPool &) = delete;
    BasicThreadPool           &operator=(BasicThreadPool &&) = delete;

    [[nodiscard]] std::string  poolName() const noexcept { return _poolName; }
    [[nodiscard]] uint32_t     minThreads() const noexcept { return _minThreads; };
    [[nodiscard]] uint32_t     maxThreads() const noexcept { return _maxThreads; };
    [[nodiscard]] std::size_t  numThreads() const noexcept { return std::atomic_load_explicit(&_numThreads, std::memory_order_acquire); }
    [[nodiscard]] std::size_t  numTasksRunning() const noexcept { return std::atomic_load_explicit(&_numTasksRunning, std::memory_order_acquire); }
    [[nodiscard]] std::size_t  numTasksQueued() const { return std::atomic_load_explicit(&_numTaskedQueued, std::memory_order_acquire); }
    [[nodiscard]] std::size_t  numTasksRecycled() const { return _recycledTasks.size(); }
    std::chrono::microseconds &sleepDuration() noexcept { return _sleepDuration; }
    std::chrono::seconds      &keepAliveDuration() noexcept { return _keepAliveDuration; }
    [[nodiscard]] bool         isInitialised() const { return _initialised.load(std::memory_order::acquire); }
    void                       waitUntilInitialised() const { _initialised.wait(false); }
    void                       requestShutdown() {
        _shutdown = true;
        _condition.notify_all();
    }
    [[nodiscard]] bool isShutdown() const { return _shutdown; }

    //
    [[nodiscard]] auto getAffinityMask() const { return _affinityMask; }
    void               setAffinityMask(std::vector<bool> threadAffinityMask) {
        _affinityMask.clear();
        std::ranges::copy(threadAffinityMask, std::back_inserter(_affinityMask));
        cleanupFinishedThreads();
        updateThreadConstraints();
    }

    [[nodiscard]] auto getSchedulingPolicy() const { return _schedulingPolicy; }
    [[nodiscard]] auto getSchedulingPriority() const { return _schedulingPriority; }
    void               setThreadSchedulingPolicy(const thread::Policy schedulingPolicy = thread::Policy::OTHER, const int schedulingPriority = 0) {
        _schedulingPolicy   = schedulingPolicy;
        _schedulingPriority = schedulingPriority;
        cleanupFinishedThreads();
        updateThreadConstraints();
    }

    void enqueue(std::function<void()> &&func) {
        std::atomic_fetch_add(&_numTaskedQueued, 1U);
        _taskQueue.push(createTask(std::move(func)));
        _condition.notify_one();
    }

    void execute(std::function<void()> &&func) {
        static thread_local SpinWait spinWait;
        std::atomic_fetch_add(&_numTaskedQueued, 1U);
        _blockingTaskQueue.push(createTask(std::move(func)));
        _condition.notify_one();
        spinWait.spinOnce();
        spinWait.spinOnce();
        while (_blockingTaskQueue.size() > 0) {
            if (const auto nThreads = numThreads(); nThreads <= numTasksRunning() && nThreads <= _maxThreads) {
                createWorkerThread();
            }
        }
        spinWait.reset();
    }

private:
    void cleanupFinishedThreads() {
        std::scoped_lock lock(_threadListMutex);
        std::erase_if(_threads, [](auto &thread) { return !thread.joinable(); });
    }

    void updateThreadConstraints() {
        std::size_t      threadID = 0;
        std::scoped_lock lock(_threadListMutex);
        std::ranges::for_each(_threads, [&](auto &thread) { updateThreadConstraints(++threadID, thread); });
    }

    void updateThreadConstraints(const std::size_t threadID, std::jthread &thread) const {
        thread::setThreadName(fmt::format("{}#{}", _poolName, threadID), thread);
        if (!_affinityMask.empty()) {
            thread::setThreadAffinity(_affinityMask, thread);
        }
        thread::setThreadSchedulingParameter(_schedulingPolicy, _schedulingPriority, thread);
    }

    void createWorkerThread() {
        std::scoped_lock  lock(_threadListMutex);
        const std::size_t nThreads = numThreads();
        std::jthread     &thread   = _threads.emplace_back(&BasicThreadPool::worker, this);
        updateThreadConstraints(nThreads + 1, thread);
        _numThreads.wait(nThreads);
    }

    Task *createTask(std::function<void()> &&func) {
        Task *task = _recycledTasks.pop();
        if (task == nullptr) {
            task = new Task{ std::atomic_fetch_add(&_taskID, 1U) + 1U, std::move(func) };
        } else {
            task->id   = std::atomic_fetch_add(&_taskID, 1U) + 1U;
            task->func = std::move(func);
        }
        return task;
    }

    bool popTask(Task *&task) {
        task = _blockingTaskQueue.pop();
        if (task == nullptr) {
            task = _taskQueue.pop();
        }
        if (task != nullptr) {
            std::atomic_fetch_sub(&_numTaskedQueued, 1U);
            return true;
        }
        return false;
    }

    void worker() {
        constexpr uint32_t N_SPIN       = 1 << 8;
        uint32_t           noop_counter = 0;
        std::atomic_fetch_add(&_numThreads, 1);
        std::mutex       mutex;
        std::unique_lock lock(mutex);
        auto             lastUsed              = std::chrono::steady_clock::now();
        auto             timeDiffSinceLastUsed = std::chrono::steady_clock::now() - lastUsed;
        Task            *currentTask           = nullptr;
        if (numThreads() >= _minThreads) {
            std::atomic_store_explicit(&_initialised, true, std::memory_order_release);
            _initialised.notify_all();
        }
        _numThreads.notify_one();
        do {
            if (popTask(currentTask)) {
                std::atomic_fetch_add(&_numTasksRunning, 1);
                currentTask->func();
                std::atomic_fetch_sub(&_numTasksRunning, 1);
                lastUsed = std::chrono::steady_clock::now();
                _recycledTasks.push(currentTask);
                noop_counter = 0;
            } else if (++noop_counter > N_SPIN) [[unlikely]] {
                // perform some thread maintenance tasks before going to sleep
                noop_counter = noop_counter / 2;
                cleanupFinishedThreads();

                _condition.wait_for(lock, _keepAliveDuration, [this] { return numTasksQueued() > 0 || isShutdown(); });
            }
            timeDiffSinceLastUsed = std::chrono::steady_clock::now() - lastUsed;
        } while (!isShutdown() && (numThreads() <= _minThreads || timeDiffSinceLastUsed < _keepAliveDuration));
        auto nThread = std::atomic_fetch_sub(&_numThreads, 1);
        _numThreads.notify_all();

        if (nThread == 1) {
            // cleanup
            _recycledTasks.clear();
            [[maybe_unused]] auto clearedBlockingTasks = _blockingTaskQueue.clear();
            assert(clearedBlockingTasks == 0 && "blocking task queue not empty");
            [[maybe_unused]] auto clearedTasks = _taskQueue.clear();
            assert(clearedTasks == 0 && "task queue not empty");
        }
    }

    void sleepOrYield() const {
        if (_sleepDuration.count() > 0) {
            std::this_thread::sleep_for(_sleepDuration);
        } else {
            std::this_thread::yield();
        }
    }
};
inline std::atomic<uint64_t> BasicThreadPool::_globalPoolId = 0U;
inline std::atomic<uint64_t> BasicThreadPool::_taskID       = 0U;
static_assert(ThreadPool<opencmw::BasicThreadPool>);

} /* namespace opencmw */

#endif // OPENCMW_CPP_THREADPOOL_HPP
