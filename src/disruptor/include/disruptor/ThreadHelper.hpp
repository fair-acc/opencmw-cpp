#pragma once

#include <bitset>
#include <string>

namespace opencmw::disruptor::thread_helper {

using AffinityMask = std::bitset<64>;

std::uint32_t getCurrentThreadId();
std::uint32_t getCurrentProcessor();

std::size_t   getProcessorCount();

bool          setThreadAffinity(const AffinityMask &mask);
AffinityMask  getThreadAffinity();

void          setThreadName(const std::string &name);
void          setThreadName(int threadId, const std::string &name);

} // namespace opencmw::disruptor::thread_helper

#define DISRUPTOR_OS_FAMILY_LINUX
#ifdef DISRUPTOR_OS_FAMILY_LINUX

#include <pthread.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace opencmw::disruptor::thread_helper {

inline std::size_t getProcessorCount() {
    return static_cast<size_t>(sysconf(_SC_NPROCESSORS_CONF));
}

inline uint32_t getCurrentProcessor() {
    return static_cast<uint32_t>(sched_getcpu());
}

inline bool setThreadAffinity(const AffinityMask &mask) {
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);

    for (size_t i = 0; i < mask.size(); ++i) {
        if (mask.test(i)) {
            CPU_SET(i, &cpuSet);
        }
    }

    return pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet) == 0;
}

inline AffinityMask getThreadAffinity() {
    AffinityMask mask;

    cpu_set_t    cpuSet;
    CPU_ZERO(&cpuSet);
    if (pthread_getaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet) == 0) {
        auto processorCount = getProcessorCount();
        auto maskSize       = mask.size();
        for (auto i = 0U; i < processorCount && i < maskSize; ++i) {
            if (CPU_ISSET(i, &cpuSet)) {
                mask.set(i);
            }
        }
    }

    return mask;
}

inline uint32_t getCurrentThreadId() {
    return static_cast<uint32_t>(syscall(SYS_gettid));
}

inline void setThreadName(const std::string &name) {
    prctl(PR_SET_NAME, name.c_str(), 0, 0, 0);
}

} // namespace opencmw::disruptor::thread_helper

#endif // DISRUPTOR_OS_FAMILY_UNIX

#ifdef DISRUPTOR_OS_FAMILY_MACOS

#include <cpuid.h>
#include <mach/mach_types.h>
#include <mach/thread_act.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#define SYSCTL_CORE_COUNT "machdep.cpu.core_count"

#define CPUID(INFO, LEAF, SUBLEAF) __cpuid_count(LEAF, SUBLEAF, INFO[0], INFO[1], INFO[2], INFO[3])

#define GETCPU(CPU) \
    { \
        uint32_t CPUInfo[4]; \
        CPUID(CPUInfo, 1, 0); \
        /* CPUInfo[1] is EBX, bits 24-31 are APIC ID */ \
        if ((CPUInfo[3] & (1 << 9)) == 0) { \
            (CPU) = -1; /* no APIC on chip */ \
        } else { \
            (CPU) = (unsigned) CPUInfo[1] >> 24; \
        } \
        if ((CPU) < 0) (CPU) = 0; \
    }

namespace opencmw::disruptor::thread_helper {
typedef struct cpu_set {
    uint32_t count;
} cpu_set_t;

static inline void CPU_ZERO(cpu_set_t *cs) { cs->count = 0; }

static inline void CPU_SET(int num, cpu_set_t *cs) { cs->count |= (1 << num); }

static inline int  CPU_ISSET(int num, cpu_set_t *cs) { return (cs->count & (1 << num)); }

inline int         sched_getaffinity(pid_t pid, size_t cpu_size, cpu_set_t *cpu_set) {
    (void) pid;
    (void) cpu_size;

    int32_t core_count = 0;
    size_t  len        = sizeof(core_count);
    int     ret        = sysctlbyname(SYSCTL_CORE_COUNT, &core_count, &len, 0, 0);
    if (ret) {
        return -1;
    }
    cpu_set->count = 0;
    for (int i = 0; i < core_count; i++) {
        cpu_set->count |= (1 << i);
    }

    return 0;
}

inline int pthread_setaffinity_np(pthread_t thread, size_t cpu_size,
        cpu_set_t *cpu_set) {
    thread_port_t mach_thread;
    int           core = 0;

    for (core = 0; core < 8 * (int) cpu_size; core++) {
        if (CPU_ISSET(core, cpu_set)) break;
    }

    thread_affinity_policy_data_t policy = { core };
    mach_thread                          = pthread_mach_thread_np(thread);
    thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
            (thread_policy_t) &policy, 1);
    return 0;
}

inline size_t getProcessorCount() {
    return static_cast<size_t>(sysconf(_SC_NPROCESSORS_CONF));
}

inline uint32_t getCurrentProcessor() {
    uint32_t cpu_id;

    GETCPU(cpu_id);

    return cpu_id;
}

inline bool setThreadAffinity(const AffinityMask &mask) {
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);

    for (size_t i = 0; i < mask.size(); ++i) {
        if (mask.test(i)) {
            CPU_SET(i, &cpuSet);
        }
    }

    return pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet) == 0;
}

inline AffinityMask getThreadAffinity() {
    AffinityMask mask;

    cpu_set_t    cpuSet;
    CPU_ZERO(&cpuSet);
    if (sched_getaffinity(0, sizeof(cpuSet), &cpuSet) == 0) {
        int processorCount = getProcessorCount();
        int maskSize       = (int) mask.size();
        for (int i = 0; i < processorCount && i < maskSize; ++i) {
            if (CPU_ISSET(i, &cpuSet)) {
                mask.set(i);
            }
        }
    }

    return mask;
}

inline uint32_t getCurrentThreadId() {
    uint64_t result;
    pthread_threadid_np(NULL, &result);
    return (uint32_t) result;
}

inline void setThreadName(const std::string &name) {
    pthread_setname_np(name.c_str());
}

} // namespace opencmw::disruptor::thread_helper

#endif // DISRUPTOR_OS_FAMILY_MACOS

#ifdef DISRUPTOR_OS_FAMILY_WINDOWS

namespace opencmw::disruptor::thread_helper {

inline std::uint32_t getCurrentThreadId() {
    return static_cast<std::uint32_t>(::GetCurrentThreadId());
}

inline std::uint32_t getCurrentProcessor() {
    return ::GetCurrentProcessorNumber();
}

inline std::size_t getProcessorCount() {
    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
    return systemInfo.dwNumberOfProcessors;
}

inline bool setThreadAffinity(const AffinityMask &mask) {
    return ::SetThreadAffinityMask(::GetCurrentThread(), mask.to_ullong()) != 0;
}

inline AffinityMask getThreadAffinity() {
    DWORD_PTR temp    = 1;
    DWORD_PTR current = ::SetThreadAffinityMask(::GetCurrentThread(), temp);
    if (current)
        ::SetThreadAffinityMask(::GetCurrentThread(), current);
    return current;
}

#if _DEBUG
#define MS_VC_EXCEPTION 0x406D1388

#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO {
    DWORD  dwType;     // Must be 0x1000.
    LPCSTR szName;     // Pointer to name (in user addr space).
    DWORD  dwThreadID; // Thread ID (-1=caller thread).
    DWORD  dwFlags;    // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

inline void setThreadName(const std::string &name) {
    setThreadName(-1, name);
}

inline void setThreadName(int threadId, const std::string &name) {
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    THREADNAME_INFO info;
    info.dwType     = 0x1000;
    info.szName     = name.c_str();
    info.dwThreadID = threadId;
    info.dwFlags    = 0;

    __try {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), reinterpret_cast<ULONG_PTR *>(&info));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
#else  // _DEBUG
inline void setThreadName(const std::string & /*name*/) {}
inline void setThreadName(int /*threadId*/, const std::string & /*name*/) {}
#endif // _DEBUG

} // namespace opencmw::disruptor::thread_helper

#endif // DISRUPTOR_OS_FAMILY_WINDOWS
