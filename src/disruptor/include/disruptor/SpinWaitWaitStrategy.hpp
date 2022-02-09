#pragma once

#include <ostream>

#include "ISequenceBarrier.hpp"
#include "IWaitStrategy.hpp"
#include "Sequence.hpp"
#include "SpinWait.hpp"

namespace opencmw::disruptor {

/**
 * Spin strategy that uses a SpinWait for IEventProcessors waiting on a barrier.
 * This strategy is a good compromise between performance and CPU resource. Latency spikes can occur after quiet periods.
 */
class SpinWaitWaitStrategy : public IWaitStrategy {
public:
    /**
     * \see IWaitStrategy.waitFor()
     */
    std::int64_t waitFor(std::int64_t sequence,
            Sequence & /*cursor*/,
            ISequence        &dependentSequence,
            ISequenceBarrier &barrier) override {
        std::int64_t availableSequence;

        SpinWait     spinWait;
        while ((availableSequence = dependentSequence.value()) < sequence) {
            barrier.checkAlert();
            spinWait.spinOnce();
        }

        return availableSequence;
    }

    /**
     * \see IWaitStrategy.signalAllWhenBlocking()
     */
    void signalAllWhenBlocking() override {}
    void writeDescriptionTo(std::ostream &stream) const override { stream << "SpinWaitWaitStrategy"; }
};

} // namespace opencmw::disruptor
