#pragma once

#include <atomic>
#include <vector>

#include "FixedSequenceGroup.hpp"
#include "IHighestPublishedSequenceProvider.hpp"
#include "ISequenceBarrier.hpp"
#include "Sequence.hpp"
#include "WaitStrategy.hpp"

namespace opencmw::disruptor {

class IHighestPublishedSequenceProvider;
class ISequence;
class Sequence;

/**
 *
 * ISequenceBarrier handed out for gating IEventProcessor on a cursor sequence and optional dependent IEventProcessors, using the given WaitStrategy.
 */
class ProcessingSequenceBarrier : public ISequenceBarrier, public std::enable_shared_from_this<ProcessingSequenceBarrier> {
private:
    std::shared_ptr<WaitStrategy>                      _waitStrategy;
    std::shared_ptr<ISequence>                         _dependentSequence;
    std::shared_ptr<Sequence>                          _cursorSequence;
    std::shared_ptr<IHighestPublishedSequenceProvider> _sequenceProvider;

    WaitStrategy                                      &_waitStrategyRef;
    ISequence                                         &_dependentSequenceRef;
    Sequence                                          &_cursorSequenceRef;
    IHighestPublishedSequenceProvider                 &_sequenceProviderRef;

    bool                                               _alerted;

public:
    ProcessingSequenceBarrier(const std::shared_ptr<IHighestPublishedSequenceProvider> &sequenceProvider,
            const std::shared_ptr<WaitStrategy>                                        &waitStrategy,
            const std::shared_ptr<Sequence>                                            &cursorSequence,
            const std::vector<std::shared_ptr<ISequence>>                              &dependentSequences)
        : _waitStrategy(waitStrategy)
        , _dependentSequence(getDependentSequence(cursorSequence, dependentSequences))
        , _cursorSequence(cursorSequence)
        , _sequenceProvider(sequenceProvider)
        , _waitStrategyRef(*_waitStrategy)
        , _dependentSequenceRef(*_dependentSequence)
        , _cursorSequenceRef(*_cursorSequence)
        , _sequenceProviderRef(*_sequenceProvider) {
        _alerted = false;
    }

    std::int64_t waitFor(std::int64_t sequence) override {
        checkAlert();

        auto availableSequence = _waitStrategyRef.waitFor(sequence, *_cursorSequence, *_dependentSequence, *shared_from_this());

        if (availableSequence < sequence) {
            return availableSequence;
        }

        return _sequenceProviderRef.getHighestPublishedSequence(sequence, availableSequence);
    }

    std::int64_t cursor() override {
        return _dependentSequenceRef.value();
    }

    bool isAlerted() override {
        return _alerted;
    }

    void alert() override {
        _alerted = true;
        if constexpr (requires { _waitStrategyRef.signalAllWhenBlocking(); }) {
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
    static std::shared_ptr<ISequence> getDependentSequence(const std::shared_ptr<Sequence> &cursorSequence, const std::vector<std::shared_ptr<ISequence>> &dependentSequences) {
        if (dependentSequences.empty()) {
            return cursorSequence;
        }

        return std::make_shared<FixedSequenceGroup>(dependentSequences);
    }
};

} // namespace opencmw::disruptor
