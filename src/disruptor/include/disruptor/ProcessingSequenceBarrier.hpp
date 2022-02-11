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
    std::shared_ptr<WaitStrategy>                      m_waitStrategy;
    std::shared_ptr<ISequence>                         m_dependentSequence;
    std::shared_ptr<Sequence>                          m_cursorSequence;
    std::shared_ptr<IHighestPublishedSequenceProvider> m_sequenceProvider;

    WaitStrategy                                      &m_waitStrategyRef;
    ISequence                                         &m_dependentSequenceRef;
    Sequence                                          &m_cursorSequenceRef;
    IHighestPublishedSequenceProvider                 &m_sequenceProviderRef;

    bool                                               m_alerted;

public:
    ProcessingSequenceBarrier(const std::shared_ptr<IHighestPublishedSequenceProvider> &sequenceProvider,
            const std::shared_ptr<WaitStrategy>                                        &waitStrategy,
            const std::shared_ptr<Sequence>                                            &cursorSequence,
            const std::vector<std::shared_ptr<ISequence>>                              &dependentSequences)
        : m_waitStrategy(waitStrategy)
        , m_dependentSequence(getDependentSequence(cursorSequence, dependentSequences))
        , m_cursorSequence(cursorSequence)
        , m_sequenceProvider(sequenceProvider)
        , m_waitStrategyRef(*m_waitStrategy)
        , m_dependentSequenceRef(*m_dependentSequence)
        , m_cursorSequenceRef(*m_cursorSequence)
        , m_sequenceProviderRef(*m_sequenceProvider) {
        m_alerted = false;
    }

    std::int64_t waitFor(std::int64_t sequence) override {
        checkAlert();

        auto availableSequence = m_waitStrategyRef.waitFor(sequence, *m_cursorSequence, *m_dependentSequence, *shared_from_this());

        if (availableSequence < sequence) {
            return availableSequence;
        }

        return m_sequenceProviderRef.getHighestPublishedSequence(sequence, availableSequence);
    }

    std::int64_t cursor() override {
        return m_dependentSequenceRef.value();
    }

    bool isAlerted() override {
        return m_alerted;
    }

    void alert() override {
        m_alerted = true;
        if constexpr (requires { m_waitStrategyRef.signalAllWhenBlocking(); }) {
            m_waitStrategyRef.signalAllWhenBlocking();
        }
    }

    void clearAlert() override {
        m_alerted = false;
    }

    void checkAlert() override {
        if (m_alerted) {
            throw alert_exception();
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
