#pragma once

#include <atomic>
#include <ostream>

#include <opencmw.hpp>

#include "ISequence.hpp"

namespace opencmw::disruptor {

/**
 * Concurrent sequence class used for tracking the progress of the ring buffer and event processors. Support a number of concurrent operations including CAS and order writes.
 * Also attempts to be more efficient with regards to false sharing by adding padding around the volatile field.
 */
class Sequence : public ISequence {
private:
    char                      _padding0[56] = {};
    std::atomic<std::int64_t> _fieldsValue;
    char                      _padding1[56] = {};

public:
    /**
     * Construct a new sequence counter that can be tracked across threads.
     *
     * \param initialValue initial value for the counter
     */
    explicit Sequence(std::int64_t initialValue = InitialCursorValue) noexcept
        : _fieldsValue(initialValue) {}

    /**
     * Current sequence number
     */
    forceinline std::int64_t value() const noexcept override {
        return std::atomic_load_explicit(&_fieldsValue, std::memory_order_acquire);
    }

    /**
     * Perform an ordered write of this sequence.  The intent is a Store/Store barrier between this write and any previous store.
     *
     * \param value The new value for the sequence.
     */
    forceinline void setValue(std::int64_t value) noexcept override {
        std::atomic_store_explicit(&_fieldsValue, value, std::memory_order_release);
    }

    /**
     * Atomically set the value to the given updated value if the current value == the expected value.
     *
     * \param expectedSequence the expected value for the sequence
     * \param nextSequence the new value for the sequence
     * \returns true if successful. False return indicates that the actual value was not equal to the expected value.
     */
    forceinline bool compareAndSet(std::int64_t expectedSequence, std::int64_t nextSequence) noexcept override {
        return std::atomic_compare_exchange_strong(&_fieldsValue, &expectedSequence, nextSequence);
    }

    /**
     * Increments the sequence and stores the result, as an atomic operation.
     *
     * \returns incremented sequence
     */
    forceinline std::int64_t incrementAndGet() noexcept override {
        return std::atomic_fetch_add(&_fieldsValue, std::int64_t(1)) + 1;
    }

    /**
     * Increments the sequence and stores the result, as an atomic operation.
     *
     * \returns incremented sequence
     */
    forceinline std::int64_t addAndGet(std::int64_t value) noexcept override {
        return std::atomic_fetch_add(&_fieldsValue, value) + value;
    }

    forceinline void writeDescriptionTo(std::ostream &stream) const noexcept override {
        stream << _fieldsValue.load();
    }

    static const std::int64_t InitialCursorValue = -1;
};

} // namespace opencmw::disruptor
