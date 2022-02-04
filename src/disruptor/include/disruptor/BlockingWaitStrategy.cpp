#include "BlockingWaitStrategy.hpp"
#include "stdafx.hpp"

#include <ostream>

#include "ISequenceBarrier.hpp"
#include "Sequence.hpp"

namespace opencmw::disruptor {

std::int64_t BlockingWaitStrategy::waitFor(std::int64_t sequence,
        Sequence                                       &cursor,
        ISequence                                      &dependentSequence,
        ISequenceBarrier                               &barrier) {
    if (cursor.value() < sequence) {
        std::unique_lock uniqueLock(m_gate);

        while (cursor.value() < sequence) {
            barrier.checkAlert();

            m_conditionVariable.wait(uniqueLock);
        }
    }

    std::int64_t availableSequence;
    while ((availableSequence = dependentSequence.value()) < sequence) {
        barrier.checkAlert();
    }

    return availableSequence;
}

void BlockingWaitStrategy::signalAllWhenBlocking() {
    std::unique_lock uniqueLock(m_gate);

    m_conditionVariable.notify_all();
}

void BlockingWaitStrategy::writeDescriptionTo(std::ostream &stream) const {
    stream << "BlockingWaitStrategy";
}

} // namespace opencmw::disruptor
