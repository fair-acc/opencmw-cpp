#pragma once

#include "IDataProvider.hpp"
#include "ISequenced.hpp"

namespace opencmw::disruptor {

template<typename T>
class IEventSequencer : public IDataProvider<T>, public ISequenced {
};

} // namespace opencmw::disruptor
