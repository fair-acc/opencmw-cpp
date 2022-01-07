
#ifndef OPENCMW_MAJORDOMO_DEBUG_H
#define OPENCMW_MAJORDOMO_DEBUG_H

#include <iostream>
#include <mutex>
#ifdef __clang__ // TODO: replace (source_location is part of C++20 but still "experimental" for clang
#include <experimental/source_location>
namespace std {
typedef std::experimental::source_location source_location;
}
#else
#include <source_location>
#endif

namespace opencmw {

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
        std::cerr << std::forward<T>(val) << ' ';
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
debug() {
    return DebugImpl{};
}

inline auto debugWithLocation(const std::source_location location = std::source_location::current()) {
    return debug() << location.file_name() << ":" << location.line() << " in " << location.function_name() << " --> ";
}

} // namespace opencmw

#endif // include guard
