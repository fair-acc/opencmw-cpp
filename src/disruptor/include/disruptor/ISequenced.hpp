#pragma once

#include <cstdint>

namespace opencmw::disruptor {

/**
 *
 */
class ISequenced {
public:
    virtual ~ISequenced() = default;

    /**
     * The capacity of the data structure to hold entries.
     */
    virtual std::int32_t bufferSize() = 0;

    /**
     * Has the buffer got capacity to allocate another sequence.  This is a concurrent method so the response should only be taken as an indication of available capacity.
     *
     * \param requiredCapacity requiredCapacity in the buffer
     * \returns true if the buffer has the capacity to allocate the next sequence otherwise false.
     */
    virtual bool hasAvailableCapacity(std::int32_t requiredCapacity) = 0;

    /**
     * Get the remaining capacity for this sequencer. return The number of slots remaining.
     */
    virtual std::int64_t getRemainingCapacity() = 0;

    /**
     * Claim the next n_slots_to_claim events in sequence for publishing.  This is for batch event producing. Using batch producing requires a little care and some math.
     * <code> int n_slots_to_claim = 10;
     *      long hi = sequencer.next(n_slots_to_claim);
     *      long lo = hi - (n_slots_to_claim - 1);
     *      for (long sequence = lo; sequence &lt;= hi; sequence++)
     *      {
     *      // Do work.
     *      }
     *      sequencer.publish(lo, hi);
     * </code>
     *
     * \param n_slots_to_claim the number of sequences to claim
     * \returns the highest claimed sequence value
     */
    virtual std::int64_t next(std::int32_t n_slots_to_claim) = 0;

    /**
     * Attempt to claim the next n_slots_to_claim events in sequence for publishing.  Will return the highest numbered slot if there is at least requiredCapacity slots available.
     * Have a look at Next for a description on how to use this method.
     *
     * \param n_slots_to_claim the number of sequences to claim
     * \returns the claimed sequence value
     */
    virtual std::int64_t tryNext(std::int32_t n_slots_to_claim) = 0;

    /**
     * Publishes a sequence. Call when the event has been filled.
     *
     * \param sequence
     */
    virtual void publish(std::int64_t sequence) = 0;

    /**
     * Batch publish sequences.  Called when all of the events have been filled.
     *
     * \param lo first sequence number to publish
     * \param hi last sequence number to publish
     */
    virtual void publish(std::int64_t lo, std::int64_t hi) = 0;
};

} // namespace opencmw::disruptor
