#pragma once

#include <atomic>
#include <memory>
#include <type_traits>

#include "IEventProcessor.hpp"
#include "ISequence.hpp"
#include "RingBuffer.hpp"

namespace opencmw::disruptor {

/**
 * No operation version of a IEventProcessor that simply tracks a Disruptor.Sequence. This is useful in tests or for pre-filling a RingBuffer<T> from a producer.
 */
template<typename T>
class NoOpEventProcessor : public IEventProcessor {
    static_assert(std::is_class<T>::value, "T should be a class");

private:
    class SequencerFollowingSequence;
    std::shared_ptr<SequencerFollowingSequence> _sequence;
    std::atomic<std::int32_t>                   _running;

public:
    /**
     * Construct a IEventProcessor that simply tracks a Disruptor.Sequence
     * .
     * \param sequencer sequencer to track.
     */
    explicit NoOpEventProcessor(const std::shared_ptr<RingBuffer<T>> &sequencer)
        : _sequence(std::make_shared<SequencerFollowingSequence>(sequencer)) {
    }

    /**
     * NoOp
     */
    void run() override {
        if (std::atomic_exchange(&_running, 1) != 0) {
            throw std::runtime_error("Thread is already running");
        }
    }

    /**
     *
     * See IEventProcessor::sequence()
     *
     */
    std::shared_ptr<ISequence> sequence() const override {
        return _sequence;
    }

    /**
     * NoOp
     */
    void halt() override {
        _running = 0;
    }

    /**
     *
     * See IEventProcessor::isRunning()
     *
     */
    bool isRunning() const override {
        return _running == 1;
    }
};

template<typename T>
class NoOpEventProcessor<T>::SequencerFollowingSequence : public ISequence {
private:
    std::shared_ptr<RingBuffer<T>> _sequencer;

public:
    explicit SequencerFollowingSequence(const std::shared_ptr<RingBuffer<T>> &sequencer)
        : _sequencer(sequencer) {
    }

    std::int64_t value() const override {
        return _sequencer->cursor();
    }

    void setValue(std::int64_t /*value*/) override {
    }

    bool compareAndSet(std::int64_t /*expectedSequence*/, std::int64_t /*nextSequence*/) override {
        return false;
    }

    std::int64_t incrementAndGet() override {
        return 0;
    }

    std::int64_t addAndGet(std::int64_t /*value*/) override {
        return 0;
    }

    void writeDescriptionTo(std::ostream &stream) const override {
        stream << "SequencerFollowingSequence";
    }
};

} // namespace opencmw::disruptor
