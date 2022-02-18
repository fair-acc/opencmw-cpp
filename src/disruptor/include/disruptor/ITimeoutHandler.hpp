#pragma once

#include <cstdint>

namespace opencmw::disruptor {

class ITimeoutHandler {
public:
    virtual ~ITimeoutHandler()                    = default;

    virtual void onTimeout(std::int64_t sequence) = 0;
};

} // namespace opencmw::disruptor
