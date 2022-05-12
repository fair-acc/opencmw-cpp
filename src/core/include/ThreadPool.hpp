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

    template<bool lock = true>
    uint32_t clear() {
        if constexpr (lock) {
            _lock.lock();
        }
        const uint32_t res = _size;
        for (Task *job = pop<false>(); job != nullptr; job = pop<false>()) {
            delete job;
        }
        assert(_size == 0 && "TaskQueue::clear() failed");
        if constexpr (lock) {
            _lock.unlock();
        }
        return res;
    }

    uint32_t size() const {
        std::scoped_lock lock(_lock);
        return _size;
    }

    template<bool lock = true>
    void push(Task *job) {
        if constexpr (lock) {
            _lock.lock();
        }
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
        if constexpr (lock) {
            _lock.unlock();
        }
    };

    template<bool lock = true>
    Task *pop() {
        if constexpr (lock) {
            _lock.lock();
        }
        Task *head = _head;
        if (head != nullptr) {
            _head = head->next;
            _size--;
            if (head == _tail) {
                _tail = nullptr;
            }
        }
        if constexpr (lock) {
            _lock.unlock();
        }
        return head;
    };
};

} // namespace thread_pool::detail

enum TaskType {
    IO_BOUND  = 0,
    CPU_BOUND = 1
};

template<typename T>
concept ThreadPool = requires(T t, std::function<void()> &&func) {
    { t.execute(std::move(func)) } -> std::same_as<void>;
};

/**
 * <h2>Basic thread pool that uses a fixed-number or optionally grow/shrink between a [min, max] number of threads.</h2>
 * The growth policy is controlled by the TaskType template parameter:
 * <ol type="A">
 *   <li> <code>TaskType::IO_BOUND</code> if the task is IO bound, i.e. it is likely to block the thread for a long time, or
 *   <li> <code>TaskType::CPU_BOUND</code> if the task is CPU bound, i.e. it is primarily limited by the CPU and memory bandwidth.
 * </ol>
 * <br>
 * For the IO_BOUND policy, unused threads are kept alive for a pre-defined amount of time to be reused and gracefully
 * shut down to the minimum number of threads when unused.
 * <br>
 * For the CPU_BOUND policy, the threads are equally spread and pinned across the set CPU affinity.
 * <br>
 * The CPU affinity and OS scheduling policy and priorities are controlled by:
 * <ul>
 *  <li> <code>setAffinityMask(std::vector&lt;bool&gt; threadAffinityMask);</code> </li>
 *  <li> <code>setThreadSchedulingPolicy(const thread::Policy schedulingPolicy, const int schedulingPriority)</code> </li>
 * </ul>
 */
template<TaskType taskType>
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
    std::atomic<std::size_t>     _numTaskedQueued{ 0U };
    std::atomic<std::size_t>     _numTasksRunning{ 0U };
    TaskQueue                    _recycledTasks;

    std::mutex                   _threadListMutex{};
    std::atomic<std::size_t>     _numThreads{ 0U };
    std::list<std::jthread>      _threads;

    std::vector<bool>            _affinityMask{};
    thread::Policy               _schedulingPolicy   = thread::Policy::OTHER;
    int                          _schedulingPriority = 0;

    const std::string            _poolName;
    const uint32_t               _minThreads;
    const uint32_t               _maxThreads;
    std::chrono::microseconds    _sleepDuration       = std::chrono::milliseconds(1);
    std::chrono::seconds         _keepAliveDurationIO = std::chrono::seconds(10);

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
        _recycledTasks.clear();
        [[maybe_unused]] const auto queueSize = _taskQueue.clear();
        assert(queueSize == 0 && "task queue not empty");
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
    std::chrono::seconds      &keepAliveDuration() noexcept { return _keepAliveDurationIO; }
    [[nodiscard]] bool         isInitialised() const { return _initialised.load(std::memory_order::acquire); }
    void                       waitUntilInitialised() const { _initialised.wait(false); }
    void                       requestShutdown() {
        _shutdown = true;
        _condition.notify_all();
    }
    [[nodiscard]] bool isShutdown() const { return _shutdown; }

    //

    [[nodiscard]] std::vector<bool> getAffinityMask() const { return _affinityMask; }

    void                            setAffinityMask(const std::vector<bool> &threadAffinityMask) {
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

    void execute(std::function<void()> &&func) {
        static thread_local SpinWait spinWait;
        std::atomic_fetch_add(&_numTaskedQueued, 1U);
        _taskQueue.push(createTask(std::move(func)));
        _condition.notify_one();
        if constexpr (taskType == TaskType::IO_BOUND) {
            _condition.notify_one();
            spinWait.spinOnce();
            spinWait.spinOnce();
            while (_taskQueue.size() > 0) {
                if (const auto nThreads = numThreads(); nThreads <= numTasksRunning() && nThreads <= _maxThreads) {
                    createWorkerThread();
                }
            }
            spinWait.reset();
        }
    }

private:
    void cleanupFinishedThreads() {
        std::scoped_lock lock(_threadListMutex);
        std::erase_if(_threads, [](auto &thread) { return !thread.joinable(); });
    }

    void updateThreadConstraints() {
        std::size_t      threadID = 0;
        std::scoped_lock lock(_threadListMutex);
        std::erase_if(_threads, [](auto &thread) { return !thread.joinable(); });
        std::ranges::for_each(_threads, [&](auto &thread) { updateThreadConstraints(threadID++, thread); });
    }

    void updateThreadConstraints(const std::size_t threadID, std::jthread &thread) const {
        thread::setThreadName(fmt::format("{}#{}", _poolName, threadID), thread);
        thread::setThreadSchedulingParameter(_schedulingPolicy, _schedulingPriority, thread);
        if (!_affinityMask.empty()) {
            if (taskType == TaskType::IO_BOUND) {
                thread::setThreadAffinity(_affinityMask);
                return;
            }
            const std::vector<bool> affinityMask = distributeThreadAffinityAcrossCores(_affinityMask, threadID);
            std::cout << fmt::format("{}#{} affinity mask: {}", _poolName, threadID, fmt::join(affinityMask, ",")) << std::endl;
            thread::setThreadAffinity(affinityMask);
        }
    }

    std::vector<bool> distributeThreadAffinityAcrossCores(const std::vector<bool> &globalAffinityMask, const std::size_t threadID) const {
        if (globalAffinityMask.empty()) {
            return {};
        }
        std::vector<bool> affinityMask;
        int               coreCount = 0;
        for (bool value : globalAffinityMask) {
            if (value) {
                affinityMask.push_back(coreCount++ % _minThreads == threadID);
            } else {
                affinityMask.push_back(false);
            }
        }
        return affinityMask;
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
        task = _taskQueue.pop();
        if (task == nullptr) {
            return false;
        }
        std::atomic_fetch_sub(&_numTaskedQueued, 1U);
        return true;
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

                _condition.wait_for(lock, _keepAliveDurationIO, [this] { return numTasksQueued() > 0 || isShutdown(); });
            }
            timeDiffSinceLastUsed = std::chrono::steady_clock::now() - lastUsed;
        } while (!isShutdown() && (numThreads() <= _minThreads || timeDiffSinceLastUsed < _keepAliveDurationIO));
        auto nThread = std::atomic_fetch_sub(&_numThreads, 1);
        _numThreads.notify_all();

        if (nThread == 1) {
            // cleanup
            _recycledTasks.clear();
            [[maybe_unused]] const auto queueSize = _taskQueue.clear();
            assert(queueSize == 0 && "task queue not empty");
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
template<TaskType T>
inline std::atomic<uint64_t> BasicThreadPool<T>::_globalPoolId = 0U;
template<TaskType T>
inline std::atomic<uint64_t> BasicThreadPool<T>::_taskID = 0U;
static_assert(ThreadPool<opencmw::BasicThreadPool<IO_BOUND>>);
static_assert(ThreadPool<opencmw::BasicThreadPool<CPU_BOUND>>);

} /* namespace opencmw */

#endif // OPENCMW_CPP_THREADPOOL_HPP
