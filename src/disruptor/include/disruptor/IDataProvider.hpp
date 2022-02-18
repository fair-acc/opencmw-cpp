#pragma once

#include <cstdint>
#include <memory>

namespace opencmw::disruptor {

template<typename T>
class IDataProvider {
public:
    virtual ~IDataProvider()                           = default;

    virtual T &operator[](std::int64_t sequence) const = 0;
};

} // namespace opencmw::disruptor
