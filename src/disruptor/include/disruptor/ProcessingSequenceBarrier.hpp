#pragma once

#include <atomic>
#include <vector>

#include "IHighestPublishedSequenceProvider.hpp"
#include "ISequenceBarrier.hpp"
#include "Sequence.hpp"
#include "WaitStrategy.hpp"

namespace opencmw::disruptor {

class IHighestPublishedSequenceProvider;
class Sequence;

/**
 *
 * ISequenceBarrier handed out for gating IEventProcessor on a cursor sequence and optional dependent IEventProcessors, using the given WaitStrategy.
 */
class ProcessingSequenceBarrier : public ISequenceBarrier, public std::enable_shared_from_this<ProcessingSequenceBarrier> {
    std::shared_ptr<WaitStrategy>                           _waitStrategy;
    std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> _dependentSequences{ std::make_shared<std::vector<std::shared_ptr<Sequence>>>() };
    std::shared_ptr<Sequence>                               _cursorSequence;
    std::shared_ptr<IHighestPublishedSequenceProvider>      _sequenceProvider;

    WaitStrategy                                           &_waitStrategyRef;
    IHighestPublishedSequenceProvider                      &_sequenceProviderRef;

    bool                                                    _alerted = false;

public:
    ProcessingSequenceBarrier(const std::shared_ptr<IHighestPublishedSequenceProvider> &sequenceProvider,
            const std::shared_ptr<WaitStrategy>                                        &waitStrategy,
            const std::shared_ptr<Sequence>                                            &cursorSequence,
            const std::vector<std::shared_ptr<Sequence>>                               &dependentSequences)
        : _waitStrategy(waitStrategy)
        , _dependentSequences(getDependentSequence(cursorSequence, dependentSequences))
        , _cursorSequence(cursorSequence)
        , _sequenceProvider(sequenceProvider)
        , _waitStrategyRef(*_waitStrategy)
        , _sequenceProviderRef(*_sequenceProvider) {}

    std::int64_t waitFor(std::int64_t sequence) override {
        checkAlert();

        auto availableSequence = _waitStrategyRef.waitFor(sequence, *_cursorSequence, *_dependentSequences, *shared_from_this());

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
    std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> getDependentSequence(const std::shared_ptr<Sequence> &cursor, const std::vector<std::shared_ptr<Sequence>> &dependentSequences) const noexcept {
        auto updatedSequences = std::make_shared<std::vector<std::shared_ptr<Sequence>>>();

        updatedSequences->resize(dependentSequences.size() + 1);
        (*updatedSequences)[0] = cursor;
        std::ranges::copy(dependentSequences.begin(), dependentSequences.end(), updatedSequences->begin() + 1);

        return updatedSequences;
    }
};

} // namespace opencmw::disruptor
