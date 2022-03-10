#pragma once

#include <algorithm>
#include <atomic>
#include <ostream>
#include <span>

#include <opencmw.hpp>

#include "ISequence.hpp"

namespace opencmw::disruptor {

static constexpr const std::size_t  kCacheLine          = 64;
static constexpr const std::size_t  kAtomicPadding      = (kCacheLine - sizeof(std::atomic<std::int64_t>)) / sizeof(std::int64_t);
static constexpr const std::int64_t kInitialCursorValue = -1L;

/**
 * Concurrent sequence class used for tracking the progress of the ring buffer and event processors. Support a number of concurrent operations including CAS and order writes.
 * Also attempts to be more efficient with regards to false sharing by adding padding around the volatile field.
 */
class Sequence : public ISequence {
    std::int64_t              _padding0[kAtomicPadding] = {}; // NOLINT
    std::atomic<std::int64_t> _fieldsValue;
    std::int64_t              _padding1[kAtomicPadding] = {}; // NOLINT

public:
    Sequence(const Sequence &)  = delete;
    Sequence(const Sequence &&) = delete;
    void operator=(const Sequence &) = delete;
    explicit Sequence(std::int64_t initialValue = kInitialCursorValue) noexcept
        : _fieldsValue(initialValue) {}

    [[nodiscard]] forceinline std::int64_t value() const noexcept override {
        return std::atomic_load_explicit(&_fieldsValue, std::memory_order_acquire);
    }

    forceinline void setValue(std::int64_t value) noexcept override {
        std::atomic_store_explicit(&_fieldsValue, value, std::memory_order_release);
    }

    [[nodiscard]] forceinline bool compareAndSet(std::int64_t expectedSequence, std::int64_t nextSequence) noexcept override {
        // atomically set the value to the given updated value if the current value == the expected value (true, otherwise folse).
        return std::atomic_compare_exchange_strong(&_fieldsValue, &expectedSequence, nextSequence);
    }

    [[nodiscard]] forceinline std::int64_t incrementAndGet() noexcept override {
        return std::atomic_fetch_add(&_fieldsValue, 1L) + 1L;
    }

    [[nodiscard]] forceinline std::int64_t addAndGet(std::int64_t value) noexcept override {
        return std::atomic_fetch_add(&_fieldsValue, value) + value;
    }

    forceinline void writeDescriptionTo(std::ostream &stream) const noexcept override {
        stream << _fieldsValue.load();
    }

    static const std::int64_t InitialCursorValue = -1;
};

} // namespace opencmw::disruptor
