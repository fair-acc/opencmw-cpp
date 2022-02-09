#pragma once

#include <atomic>
#include <ostream>

#include "ISequence.hpp"

namespace opencmw::disruptor {

/**
 * Concurrent sequence class used for tracking the progress of the ring buffer and event processors. Support a number of concurrent operations including CAS and order writes.
 * Also attempts to be more efficient with regards to false sharing by adding padding around the volatile field.
 */
class Sequence : public ISequence {
private:
    char                      m_padding0[56] = {};
    std::atomic<std::int64_t> m_fieldsValue;
    char                      m_padding1[56] = {};

public:
    /**
     * Construct a new sequence counter that can be tracked across threads.
     *
     * \param initialValue initial value for the counter
     */
    explicit Sequence(std::int64_t initialValue = InitialCursorValue)
        : m_fieldsValue(initialValue) {}

    /**
     * Current sequence number
     */
    std::int64_t value() const override {
        return std::atomic_load_explicit(&m_fieldsValue, std::memory_order_acquire);
    }

    /**
     * Perform an ordered write of this sequence.  The intent is a Store/Store barrier between this write and any previous store.
     *
     * \param value The new value for the sequence.
     */
    void setValue(std::int64_t value) override {
        std::atomic_store_explicit(&m_fieldsValue, value, std::memory_order_release);
    }

    /**
     * Atomically set the value to the given updated value if the current value == the expected value.
     *
     * \param expectedSequence the expected value for the sequence
     * \param nextSequence the new value for the sequence
     * \returns true if successful. False return indicates that the actual value was not equal to the expected value.
     */
    bool compareAndSet(std::int64_t expectedSequence, std::int64_t nextSequence) override {
        return std::atomic_compare_exchange_strong(&m_fieldsValue, &expectedSequence, nextSequence);
    }

    /**
     * Increments the sequence and stores the result, as an atomic operation.
     *
     * \returns incremented sequence
     */
    std::int64_t incrementAndGet() override {
        return std::atomic_fetch_add(&m_fieldsValue, std::int64_t(1)) + 1;
    }

    /**
     * Increments the sequence and stores the result, as an atomic operation.
     *
     * \returns incremented sequence
     */
    std::int64_t addAndGet(std::int64_t value) override {
        return std::atomic_fetch_add(&m_fieldsValue, value) + value;
    }

    void writeDescriptionTo(std::ostream &stream) const override {
        stream << m_fieldsValue.load();
    }

    /**
     * Set to -1 as sequence starting point
     */
    static const std::int64_t InitialCursorValue = -1;
};

} // namespace opencmw::disruptor
