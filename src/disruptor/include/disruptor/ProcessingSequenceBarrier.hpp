#pragma once

#include <atomic>
#include <vector>

#include "ISequenceBarrier.hpp"
#include "RingBuffer.hpp"
#include "Sequence.hpp"
#include "WaitStrategy.hpp"

namespace opencmw::disruptor {

/**
 *
 * ISequenceBarrier handed out for gating IEventProcessor on a cursor sequence and optional dependent IEventProcessors, using the given WaitStrategy.
 */
template<typename T, std::size_t SIZE, WaitStrategy WAIT_STRATEGY, template<std::size_t, typename> typename CLAIM_STRATEGY>
class ProcessingSequenceBarrier : public ISequenceBarrier, public std::enable_shared_from_this<ProcessingSequenceBarrier<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> {
    std::shared_ptr<WAIT_STRATEGY>                          _waitStrategy;
    std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> _dependentSequences{ std::make_shared<std::vector<std::shared_ptr<Sequence>>>() };
    std::shared_ptr<Sequence>                               _cursorSequence;
    std::shared_ptr<DataProvider<T>>                        _sequenceProvider;

    WAIT_STRATEGY                                          &_waitStrategyRef;
    EventStore<T>                                          &_sequenceProviderRef;

    bool                                                    _alerted = false;

public:
    ProcessingSequenceBarrier(const std::shared_ptr<DataProvider<T>> &sequenceProvider,
            const std::shared_ptr<WAIT_STRATEGY>                     &waitStrategy,
            const std::shared_ptr<Sequence>                          &cursorSequence,
            const std::vector<std::shared_ptr<Sequence>>             &dependentSequences)
        : _waitStrategy(waitStrategy)
        , _dependentSequences(getDependentSequence(cursorSequence, dependentSequences))
        , _cursorSequence(cursorSequence)
        , _sequenceProvider(sequenceProvider)
        , _waitStrategyRef(*_waitStrategy)
        , _sequenceProviderRef(*_sequenceProvider) {}

    std::int64_t waitFor(std::int64_t sequence) override {
        checkAlert();

        auto availableSequence = _waitStrategyRef.waitFor(sequence, *_cursorSequence, *_dependentSequences, *(this->shared_from_this()));

        if (availableSequence < sequence) {
            return availableSequence;
        }

        return _sequenceProviderRef.getHighestPublishedSequence(sequence, availableSequence);
    }

    std::int64_t cursor() override {
        return detail::getMinimumSequence(*_dependentSequences);
    }

    bool isAlerted() override {
        return _alerted;
    }

    void alert() override {
        _alerted = true;
        if constexpr (hasSignalAllWhenBlocking<WAIT_STRATEGY>) {
            _waitStrategyRef.signalAllWhenBlocking();
        }
    }

    void clearAlert() override {
        _alerted = false;
    }

    void checkAlert() override {
        if (_alerted) {
            throw AlertException();
        }
    }

private:
    std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> getDependentSequence(const std::shared_ptr<Sequence> &cursor, const std::vector<std::shared_ptr<Sequence>> &dependentSequences) const noexcept {
        auto updatedSequences = std::make_shared<std::vector<std::shared_ptr<Sequence>>>();

        updatedSequences->resize(dependentSequences.size() + 1);
        (*updatedSequences)[0] = cursor;
        std::ranges::copy(dependentSequences.begin(), dependentSequences.end(), updatedSequences->begin() + 1);

        return updatedSequences;
    }
};

/**
 * Create a new SequenceBarrier to be used by an EventProcessor to track which messages are available to be read from the ring buffer given a list of sequences to track.
 *
 * \param sequencesToTrack the additional sequences to track
 * \returns A sequence barrier that will track the specified sequences.
 */
template<typename T, std::size_t SIZE, WaitStrategy WAIT_STRATEGY, template<std::size_t, typename> typename CLAIM_STRATEGY>
[[nodiscard]] std::shared_ptr<ISequenceBarrier> newBarrier(const std::shared_ptr<RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> &ringBuffer, const std::vector<std::shared_ptr<Sequence>> &sequencesToTrack) {
    return std::make_shared<ProcessingSequenceBarrier<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>>(ringBuffer, ringBuffer->waitStrategy(), ringBuffer->cursorSequence(), sequencesToTrack);
}

} // namespace opencmw::disruptor
