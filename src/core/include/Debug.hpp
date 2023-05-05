
#ifndef OPENCMW_MAJORDOMO_DEBUG_H
#define OPENCMW_MAJORDOMO_DEBUG_H

#include <ctime>
#include <filesystem>
#include <iostream>
#include <mutex>
#if defined(__clang__)
#include <experimental/source_location>
namespace std {
typedef std::experimental::source_location source_location;
}
#else
#include <source_location>
#endif

#include <fmt/format.h>

namespace opencmw::debug {

struct DebugImpl {
    bool _breakLineOnEnd = true;

    DebugImpl() {}

    ~DebugImpl() {
        if (_breakLineOnEnd) {
            operator<<('\n');
        }
    }

    template<typename T>
    DebugImpl &operator<<(T &&val) {
        // static std::mutex print_lock;
        // std::lock_guard   lock{ print_lock };
        std::cerr << std::forward<T>(val);
        return *this;
    }

    DebugImpl(const DebugImpl & /*unused*/) {
    }

    DebugImpl(DebugImpl &&other) noexcept {
        other._breakLineOnEnd = false;
    }
};

// TODO: Make a proper debug function
inline auto
log() {
    return DebugImpl{};
}

inline auto withLocation(const std::source_location location = std::source_location::current()) {
    std::error_code error;
    auto            relative = std::filesystem::relative(location.file_name(), error);
    return log() << (relative.string() /*location.file_name()*/) << ":" << location.line() << " in " << location.function_name() << " --> ";
}

} // namespace opencmw::debug

#define REQUIRE_MESSAGE(cond, msg) \
    do { \
        INFO(msg); \
        REQUIRE(cond); \
    } while ((void) 0, 0)

#define REQUIRE_NOTHROW_MESSAGE(cond, msg) \
    do { \
        INFO(msg); \
        REQUIRE_NOTHROW(cond); \
    } while ((void) 0, 0)

#define REQUIRE_THROWS_AS_MESSAGE(cond, exception, msg) \
    do { \
        INFO(msg); \
        REQUIRE_THROWS_AS(cond, exception); \
    } while ((void) 0, 0)

// #define OPENCMW_INSTRUMENT_ALLOC 1 //NOLINT -- used as a global macro to enable alloc/free profiling
namespace opencmw::debug {
static std::size_t           alloc{ 0 };   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static std::size_t           realloc{ 0 }; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static std::size_t           dealloc{ 0 }; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

[[maybe_unused]] static void resetStats() {
    alloc = realloc = dealloc = 0;
}

[[maybe_unused]] static void printAllocationStats(const char *message = "") {
    std::cout << fmt::format("{} - alloc-/re-/free: {} / {} / {}  diff: {}\n", message, alloc, realloc, dealloc, alloc - dealloc) << std::flush;
    // do not account for allocation due to fmt::format etc.
    alloc -= 1;
    dealloc -= 1;
}

#pragma clang diagnostic push
#pragma ide diagnostic   ignored "UnusedLocalVariable"
class Timer {
private:
    const char       *_message;
    const int         _alignMsg;
    const int         _alignClock;
    const int         _alignTime;
    const clock_t     _begin;
    const std::size_t _alloc;
    const std::size_t _realloc;
    const std::size_t _dealloc;

public:
    Timer() = delete;
    explicit Timer(const char *message, const int alignMsg = 20, const int alignClock = 10, const int alignTime = 5) noexcept
        : _message(message), _alignMsg(alignMsg), _alignClock(alignClock), _alignTime(alignTime), _begin(clock()), _alloc(alloc), _realloc(realloc), _dealloc(dealloc) {}
    Timer(const Timer &other) = delete;
    Timer(Timer &&other)      = delete;
    Timer &operator=(const Timer &other) = delete;
    Timer &operator=(Timer &&other) = delete;
    ~Timer() noexcept {
        const clock_t     diff            = clock() - _begin;
        const std::size_t allocation      = opencmw::debug::alloc - _alloc;
        const std::size_t deallocation    = opencmw::debug::dealloc - _dealloc;
        const std::size_t diffAllocations = allocation - deallocation;
        const std::size_t reallocs        = realloc - _realloc;
        const double      cpu_time_used   = static_cast<double>(1000 * diff) / CLOCKS_PER_SEC;
        if (diffAllocations == 0) {
#ifdef OPENCMW_INSTRUMENT_ALLOC
            std::cout << fmt::format("{:<{}}:{:>{}} clock cycles or {:>{}} ms - balanced memory (de-)allocation: {} - reallocs: {}\n",
                    _message, _alignMsg, diff, _alignClock, cpu_time_used, _alignTime, allocation, reallocs);
#else
            std::cout << fmt::format("{:<{}}:{:>{}} clock cycles or {:>{}} ms\n", _message, _alignMsg, diff, _alignClock, cpu_time_used, _alignTime);
#endif
            std::cout << std::flush;
        } else {
            std::cout << std::flush;
            std::cerr << fmt::format("{:<{}}:{:>{}} clock cycles or {:>{}} ms - memory-leak (de-)allocation: {} vs. {} - reallocs: {}\n",
                    _message, _alignMsg, diff, _alignClock, cpu_time_used, _alignTime, allocation, deallocation, reallocs);
            std::cerr << std::flush;
        }

        // do not account for allocation due to fmt::format etc.
        alloc -= 1;
        dealloc -= 1;
    }
};
#pragma clang diagnostic pop

} // namespace opencmw::debug

#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-macro-usage"
// TODO: make this a prettier overloaded namespace-global 'operator<<'
// option: put this and other similar things into 'opencmw::debug' namespace?
#define PRINT_VECTOR(a) \
    std::cout << #a << "[" << a.size() << "] = {"; \
    for (std::size_t i = 0; i < a.size(); i++) { \
        if (i != 0) \
            std::cout << ' '; \
        std::cout << a.at(i); \
    } \
    std::cout << '}' << std::endl // N.B. ';' missing on purpose
#pragma clang diagnostic pop

#ifdef OPENCMW_INSTRUMENT_ALLOC
/* #################################################################### */
/* # override malloc/realloc/free/new/delete for diagnostics purposes # */
/* #################################################################### */
#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-no-malloc"
#pragma ide diagnostic   ignored "cppcoreguidelines-owning-memory"
#pragma ide diagnostic   ignored "cppcoreguidelines-macro-usage"
#pragma ide diagnostic   ignored "misc-definitions-in-headers"
void                    *opencmw_malloc(size_t size) {
    opencmw::debug::alloc += 1;
    return std::malloc(size);
}
void *opencmw_realloc(void *ptr, size_t size) {
    if (ptr == nullptr) {
        opencmw::debug::alloc += 1;
    } else {
        opencmw::debug::realloc += 1;
    }
    return std::realloc(ptr, size);
}
void opencmw_free(void *ptr) {
    opencmw::debug::dealloc += 1;
    std::free(ptr);
}
#define malloc(X) opencmw_malloc(X)
#define free(X) opencmw_free(X)
#define realloc(X, Y) opencmw_realloc(X, Y)

void                    *operator new(std::size_t size) { return malloc(size); }
void                     operator delete(void *ptr) noexcept { free(ptr); }
void                     operator delete(void *ptr, std::size_t /*unused*/) noexcept { free(ptr); }
#pragma clang diagnostic pop
#endif // OPENCMW_INSTRUMENT_ALLOC

#endif // include guard
