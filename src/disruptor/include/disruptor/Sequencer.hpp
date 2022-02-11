#pragma once

#include <memory>
#include <ostream>
#include <utility>
#include <vector>

#include "EventPoller.hpp"
#include "ISequencer.hpp"
#include "ProcessingSequenceBarrier.hpp"
#include "Sequence.hpp"
#include "SequenceGroups.hpp"
#include "Util.hpp"
#include "WaitStrategy.hpp"

namespace opencmw::disruptor {

template<typename T>
class Sequencer : public ISequencer<T>, public std::enable_shared_from_this<Sequencer<T>> {
public:
    /**
     * Construct a Sequencer with the selected strategies.
     *
     * \param bufferSize
     * \param waitStrategy waitStrategy for those waiting on sequences.
     */
    Sequencer(std::int32_t bufferSize, std::shared_ptr<WaitStrategy> waitStrategy)
        : m_bufferSize(bufferSize)
        , m_waitStrategy(std::move(waitStrategy))
        , m_cursor(std::make_shared<Sequence>())
        , m_waitStrategyRef(*m_waitStrategy)
        , m_cursorRef(*m_cursor) {
        if (bufferSize < 1) {
            throw std::invalid_argument("bufferSize must not be less than 1"); // replace by constrained NTTP
        }

        if (!Util::isPowerOf2(bufferSize)) {
            throw std::invalid_argument("bufferSize must be a power of 2"); // replace by constrained NTTP
        }
    }

    /**
     * Create a ISequenceBarrier that gates on the the cursor and a list of Sequences
     *
     * \param sequencesToTrack
     *
     */
    std::shared_ptr<ISequenceBarrier> newBarrier(const std::vector<std::shared_ptr<ISequence>> &sequencesToTrack) override {
        return std::make_shared<ProcessingSequenceBarrier>(this->shared_from_this(), m_waitStrategy, m_cursor, sequencesToTrack);
    }

    /**
     * The capacity of the data structure to hold entries.
     */
    std::int32_t bufferSize() override {
        return m_bufferSize;
    }

    /**
     * Get the value of the cursor indicating the published sequence.
     */
    [[nodiscard]] std::int64_t cursor() const override {
        return m_cursorRef.value();
    }

    /**
     * Add the specified gating sequences to this instance of the Disruptor.  They will safely and atomically added to the list of gating sequences.
     *
     * \param gatingSequences The sequences to add.
     */
    void addGatingSequences(const std::vector<std::shared_ptr<ISequence>> &gatingSequences) override {
        SequenceGroups::addSequences(m_gatingSequences, *this, gatingSequences);
    }

    /**
     * Remove the specified sequence from this sequencer.
     *
     * \param sequence to be removed.
     * \returns true if this sequence was found, false otherwise.
     */
    bool removeGatingSequence(const std::shared_ptr<ISequence> &sequence) override {
        return SequenceGroups::removeSequence(m_gatingSequences, sequence);
    }

    /**
     * Get the minimum sequence value from all of the gating sequences added to this ringBuffer.
     *
     * \returns The minimum gating sequence or the cursor sequence if no sequences have been added.
     */
    std::int64_t getMinimumSequence() override {
        return Util::getMinimumSequence(m_gatingSequences, m_cursorRef.value());
    }

    /**
     * Creates an event poller for this sequence that will use the supplied data provider and gating sequences.
     *
     * \param provider The data source for users of this event poller
     * \param gatingSequences Sequence to be gated on.
     * \tparam T
     * \returns A poller that will gate on this ring buffer and the supplied sequences.
     */
    std::shared_ptr<EventPoller<T>> newPoller(const std::shared_ptr<IDataProvider<T>> &provider, const std::vector<std::shared_ptr<ISequence>> &gatingSequences) override {
        return EventPoller<T>::newInstance(provider, this->shared_from_this(), std::make_shared<Sequence>(), m_cursor, gatingSequences);
    }

    void writeDescriptionTo(std::ostream &stream) const override {
        stream << "WaitStrategy: { ";
        // stream << typeName<decltype(m_waitStrategy)>(); //xTODO: change
        stream << typeid(decltype(m_waitStrategy)).name();
        stream << " }, Cursor: { ";
        m_cursor->writeDescriptionTo(stream);
        stream << " }, GatingSequences: [ ";

        auto firstItem = true;
        for (auto &&sequence : m_gatingSequences) {
            if (firstItem)
                firstItem = false;
            else
                stream << ", ";
            stream << "{ ";
            sequence->writeDescriptionTo(stream);
            stream << " }";
        }

        stream << " ]";
    }

protected:
    /**
     * Volatile in the Java version => always use Volatile.Read/Write or Interlocked methods to access this field.
     */
    std::vector<std::shared_ptr<ISequence>> m_gatingSequences;

    std::int32_t                            m_bufferSize;
    std::shared_ptr<WaitStrategy>           m_waitStrategy;
    std::shared_ptr<Sequence>               m_cursor;
    WaitStrategy                           &m_waitStrategyRef;
    Sequence                               &m_cursorRef;
};

} // namespace opencmw::disruptor
