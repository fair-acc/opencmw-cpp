#include "SpinWaitWaitStrategy.hpp"
#include "stdafx.hpp"

#include <ostream>

#include "ISequenceBarrier.hpp"
#include "Sequence.hpp"
#include "SpinWait.hpp"

namespace opencmw::disruptor {

std::int64_t SpinWaitWaitStrategy::waitFor(std::int64_t sequence,
        Sequence & /*cursor*/,
        ISequence        &dependentSequence,
        ISequenceBarrier &barrier) {
    std::int64_t availableSequence;

    SpinWait     spinWait;
    while ((availableSequence = dependentSequence.value()) < sequence) {
        barrier.checkAlert();
        spinWait.spinOnce();
    }

    return availableSequence;
}

void SpinWaitWaitStrategy::signalAllWhenBlocking() {
}

void SpinWaitWaitStrategy::writeDescriptionTo(std::ostream &stream) const {
    stream << "SpinWaitWaitStrategy";
}

} // namespace opencmw::disruptor
