#pragma once

namespace opencmw::disruptor {

class IEventReleaser {
public:
    virtual ~IEventReleaser() = default;

    virtual void release()    = 0;
};

} // namespace opencmw::disruptor
