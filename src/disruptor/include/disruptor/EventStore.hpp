#pragma once

#include <cstdint>
#include <memory>

#include "Sequence.hpp"

namespace opencmw::disruptor {

/**
 * @brief The EventStore class is a thread-safe store of events.
 * This follows the event-sourcing programming paradigm as described in
 * <a href="https://martinfowler.com/eaaDev/EventSourcing.html">Event Sourcing</a>.
 *
 * @tparam T The type of event stored.
 */
template<typename T>
class EventStore {
public:
    virtual ~EventStore()                                                                                                                             = default;

    [[nodiscard]] virtual T                                        &operator[](std::int64_t sequence) const                                           = 0;

    [[nodiscard]] virtual constexpr std::int32_t                    bufferSize() const noexcept                                                       = 0;
    [[nodiscard]] virtual bool                                      hasAvailableCapacity(std::int32_t requiredCapacity) const noexcept                = 0;
    [[nodiscard]] virtual std::int64_t                              next(std::int32_t n_slots_to_claim = 1) noexcept                                  = 0;
    [[nodiscard]] virtual std::int64_t                              tryNext(std::int32_t n_slots_to_claim = 1)                                        = 0;
    [[nodiscard]] virtual std::int64_t                              getRemainingCapacity() const noexcept                                             = 0;
    virtual void                                                    publish(std::int64_t sequence)                                                    = 0;
    [[nodiscard]] virtual bool                                      isAvailable(std::int64_t sequence) const noexcept                                 = 0;
    [[nodiscard]] virtual std::int64_t                              cursor() const noexcept                                                           = 0;
    [[nodiscard]] virtual bool                                      isPublished(std::int64_t sequence) const noexcept                                 = 0;
    virtual void                                                    addGatingSequences(const std::vector<std::shared_ptr<Sequence>> &gatingSequences) = 0;
    virtual bool                                                    removeGatingSequence(const std::shared_ptr<Sequence> &sequence)                   = 0;
    virtual std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> getGatingSequences() const noexcept                                               = 0;
    [[nodiscard]] virtual std::int64_t                              getMinimumGatingSequence() const noexcept                                         = 0;

    /**
     * Get the highest sequence number that can be safely read from the ring buffer. The scan will range from nextSequence to availableSequence.
     * If there are no available values > nextSequence() the return value will be nextSequence - 1
     * To work correctly a consumer should pass a value that is 1 higher than the last sequence that was successfully processed.
     *
     * \param nextSequence The sequence to start scanning from.
     * \param availableSequence The sequence to scan to.
     * \returns The highest value that can be safely read, will be at least nextSequence - 1
     */
    [[nodiscard]] virtual std::int64_t getHighestPublishedSequence(std::int64_t nextSequence, std::int64_t availableSequence) const noexcept = 0;
};

} // namespace opencmw::disruptor
