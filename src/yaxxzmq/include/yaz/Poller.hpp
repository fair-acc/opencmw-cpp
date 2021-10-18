#ifndef YAZ_POLLER_H
#define YAZ_POLLER_H

#include <array>
#include <cstddef>
#include <zmq.h>

#include "Result.hpp"

namespace yaz {

// A simple wrapper class for 0mq polling
template<std::size_t ItemCount>
class Poller {
public:
    explicit constexpr Poller() = default;

    constexpr auto &operator[](std::size_t index) {
        return _items[index];
    }

    constexpr const auto &operator[](std::size_t index) const {
        return _items[index];
    }

    [[nodiscard]] positive_or_errno<int> poll() {
        return positive_or_errno<int>{ zmq_poll(_items.data(), ItemCount, 0) };
    }

private:
    std::array<zmq_pollitem_t, ItemCount> _items{};
};
} // namespace yaz

#endif // include guard
