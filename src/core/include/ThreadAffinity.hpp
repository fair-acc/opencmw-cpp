#ifndef OPENCMW_CPP_THREADAFFINITY_HPP
#define OPENCMW_CPP_THREADAFFINITY_HPP

#include <algorithm>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
#include <thread>
#include <vector>

#if !defined(_WIN32) && (defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))) // UNIX-style OS
#include <unistd.h>
#ifdef _POSIX_VERSION // POSIX compliant
#include <pthread.h>
#include <sched.h>
#endif
#endif

namespace opencmw::thread {

constexpr size_t THREAD_MAX_NAME_LENGTH = 16;
constexpr int    THREAD_UNINITIALISED   = 1;
constexpr int    THREAD_ERROR_UNKNOWN   = 2;
constexpr int    THREAD_VALUE_RANGE     = 3;
constexpr int    THREAD_ERANGE          = 34;

class thread_exception : public std::error_category {
public:
    constexpr thread_exception()
        : std::error_category(){};

    const char *name() const noexcept { return "thread_exception"; };
    std::string message(int errorCode) const {
        switch (errorCode) {
        case THREAD_UNINITIALISED:
            return "thread uninitialised or user does not have the appropriate rights (ie. CAP_SYS_NICE capability)";
        case THREAD_ERROR_UNKNOWN:
            return "thread error code 2";
        case THREAD_ERANGE:
            return fmt::format("length of the string specified pointed to by name exceeds the allowed limit THREAD_MAX_NAME_LENGTH = '{}'", THREAD_MAX_NAME_LENGTH);
        case THREAD_VALUE_RANGE:
            return fmt::format("priority out of valid range for scheduling policy", THREAD_MAX_NAME_LENGTH);
        default:
            return fmt::format("unknown threading error code {}", errorCode);
        }
    };
};

template<class type>
concept thread_type = std::is_same<type, std::thread>::value || std::is_same<type, std::jthread>::value;

namespace detail{
#ifdef _POSIX_VERSION
    template<typename Tp, typename... Us>
    constexpr decltype(auto) firstElement(Tp && t, Us &&...) noexcept {
            return std::forward<Tp>(t);
}

inline constexpr pthread_t getPosixHandler(thread_type auto &...t) noexcept {
    if constexpr (sizeof...(t) > 0) {
        return firstElement(t...).native_handle();
    } else {
        return pthread_self();
    }
}

std::string getThreadName(const pthread_t &handle) {
    if (handle == 0U) {
        return "uninitialised thread";
    }
    char threadName[THREAD_MAX_NAME_LENGTH];
    if (int rc = pthread_getname_np(handle, threadName, THREAD_MAX_NAME_LENGTH) != 0) {
        throw std::system_error(rc, thread_exception(), fmt::format("getThreadName(thread_type)"));
    }
    return std::string{ threadName, std::min(strlen(threadName), THREAD_MAX_NAME_LENGTH) };
}

int getPid() { return getpid(); }
#else
    int detail::getPid(){ return 0;
}
#endif
} // namespace detail

std::string getProcessName(const int pid = detail::getPid()) {
#ifdef _POSIX_VERSION
    std::ifstream in(fmt::format("/proc/{}/comm", pid), std::ios::in);
    if (in.is_open()) {
        std::string fileContent;
        std::getline(in, fileContent, '\n');
        return fileContent;
    }
#endif
    return "unknown process name";
} // namespace detail

std::string getThreadName(thread_type auto &...thread) {
#ifdef _POSIX_VERSION
    const pthread_t handle = detail::getPosixHandler(thread...);
    if (handle == 0U) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("getThreadName(thread_type)"));
    }
    return detail::getThreadName(handle);
#else
    return "unknown thread name";
#endif
}

void setProcessName(const std::string_view &processName, int pid = detail::getPid()) {
#ifdef _POSIX_VERSION
    std::ofstream out(fmt::format("/proc/{}/comm", pid), std::ios::out);
    if (!out.is_open()) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("setProcessName({},{})", processName, pid));
    }
    out << std::string{ processName.cbegin(), std::min(15LU, processName.size()) };
    out.close();
#endif
}

void setThreadName(const std::string_view &threadName, thread_type auto &...thread) {
#ifdef _POSIX_VERSION
    const pthread_t handle = detail::getPosixHandler(thread...);
    if (handle == 0U) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("setThreadName({}, thread_type)", threadName, detail::getThreadName(handle)));
    }
    if (int rc = pthread_setname_np(handle, threadName.data()) < 0) {
        throw std::system_error(rc, thread_exception(), fmt::format("setThreadName({},{}) - error code '{}'", threadName, detail::getThreadName(handle), rc));
    }
#endif
}

namespace detail {
#ifdef _POSIX_VERSION
std::vector<bool> getAffinityMask(const cpu_set_t &cpuSet) {
    std::vector<bool> bitMask(std::min(sizeof(cpu_set_t), static_cast<size_t>(std::thread::hardware_concurrency())));
    for (size_t i = 0; i < bitMask.size(); i++) {
        bitMask[i] = CPU_ISSET(i, &cpuSet);
    }
    return bitMask;
}

template<class T>
requires requires(T value) { std::get<0>(value); }
constexpr cpu_set_t getAffinityMask(const T &threadMap) {
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    size_t nMax = std::min(threadMap.size(), static_cast<size_t>(std::thread::hardware_concurrency()));
    for (size_t i = 0; i < nMax; i++) {
        if (threadMap[i]) {
            CPU_SET(i, &cpuSet);
        } else {
            CPU_CLR(i, &cpuSet);
        }
    }
    return cpuSet;
}
#endif
} // namespace detail

std::vector<bool> getThreadAffinity(thread_type auto &...thread) {
#ifdef _POSIX_VERSION
    const pthread_t handle = detail::getPosixHandler(thread...);
    if (handle == 0U) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("getThreadAffinity(thread_type)"));
    }
    cpu_set_t cpuSet;
    if (int rc = pthread_getaffinity_np(handle, sizeof(cpu_set_t), &cpuSet) != 0) {
        throw std::system_error(rc, thread_exception(), fmt::format("getThreadAffinity({})", detail::getThreadName(handle)));
    }
    return detail::getAffinityMask(cpuSet);
#else
    return std::vector<bool>(std::thread::hardware_concurrency()); // cannot set affinity for non-posix threads
#endif
}

template<class T>
requires requires(T value) { std::get<0>(value); }
constexpr bool setThreadAffinity(const T &threadMap, thread_type auto &...thread) {
#ifdef _POSIX_VERSION
    const pthread_t handle = detail::getPosixHandler(thread...);
    if (handle == 0U) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("setThreadAffinity(std::vector<bool, {}> = {{{}}}, thread_type)", threadMap.size(), fmt::join(threadMap, ", ")));
    }
    cpu_set_t cpuSet = detail::getAffinityMask(threadMap);
    if (int rc = pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuSet) != 0) {
        throw std::system_error(rc, thread_exception(), fmt::format("setThreadAffinity(std::vector<bool, {}> = {{{}}}, {})", threadMap.size(), fmt::join(threadMap, ", "), detail::getThreadName(handle)));
    }
    return true;
#else
    return false;                                                  // cannot set affinity for non-posix threads
#endif
}

std::vector<bool> getProcessAffinity(const int pid = detail::getPid()) {
#ifdef _POSIX_VERSION
    if (pid <= 0) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("getProcessAffinity({}) -- invalid pid", pid));
    }
    cpu_set_t cpuSet;
    if (int rc = sched_getaffinity(pid, sizeof(cpu_set_t), &cpuSet) != 0) {
        throw std::system_error(rc, thread_exception(), fmt::format("getProcessAffinity(std::bitset<{}> = {}, thread_type)"));
    }
    return detail::getAffinityMask(cpuSet);
#else
    return std::vector<bool>(std::thread::hardware_concurrency()); // cannot set affinity for non-posix threads
#endif
}

template<class T>
requires requires(T value) { std::get<0>(value); }
constexpr bool setProcessAffinity(const T &threadMap, const int pid = detail::getPid()) {
#ifdef _POSIX_VERSION
    if (pid <= 0) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("setProcessAffinity(std::vector<bool, {}> = {{{}}}, {})", threadMap.size(), fmt::join(threadMap, ", "), pid));
    }
    cpu_set_t cpuSet = detail::getAffinityMask(threadMap);
    if (int rc = sched_setaffinity(pid, sizeof(cpu_set_t), &cpuSet) != 0) {
        throw std::system_error(rc, thread_exception(), fmt::format("setProcessAffinity(std::vector<bool, {}> = {{{}}}, {})", threadMap.size(), fmt::join(threadMap, ", "), pid));
    }

    return true;
#else
    return false;                                                  // cannot set affinity for non-posix threads
#endif
}
enum Policy {
    OTHER       = 0,
    FIFO        = 1,
    ROUND_ROBIN = 2
};

struct SchedulingParameter {
    Policy policy; // e.g. SCHED_OTHER, SCHED_RR, FSCHED_FIFO
    int    priority;
};

namespace detail {
#ifdef _POSIX_VERSION
Policy getEnumPolicy(const int policy) {
    switch (policy) {
    case SCHED_FIFO: return FIFO;
    case SCHED_RR: return ROUND_ROBIN;
    case SCHED_OTHER:
    default:
        return OTHER;
    }
}
#endif
} // namespace detail

struct SchedulingParameter getProcessSchedulingParameter(const int pid = detail::getPid()) {
#ifdef _POSIX_VERSION
    if (pid <= 0) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("getProcessSchedulingParameter({}) -- invalid pid", pid));
    }
    struct sched_param param;
    const int          policy = sched_getscheduler(pid);
    if (int rc = sched_getparam(pid, &param) != 0) {
        throw std::system_error(rc, thread_exception(), fmt::format("getProcessSchedulingParameter({}) - sched_getparam error", pid));
    }
    return SchedulingParameter{ .policy = detail::getEnumPolicy(policy), .priority = param.sched_priority };
#else
    return struct SchedulingParameter {};
#endif
}

void setProcessSchedulingParameter(Policy scheduler, int priority, const int pid = detail::getPid()) {
#ifdef _POSIX_VERSION
    if (pid <= 0) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("setProcessSchedulingParameter({}, {}, {}) -- invalid pid", scheduler, priority, pid));
    }
    const int minPriority = sched_get_priority_min(scheduler);
    const int maxPriority = sched_get_priority_max(scheduler);
    if (priority < minPriority || priority > maxPriority) {
        throw std::system_error(THREAD_VALUE_RANGE, thread_exception(), fmt::format("setProcessSchedulingParameter({}, {}, {}) -- requested priority out-of-range [{}, {}]", scheduler, priority, pid, minPriority, maxPriority));
    }
    struct sched_param param {
        .sched_priority = priority
    };
    if (int rc = sched_setscheduler(pid, scheduler, &param) != 0) {
        throw std::system_error(rc, thread_exception(), fmt::format("setProcessSchedulingParameter({}, {}, {}) - sched_setscheduler return code: {}", scheduler, priority, pid, rc));
    }
#endif
}

struct SchedulingParameter getThreadSchedulingParameter(thread_type auto &...thread) {
#ifdef _POSIX_VERSION
    const pthread_t handle = detail::getPosixHandler(thread...);
    if (handle == 0U) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("getThreadSchedulingParameter(thread_type) -- invalid thread"));
    }
    struct sched_param param;
    int                policy;
    if (int rc = pthread_getschedparam(handle, &policy, &param) != 0) {
        throw std::system_error(rc, thread_exception(), fmt::format("getThreadSchedulingParameter({}) - sched_getparam error", detail::getThreadName(handle)));
    }
    return SchedulingParameter{ .policy = detail::getEnumPolicy(policy), .priority = param.sched_priority };
#else
    return struct SchedulingParameter {};
#endif
}

void setThreadSchedulingParameter(Policy scheduler, int priority, thread_type auto &...thread) {
#ifdef _POSIX_VERSION
    const pthread_t handle = detail::getPosixHandler(thread...);
    if (handle == 0U) {
        throw std::system_error(THREAD_UNINITIALISED, thread_exception(), fmt::format("setThreadSchedulingParameter({}, {}, thread_type) -- invalid thread", scheduler, priority));
    }
    const int minPriority = sched_get_priority_min(scheduler);
    const int maxPriority = sched_get_priority_max(scheduler);
    if (priority < minPriority || priority > maxPriority) {
        throw std::system_error(THREAD_VALUE_RANGE, thread_exception(), fmt::format("setThreadSchedulingParameter({}, {}, {}) -- requested priority out-of-range [{}, {}]", scheduler, priority, detail::getThreadName(handle), minPriority, maxPriority));
    }
    struct sched_param param {
        .sched_priority = priority
    };
    if (int rc = pthread_setschedparam(handle, scheduler, &param) != 0) {
        throw std::system_error(rc, thread_exception(), fmt::format("setThreadSchedulingParameter({}, {}, {}) - pthread_setschedparam return code: {}", scheduler, priority, detail::getThreadName(handle), rc));
    }
#endif
}

} // namespace opencmw::thread

#endif // OPENCMW_CPP_THREADAFFINITY_HPP
