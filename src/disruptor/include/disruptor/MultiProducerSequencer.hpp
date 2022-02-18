#pragma once

#include <memory>

#include "Sequencer.hpp"
#include "SpinWait.hpp"
#include "Util.hpp"

namespace opencmw::disruptor {

/**
 * Coordinator for claiming sequences for access to a data structure while tracking dependent Sequences. Suitable for use for sequencing across multiple publisher threads.
 * Note on Sequencer.cursor:  With this sequencer the cursor value is updated after the call to Sequencer::next(), to determine the highest available sequence that can be read,
 * then getHighestPublishedSequence should be used.
 */
template<typename T>
class MultiProducerSequencer : public Sequencer<T> {
private:
    std::shared_ptr<Sequence> _gatingSequenceCache = std::make_shared<Sequence>();

    // availableBuffer tracks the state of each ringbuffer slot
    // see below for more details on the approach
    std::unique_ptr<std::int32_t[]> _availableBuffer;
    std::int32_t                    _indexMask;
    std::int32_t                    _indexShift;

public:
    MultiProducerSequencer(std::int32_t bufferSize, const std::shared_ptr<WaitStrategy> &waitStrategy)
        : Sequencer<T>(bufferSize, waitStrategy) {
        _availableBuffer = std::unique_ptr<int[]>(new int[bufferSize]);
        _indexMask       = bufferSize - 1;
        _indexShift      = util::log2(bufferSize);
        initializeAvailableBuffer();
    }

    /**
     * Has the buffer got capacity to allocate another sequence.  This is a concurrent method so the response should only be taken as an indication of available capacity.
     *
     * \param requiredCapacity requiredCapacity in the buffer
     * \returns true if the buffer has the capacity to allocate the next sequence otherwise false.
     */
    bool hasAvailableCapacity(std::int32_t requiredCapacity) override {
        return hasAvailableCapacity(this->_gatingSequences, requiredCapacity, this->_cursor->value());
    }

    /**
     * Claim a specific sequence when only one publisher is involved.
     *
     * \param sequence sequence to be claimed.
     */
    void claim(std::int64_t sequence) override {
        this->_cursor->setValue(sequence);
    }

    /**
     * Claim the next n_slots_to_claim events in sequence for publishing.  This is for batch event producing.  Using batch producing requires a little care and some math.
     * <code>
     *     int n_slots_to_claim = 10;
     *     long hi = sequencer.next(n_slots_to_claim);
     *     long lo = hi - (n_slots_to_claim - 1);
     *     for (long sequence = lo; sequence<hi; sequence++)
     *     {
     *         // Do work.
     *     }
     *     sequencer.publish(lo, hi);
     * </code>
     *
     * \param n_slots_to_claim the number of sequences to claim
     * \returns the highest claimed sequence value
     */
    std::int64_t next(std::int32_t n_slots_to_claim) override {
        if (n_slots_to_claim < 1) {
            throw std::out_of_range("n_slots_to_claim must be > 0");
        }

        std::int64_t current;
        std::int64_t next;

        SpinWait     spinWait;
        do {
            current                           = this->_cursor->value();
            next                              = current + n_slots_to_claim;

            std::int64_t wrapPoint            = next - this->_bufferSize;
            std::int64_t cachedGatingSequence = _gatingSequenceCache->value();

            if (wrapPoint > cachedGatingSequence || cachedGatingSequence > current) {
                std::int64_t gatingSequence = util::getMinimumSequence(this->_gatingSequences, current);

                if (wrapPoint > gatingSequence) {
                    if constexpr (requires { this->_waitStrategy->signalAllWhenBlocking(); }) {
                        this->_waitStrategy->signalAllWhenBlocking();
                    }
                    spinWait.spinOnce();
                    continue;
                }

                _gatingSequenceCache->setValue(gatingSequence);
            } else if (this->_cursor->compareAndSet(current, next)) {
                break;
            }
        } while (true);

        return next;
    }

    /**
     * Attempt to claim the next event in sequence for publishing.  Will return the number of the slot if there is at least n slots available.
     *
     * \param n
     * \param n the number of sequences to claim
     * \returns the claimed sequence value
     */
    std::int64_t tryNext(std::int32_t n_slots_to_claim = 1) override {
        if (n_slots_to_claim < 1) {
            throw std::out_of_range("n_slots_to_claim must be > 0");
        }

        std::int64_t current;
        std::int64_t next;

        do {
            current = this->_cursor->value();
            next    = current + n_slots_to_claim;

            if (!hasAvailableCapacity(this->_gatingSequences, n_slots_to_claim, current)) {
                throw NoCapacityException();
            }
        } while (!this->_cursor->compareAndSet(current, next));

        return next;
    }

    /**
     * Get the remaining capacity for this sequencer. return The number of slots remaining.
     */
    std::int64_t getRemainingCapacity() override {
        auto consumed = util::getMinimumSequence(this->_gatingSequences, this->_cursorRef.value());
        auto produced = this->_cursorRef.value();

        return this->bufferSize() - (produced - consumed);
    }

    /**
     * Publish an event and make it visible to IEventProcessors
     *
     * \param sequence sequence to be published
     */
    void publish(std::int64_t sequence) override {
        setAvailable(sequence);
        if constexpr (requires { this->_waitStrategy->signalAllWhenBlocking(); }) {
            this->_waitStrategyRef.signalAllWhenBlocking();
        }
    }

    /**
     * Publish an event and make it visible to IEventProcessors
     */
    void publish(std::int64_t lo, std::int64_t hi) override {
        for (std::int64_t l = lo; l <= hi; l++) {
            setAvailable(l);
        }
        if constexpr (requires { this->_waitStrategy->signalAllWhenBlocking(); }) {
            this->_waitStrategyRef.signalAllWhenBlocking();
        }
    }

    /**
     * Confirms if a sequence is published and the event is available for use; non-blocking.
     *
     * \param sequence sequence of the buffer to check
     * \returns true if the sequence is available for use, false if not
     */
    bool isAvailable(std::int64_t sequence) override {
        auto index = calculateIndex(sequence);
        auto flag  = calculateAvailabilityFlag(sequence);

        return _availableBuffer[static_cast<std::size_t>(index)] == flag;
    }

    /**
     * Get the highest sequence number that can be safely read from the ring buffer.  Depending on the implementation of the Sequencer this call may need to scan a number of values
     * in the Sequencer.  The scan will range from nextSequence to availableSequence.  If there are no available values > nextSequence the return value will be nextSequence - 1.
     * To work correctly a consumer should pass a value that it 1 higher than the last sequence that was successfully processed.
     *
     * \param lowerBound The sequence to start scanning from.
     * \param availableSequence The sequence to scan to.
     * \returns The highest value that can be safely read, will be at least\returns <code>nextSequence - 1</code>\returns .
     */
    std::int64_t getHighestPublishedSequence(std::int64_t lowerBound, std::int64_t availableSequence) override {
        for (std::int64_t sequence = lowerBound; sequence <= availableSequence; sequence++) {
            if (!isAvailable(sequence)) {
                return sequence - 1;
            }
        }

        return availableSequence;
    }

private:
    bool hasAvailableCapacity(const std::vector<std::shared_ptr<ISequence>> &gatingSequences, std::int32_t requiredCapacity, std::int64_t cursorValue) {
        auto wrapPoint            = (cursorValue + requiredCapacity) - this->_bufferSize;
        auto cachedGatingSequence = _gatingSequenceCache->value();

        if (wrapPoint > cachedGatingSequence || cachedGatingSequence > cursorValue) {
            auto minSequence = util::getMinimumSequence(gatingSequences, cursorValue);
            _gatingSequenceCache->setValue(minSequence);

            if (wrapPoint > minSequence) {
                return false;
            }
        }

        return true;
    }

    void initializeAvailableBuffer() {
        for (std::int32_t i = this->_bufferSize - 1; i != 0; i--) {
            setAvailableBufferValue(i, -1);
        }

        setAvailableBufferValue(0, -1);
    }

    void setAvailable(std::int64_t sequence) {
        setAvailableBufferValue(calculateIndex(sequence), calculateAvailabilityFlag(sequence));
    }

    void setAvailableBufferValue(std::int32_t index, std::int32_t flag) {
        _availableBuffer[static_cast<std::size_t>(index)] = flag;
    }

    std::int32_t calculateAvailabilityFlag(std::int64_t sequence) {
        return static_cast<std::int32_t>(static_cast<std::uint64_t>(sequence) >> _indexShift);
    }

    std::int32_t calculateIndex(std::int64_t sequence) {
        return static_cast<std::int32_t>(sequence) & _indexMask;
    }
};

} // namespace opencmw::disruptor
