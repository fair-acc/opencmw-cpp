
#ifndef OPENCMW_MAJORDOMO_DEBUG_H
#define OPENCMW_MAJORDOMO_DEBUG_H

#include <ctime>
#include <iostream>
#include <mutex>

#include <fmt/format.h>

#ifdef EMSCRIPTEN
#include <emscripten/threading.h>
#endif

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




class DebugBeforeAfter {
public:
    DebugBeforeAfter(const char* file, int line, const char* function)
        : file_(file), line_(line), function_(function) {
        start_ = std::chrono::high_resolution_clock::now();
        std::cout << "Before: File: " << file_ << ", Line: " << line_ << ", Function: " << function_ << std::endl;
    }

    ~DebugBeforeAfter() {
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "After:  File: " << file_ << ", Line: " << line_ << ", Function: " << function_
                  << ", Duration: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count() << " ms" << std::endl;
    }

private:
    const char* file_;
    int line_;
    const char* function_;
    std::chrono::high_resolution_clock::time_point start_;
};

#ifdef NDEBUG
#define DEBUG_VARIABLES(...)
#define DEBUG_LOG(msg)
#define DEBUG_FINISH(msg)
#define DEBUG_LOG_EVERY_SECOND(msg)
#else

#define _DEBUG_PREFIXES_1 ""
#define _DEBUG_PREFIXES_2 ""

#ifdef EMSCRIPTEN
#define _DEBUG_EMSCRIPTEN_THREADS_ "(emscripten mainthread/mainruntimethread) (" <<  emscripten_is_main_runtime_thread() << "/" << emscripten_is_main_browser_thread() << ")"

#ifndef NDEBUGEMSCRIPTEN
#undef _DEBUG_PREFIXES_1
#define _DEBUG_PREFIXES_1 _DEBUG_EMSCRIPTEN_THREADS_
#endif
#endif // EMSCRIPTEN

#define _DEBUG_PREFIXES  "\t\t\t|" << __FILE__<<":"<<__LINE__ << " @ " << __PRETTY_FUNCTION__ << ": " << _DEBUG_PREFIXES_1 << _DEBUG_PREFIXES_2

#define DEBUG_VARIABLES(...) do { \
    std::cout << "(" << #__VA_ARGS__ << ") {"; logVars(__VA_ARGS__); std::cout << "}"; \
    std::cout << _DEBUG_PREFIXES << #__VA_ARGS__ << ": "; \
    std::cout << std::endl; \
} while(0) ;

#define DEBUG_LOG(msg) std::cout << msg << _DEBUG_PREFIXES << std::endl;

template<typename T>
void logVars(const T& val) {
    std::cout << val;
}

template<typename T, typename... Args>
void logVars(const T& val, const Args&... args) {
    std::cout << val << ", ";
    logVars(args...);
}

#define DEBUG_FINISH(expr) \
DEBUG_LOG(#expr)               \
expr;                       \
DEBUG_LOG("~" << #expr);

#define DEBUG_LOG_EVERY_SECOND(msg) \
    { \
        static auto lastTime = std::chrono::steady_clock::now(); \
        auto currentTime = std::chrono::steady_clock::now(); \
        if (std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime).count() >= 1) { \
            lastTime = currentTime; \
            DEBUG_LOG(msg); \
        } \
    }

#endif // NDEBUG




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
