#ifndef OPENCMW_CPP_THREADPOOL_HPP
#define OPENCMW_CPP_THREADPOOL_HPP

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <format>

#include <Formatter.hpp>
#include <opencmw.hpp>
#include <SpinWait.hpp>
#include <ThreadAffinity.hpp>

namespace opencmw {
using namespace std::chrono_literals;

class TaskQueue;

namespace thread_pool::detail {

/**
 * @brief a move-only implementation of std::function by Matthias Kretz, GSI
 * TODO(C++23): to be replaced once C++23's STL version is out/available:
 * https://en.cppreference.com/w/cpp/utility/functional/move_only_function/move_only_function
 */
class move_only_function {
    using FunPtr          = std::unique_ptr<void, void (*)(void *)>;
    FunPtr _erased_fun    = { nullptr, [](void *) {} };
    void (*_call)(void *) = nullptr;

public:
    constexpr move_only_function() = default;

    template<typename F>
        requires(!std::is_reference_v<F>)
    constexpr move_only_function(F &&fun)
        : _erased_fun(new F(std::forward<F>(fun)), [](void *ptr) { delete static_cast<F *>(ptr); }), _call([](void *ptr) { (*static_cast<F *>(ptr))(); }) {}

    template<typename F>
        requires(!std::is_reference_v<F>)
    constexpr move_only_function &operator=(F &&fun) {
        _erased_fun = FunPtr(new F(std::forward<F>(fun)), [](void *ptr) { delete static_cast<F *>(ptr); });
        _call       = [](void *ptr) { (*static_cast<F *>(ptr))(); };
        return *this;
    }

    constexpr void operator()() {
        if (_call) {
            _call(_erased_fun.get());
        }
    }
    constexpr void operator()() const {
        if (_call) {
            _call(_erased_fun.get());
        }
    }
};

struct Task {
    uint64_t           id;
    move_only_function func;
    std::string        name{}; // Default value provided to avoid warnings on construction with {.id = ..., .func = ...}
    int32_t            priority = 0;
    int32_t            cpuID    = -1;
    std::weak_ordering operator<=>(const Task &other) const noexcept { return priority <=> other.priority; }

    // We want to reuse objects to avoid reallocations
    void reset() noexcept {
        *this = Task();
    }
};

template<bool lock, typename... Args>
struct conditional_lock : public std::scoped_lock<Args...> {
    using std::scoped_lock<Args...>::scoped_lock;
};

template<typename... Args>
struct conditional_lock<false, Args...> {
    conditional_lock(const Args &...) {};
};

class TaskQueue {
public:
    using TaskContainer = std::list<Task>;

private:
    mutable AtomicMutex<> _mutex;

    TaskContainer         _tasks;

    template<bool shouldLock>
    using conditional_lock = conditional_lock<shouldLock, AtomicMutex<>>;

public:
    TaskQueue()                                  = default;
    TaskQueue(const TaskQueue &queue)            = delete;
    TaskQueue &operator=(const TaskQueue &queue) = delete;
    ~TaskQueue() {
        clear();
    }

    template<bool shouldLock = true>
    void clear() {
        conditional_lock<shouldLock> lock(_mutex);
        _tasks.clear();
    }

    template<bool shouldLock = true>
    std::size_t size() const {
        conditional_lock<shouldLock> lock(_mutex);
        return _tasks.size();
    }

    template<bool shouldLock = true>
    void push(TaskContainer jobContainer) {
        conditional_lock<shouldLock> lock(_mutex);
        assert(!jobContainer.empty());
        auto      &job                = jobContainer.front();
        const auto currentJobPriority = job.priority;

        const auto insertPosition     = [&] {
            if (currentJobPriority == 0) {
                return _tasks.end();
            } else {
                return std::find_if(_tasks.begin(), _tasks.end(),
                            [currentJobPriority](const auto &task) {
                            return task.priority < currentJobPriority;
                        });
            }
        }();

        _tasks.splice(insertPosition, jobContainer, jobContainer.begin(), jobContainer.end());
    }

    template<bool shouldLock = true>
    TaskContainer pop() {
        conditional_lock<shouldLock> lock(_mutex);
        TaskContainer                result;
        if (!_tasks.empty()) {
            result.splice(result.begin(), _tasks, _tasks.begin(), std::next(_tasks.begin()));
        }
        return result;
    }
};

} // namespace thread_pool::detail

class TaskQueue;

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
 * Some user-level examples: <br>
 * @code
 *
 * // pool for CPU-bound tasks with exactly 1 thread
 * opencmw::BasicThreadPool&lt;opencmw::CPU_BOUND&gt; poolWork("CustomCpuPool", 1, 1);
 * // enqueue and add task to list -- w/o return type
 * poolWork.execute([] { std::print("Hello World from thread '{}'!\n", getThreadName()); }); // here: caller thread-name
 * poolWork.execute([](const auto &...args) { std::print(std::runtime_format("Hello World from thread '{}'!\n"), args...); }, getThreadName()); // here: executor thread-name
 * // [..]
 *
 * // pool for IO-bound (potentially blocking) tasks with at least 1 and a max of 1000 threads
 * opencmw::BasicThreadPool&lt;opencmw::IO_BOUND&gt;  poolIO("CustomIOPool", 1, 1000);
 * poolIO.keepAliveDuration = seconds(10);              // keeps idling threads alive for 10 seconds (optional)
 * poolIO.waitUntilInitialised();                       // wait until the pool is initialised (optional)
 * poolIO.setAffinityMask({ true, true, true, false }); // allows executor threads to run on the first four CPU cores
 *
 * constexpr auto           func1  = [](const auto &...args) { return std::format(std::runtime_format("thread '{1}' scheduled task '{0}'!\n"), getThreadName(), args...); };
 * std::future&lt;std::string&gt; result = poolIO.execute&lt;"customTaskName"&gt;(func1, getThreadName()); // N.B. the calling thread is owner of the std::future
 *
 * // execute a task with a name, a priority and single-core affinity (here: 2)
 * poolIO.execute&lt;"task name", 20U, 2&gt;([]() { std::print("Hello World from custom thread '{}'!\n", getThreadName()); });
 *
 * try {
 *     poolIO.execute&lt;"customName", 20U, 3&gt;([]() {  [..] this potentially long-running task is trackable via it's 'customName' thread name [..] });
 * } catch (const std::invalid_argument &e) {
 *     std::print("caught exception: {}\n", e.what());
 * }
 * @endcode
 */
template<TaskType taskType>
class BasicThreadPool {
    using Task      = thread_pool::detail::Task;
    using TaskQueue = thread_pool::detail::TaskQueue;
    static std::atomic<uint64_t> _globalPoolId;
    static std::atomic<uint64_t> _taskID;
    static std::string           generateName() { return std::format("BasicThreadPool#{}", _globalPoolId.fetch_add(1)); }

    std::atomic<bool>            _initialised = ATOMIC_FLAG_INIT;
    bool                         _shutdown    = false;

    std::condition_variable      _condition;
    std::atomic<std::size_t>     _numTaskedQueued = 0U; // cache for _taskQueue.size()
    std::atomic<std::size_t>     _numTasksRunning = 0U;
    TaskQueue                    _taskQueue;
    TaskQueue                    _recycledTasks;

    std::mutex                   _threadListMutex;
    std::atomic<std::size_t>     _numThreads = 0U;
    std::list<std::thread>       _threads;

    std::vector<bool>            _affinityMask;
    thread::Policy               _schedulingPolicy   = thread::Policy::OTHER;
    int                          _schedulingPriority = 0;

    const std::string            _poolName;
    const uint32_t               _minThreads;
    const uint32_t               _maxThreads;

public:
    std::chrono::microseconds sleepDuration     = 1ms;
    std::chrono::milliseconds keepAliveDuration = 10s;

    BasicThreadPool(const std::string_view &name = generateName(), uint32_t min = std::thread::hardware_concurrency(), uint32_t max = std::thread::hardware_concurrency())
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
        for (auto &t : _threads) {
            t.join();
        }
    }

    BasicThreadPool(const BasicThreadPool &)                     = delete;
    BasicThreadPool(BasicThreadPool &&)                          = delete;
    BasicThreadPool          &operator=(const BasicThreadPool &) = delete;
    BasicThreadPool          &operator=(BasicThreadPool &&)      = delete;

    [[nodiscard]] std::string poolName() const noexcept { return _poolName; }
    [[nodiscard]] uint32_t    minThreads() const noexcept { return _minThreads; };
    [[nodiscard]] uint32_t    maxThreads() const noexcept { return _maxThreads; };

    [[nodiscard]] std::size_t numThreads() const noexcept { return std::atomic_load_explicit(&_numThreads, std::memory_order_acquire); }
    [[nodiscard]] std::size_t numTasksRunning() const noexcept { return std::atomic_load_explicit(&_numTasksRunning, std::memory_order_acquire); }
    [[nodiscard]] std::size_t numTasksQueued() const { return std::atomic_load_explicit(&_numTaskedQueued, std::memory_order_acquire); }
    [[nodiscard]] std::size_t numTasksRecycled() const { return _recycledTasks.size(); }
    [[nodiscard]] bool        isInitialised() const { return _initialised.load(std::memory_order::acquire); }
    void                      waitUntilInitialised() const { _initialised.wait(false); }
    void                      requestShutdown() {
        _shutdown = true;
        _condition.notify_all();
        for (auto &t : _threads) {
            t.join();
        }
    }
    [[nodiscard]] bool isShutdown() const { return _shutdown; }

    //

    [[nodiscard]] std::vector<bool> getAffinityMask() const { return _affinityMask; }

    void                            setAffinityMask(const std::vector<bool> &threadAffinityMask) {
        _affinityMask.clear();
        std::copy(threadAffinityMask.begin(), threadAffinityMask.end(), std::back_inserter(_affinityMask));
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

    // TODO: Do we need support for cancellation?
    template<const basic_fixed_string taskName = "", uint32_t priority = 0, int32_t cpuID = -1, std::invocable Callable, typename... Args, typename R = std::invoke_result_t<Callable, Args...>>
        requires(std::is_same_v<R, void>)
    void execute(Callable &&func, Args &&...args) {
        static thread_local SpinWait spinWait;
        if constexpr (cpuID >= 0) {
            if (cpuID >= _affinityMask.size() || !_affinityMask[cpuID]) {
                throw std::invalid_argument(std::format("requested cpuID {} incompatible with set affinity mask({}): [{}]",
                        cpuID, _affinityMask.size(), opencmw::join(_affinityMask, ", ")));
            }
        }
        _numTaskedQueued.fetch_add(1U);

        _taskQueue.push(createTask<taskName, priority, cpuID>(FWD(func), FWD(args)...));
        _condition.notify_one();
        if constexpr (taskType == TaskType::IO_BOUND) {
            spinWait.spinOnce();
            spinWait.spinOnce();
            while (_taskQueue.size() > 0) {
                if (const auto nThreads = numThreads(); nThreads <= numTasksRunning() && nThreads <= _maxThreads) {
                    createWorkerThread();
                }
                _condition.notify_one();
                spinWait.spinOnce();
                spinWait.spinOnce();
            }
            spinWait.reset();
        }
    }

    template<const basic_fixed_string taskName = "", uint32_t priority = 0, int32_t cpuID = -1, std::invocable Callable, typename... Args, typename R = std::invoke_result_t<Callable, Args...>>
        requires(!std::is_same_v<R, void>)
    [[nodiscard]] std::future<R> execute(Callable &&func, Args &&...funcArgs) {
        if constexpr (cpuID >= 0) {
            if (cpuID >= _affinityMask.size() || !_affinityMask[cpuID]) {
                throw std::invalid_argument(std::format("cpuID {} is out of range [0,{}] or incompatible with set affinity mask [{}]",
                        cpuID, _affinityMask.size(), opencmw::join(_affinityMask, ",")));
            }
        }
        std::promise<R> promise;
        auto            result = promise.get_future();
        auto            lambda = [promise = std::move(promise), func = FWD(func), ... args = FWD(funcArgs)]() mutable {
            try {
                promise.set_value(func(args...));
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        };
        execute<taskName, priority, cpuID>(std::move(lambda));
        return result;
    }

private:
    void cleanupFinishedThreads() {
        std::scoped_lock lock(_threadListMutex);
        // TODO:
        // (C++Ref) A thread that has finished executing code, but has not yet been
        // joined is still considered an active thread of execution and is
        // therefore joinable.
        // std::erase_if(_threads, [](auto &thread) { return !thread.joinable(); });
    }

    void updateThreadConstraints() {
        std::scoped_lock lock(_threadListMutex);
        // std::erase_if(_threads, [](auto &thread) { return !thread.joinable(); });

        std::for_each(_threads.begin(), _threads.end(), [this, threadID = std::size_t{ 0 }](auto &thread) mutable { this->updateThreadConstraints(threadID++, thread); });
    }

    void updateThreadConstraints(const std::size_t threadID, std::thread &thread) const {
        thread::setThreadName(std::format("{}#{}", _poolName, threadID), thread);
        thread::setThreadSchedulingParameter(_schedulingPolicy, _schedulingPriority, thread);
        if (!_affinityMask.empty()) {
            if (taskType == TaskType::IO_BOUND) {
                thread::setThreadAffinity(_affinityMask);
                return;
            }
            const std::vector<bool> affinityMask = distributeThreadAffinityAcrossCores(_affinityMask, threadID);
            std::cout << std::format("{}#{} affinity mask: {}", _poolName, threadID, opencmw::join(affinityMask, ",")) << std::endl;
            thread::setThreadAffinity(affinityMask);
        }
    }

    std::vector<bool> distributeThreadAffinityAcrossCores(const std::vector<bool> &globalAffinityMask, const std::size_t threadID) const {
        if (globalAffinityMask.empty()) {
            return {};
        }
        std::vector<bool> affinityMask;
        std::size_t       coreCount = 0;
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
        std::thread      &thread   = _threads.emplace_back(&BasicThreadPool::worker, this);
        updateThreadConstraints(nThreads + 1, thread);
    }

    template<const basic_fixed_string taskName = "", uint32_t priority = 0, int32_t cpuID = -1, std::invocable Callable, typename... Args>
    auto createTask(Callable &&func, Args &&...funcArgs) {
        const auto getTask = [&recycledTasks = _recycledTasks](Callable &&f, Args &&...args) {
            auto extracted = recycledTasks.pop();
            if (extracted.empty()) {
                if constexpr (sizeof...(Args) == 0) {
                    extracted.push_front(Task{ .id = _taskID.fetch_add(1U) + 1U, .func = std::move(f) });
                } else {
                    extracted.push_front(Task{ .id = _taskID.fetch_add(1U) + 1U, .func = std::move(std::bind_front(FWD(f), FWD(args)...)) });
                }
            } else {
                auto &task = extracted.front();
                task.id    = _taskID.fetch_add(1U) + 1U;
                if constexpr (sizeof...(Args) == 0) {
                    task.func = std::move(f);
                } else {
                    task.func = std::move(std::bind_front(FWD(f), FWD(args)...));
                }
            }
            return extracted;
        };

        auto  taskContainer = getTask(FWD(func), FWD(funcArgs)...);
        auto &task          = taskContainer.front();

        if constexpr (!taskName.empty()) {
            task.name = taskName.c_str();
        }
        task.priority = priority;
        task.cpuID    = cpuID;

        return taskContainer;
    }

    TaskQueue::TaskContainer popTask() {
        auto result = _taskQueue.pop();
        if (!result.empty()) {
            _numTaskedQueued.fetch_sub(1U);
        }
        return result;
    }

    void worker() {
        constexpr uint32_t N_SPIN       = 1 << 8;
        uint32_t           noop_counter = 0;
        const auto         threadID     = _numThreads.fetch_add(1);
        std::mutex         mutex;
        std::unique_lock   lock(mutex);
        auto               lastUsed = std::chrono::steady_clock::now();
        if (numThreads() >= _minThreads) {
            std::atomic_store_explicit(&_initialised, true, std::memory_order_release);
            _initialised.notify_all();
        }
        _numThreads.notify_one();
        bool running = true;
        do {
            TaskQueue::TaskContainer currentTaskContainer = popTask();
            if (!currentTaskContainer.empty()) {
                assert(!currentTaskContainer.empty());
                auto &currentTask = currentTaskContainer.front();
                _numTasksRunning.fetch_add(1);
                bool nameSet = !(currentTask.name.empty());
                if (nameSet) {
                    thread::setThreadName(currentTask.name);
                }
                currentTask.func();
                // execute dependent children
                currentTask.reset();
                _recycledTasks.push(std::move(currentTaskContainer));
                _numTasksRunning.fetch_sub(1);
                if (nameSet) {
                    thread::setThreadName(std::format("{}#{}", _poolName, threadID));
                }
                lastUsed     = std::chrono::steady_clock::now();
                noop_counter = 0;
            } else if (++noop_counter > N_SPIN) [[unlikely]] {
                // perform some thread maintenance tasks before going to sleep
                noop_counter = noop_counter / 2;
                cleanupFinishedThreads();

                _condition.wait_for(lock, keepAliveDuration, [this] { return numTasksQueued() > 0 || isShutdown(); });
            }
            // check if this thread is to be kept
            auto timeDiffSinceLastUsed = std::chrono::steady_clock::now() - lastUsed;
            if (isShutdown()) {
                auto nThread = _numThreads.fetch_sub(1);
                _numThreads.notify_all();
                if (nThread == 1) { // cleanup last thread
                    _recycledTasks.clear();
                    _taskQueue.clear();
                }
                running = false;
            } else if (timeDiffSinceLastUsed > keepAliveDuration) { // decrease to the minimum of _minThreads in a thread safe way
                unsigned long nThreads = numThreads();
                while (nThreads > minThreads()) { // compare and swap loop
                    if (_numThreads.compare_exchange_weak(nThreads, nThreads - 1, std::memory_order_acq_rel)) {
                        _numThreads.notify_all();
                        if (nThreads == 1) { // cleanup last thread
                            _recycledTasks.clear();
                            _taskQueue.clear();
                        }
                        running = false;
                        break;
                    }
                }
            }
        } while (running);
    }
};
template<TaskType T>
inline std::atomic<uint64_t> BasicThreadPool<T>::_globalPoolId = 0U;
template<TaskType T>
inline std::atomic<uint64_t> BasicThreadPool<T>::_taskID = 0U;
static_assert(ThreadPool<BasicThreadPool<IO_BOUND>>);
static_assert(ThreadPool<BasicThreadPool<CPU_BOUND>>);

} // namespace opencmw

#endif // THREADPOOL_HPP
