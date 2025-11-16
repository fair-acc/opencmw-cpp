#ifndef SEQUENCE_HPP
#define SEQUENCE_HPP

#include <algorithm>
#include <atomic>
#include <format>
#include <ostream>
#include <span>

#include <opencmw.hpp>

namespace opencmw::disruptor {

static constexpr const std::size_t  kCacheLine          = 64;
static constexpr const std::int64_t kInitialCursorValue = -1L;

/**
 * Concurrent sequence class used for tracking the progress of the ring buffer and event processors. Support a number of concurrent operations including CAS and order writes.
 * Also attempts to be more efficient with regards to false sharing by adding padding around the volatile field.
 */
class Sequence {
    alignas(kCacheLine) std::atomic<std::int64_t> _fieldsValue{};

public:
    Sequence(const Sequence &)       = delete;
    Sequence(const Sequence &&)      = delete;
    void operator=(const Sequence &) = delete;
    explicit Sequence(std::int64_t initialValue = kInitialCursorValue) noexcept
        : _fieldsValue(initialValue) {}

    [[nodiscard]] OPENCMW_FORCEINLINE std::int64_t value() const noexcept {
        return std::atomic_load_explicit(&_fieldsValue, std::memory_order_acquire);
    }

    OPENCMW_FORCEINLINE void setValue(std::int64_t value) noexcept {
        std::atomic_store_explicit(&_fieldsValue, value, std::memory_order_release);
    }

    [[nodiscard]] OPENCMW_FORCEINLINE bool compareAndSet(std::int64_t expectedSequence, std::int64_t nextSequence) noexcept {
        // atomically set the value to the given updated value if the current value == the expected value (true, otherwise folse).
        return std::atomic_compare_exchange_strong(&_fieldsValue, &expectedSequence, nextSequence);
    }

    [[nodiscard]] OPENCMW_FORCEINLINE std::int64_t incrementAndGet() noexcept {
        return std::atomic_fetch_add(&_fieldsValue, 1L) + 1L;
    }

    [[nodiscard]] OPENCMW_FORCEINLINE std::int64_t addAndGet(std::int64_t value) noexcept {
        return std::atomic_fetch_add(&_fieldsValue, value) + value;
    }
};

namespace detail {
/**
 * Get the minimum sequence from an array of Sequences.
 *
 * \param sequences sequences to compare.
 * \param minimum an initial default minimum.  If the array is empty this value will returned.
 * \returns the minimum sequence found or lon.MaxValue if the array is empty.
 */
inline std::int64_t getMinimumSequence(const std::vector<std::shared_ptr<Sequence>> &sequences, std::int64_t minimum = std::numeric_limits<std::int64_t>::max()) noexcept {
    if (sequences.empty()) {
        return minimum;
    }
#if not defined(_LIBCPP_VERSION)
    return std::min(minimum, std::ranges::min(sequences, std::less{}, [](const auto &sequence) noexcept { return sequence->value(); })->value());
#else
    std::vector<int64_t> v;
    v.reserve(sequences.size());
    for (auto &sequence : sequences) {
        v.push_back(sequence->value());
    }
    // std::for_each(sequences.begin(), sequences.end(), [v](const auto &sequence) noexcept { v.push_back(8); });
    auto min = std::min_element(v.begin(), v.end());
    return (*min < minimum) ? *min : minimum;
#endif
}

inline void addSequences(std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> &sequences, const Sequence &cursor, const std::vector<std::shared_ptr<Sequence>> &sequencesToAdd) {
    std::int64_t                                            cursorSequence;
    std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> updatedSequences;
    std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> currentSequences;

    do {
        currentSequences = std::atomic_load_explicit(&sequences, std::memory_order_acquire);
        updatedSequences = std::make_shared<std::vector<std::shared_ptr<Sequence>>>(currentSequences->size() + sequencesToAdd.size());

#if not defined(_LIBCPP_VERSION)
        std::ranges::copy(currentSequences->begin(), currentSequences->end(), updatedSequences->begin());
#else
        std::copy(currentSequences->begin(), currentSequences->end(), updatedSequences->begin());
#endif

        cursorSequence = cursor.value();

        auto index     = currentSequences->size();
        for (auto &&sequence : sequencesToAdd) {
            sequence->setValue(cursorSequence);
            (*updatedSequences)[index] = sequence;
            index++;
        }
    } while (!std::atomic_compare_exchange_weak(&sequences, &currentSequences, updatedSequences)); // xTODO: explicit memory order

    cursorSequence = cursor.value();

    for (auto &&sequence : sequencesToAdd) {
        sequence->setValue(cursorSequence);
    }
}

inline bool removeSequence(std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> &sequences, const std::shared_ptr<Sequence> &sequence) {
    std::uint32_t                                           numToRemove;
    std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> oldSequences;
    std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> newSequences;

    do {
        oldSequences = std::atomic_load_explicit(&sequences, std::memory_order_acquire);
#if not defined(_LIBCPP_VERSION)
        numToRemove = static_cast<std::uint32_t>(std::ranges::count_if(*oldSequences, [&sequence](const auto &value) { return value == sequence; })); // specifically uses identity
#else
        numToRemove = static_cast<std::uint32_t>(std::count_if((*oldSequences).begin(), (*oldSequences).end(), [&sequence](const auto &value) { return value == sequence; })); // specifically uses identity
#endif
        if (numToRemove == 0) {
            break;
        }

        auto oldSize = static_cast<std::uint32_t>(oldSequences->size());
        newSequences = std::make_shared<std::vector<std::shared_ptr<Sequence>>>(oldSize - numToRemove);

        for (auto i = 0U, pos = 0U; i < oldSize; ++i) {
            const auto &testSequence = (*oldSequences)[i];
            if (sequence != testSequence) {
                (*newSequences)[pos] = testSequence;
                pos++;
            }
        }
    } while (!std::atomic_compare_exchange_weak(&sequences, &oldSequences, newSequences));

    return numToRemove != 0;
}

} // namespace detail

} // namespace opencmw::disruptor

template<>
struct std::formatter<opencmw::disruptor::Sequence, char> {
    constexpr auto parse(std::format_parse_context &ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const opencmw::disruptor::Sequence &value, FormatContext &ctx) const {
        return std::format_to(ctx.out(), "{}", value.value());
    }
};

namespace opencmw {
inline std::ostream &operator<<(std::ostream &os, const opencmw::disruptor::Sequence &v) {
    return os << std::format("{}", v);
}
} // namespace opencmw

#endif // SEQUENCE_HPP
