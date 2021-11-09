
#ifndef OPENCMW_MAJORDOMO_DEBUG_H
#define OPENCMW_MAJORDOMO_DEBUG_H

#include <iostream>
#include <mutex>
#include <source_location>

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

#endif // include guard
