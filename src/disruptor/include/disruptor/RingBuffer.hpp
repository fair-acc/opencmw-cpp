#pragma once

#include <concepts>
#include <ostream>
#include <type_traits>

#include <fmt/format.h>

#include "Exception.hpp"
#include "ICursored.hpp"
#include "IEventSequencer.hpp"
#include "IEventTranslator.hpp"
#include "IEventTranslatorVararg.hpp"
#include "ISequenceBarrier.hpp"
#include "ISequencer.hpp"
#include "MultiProducerSequencer.hpp"
#include "ProducerType.hpp"
#include "SingleProducerSequencer.hpp"
#include "Util.hpp"
#include "WaitStrategy.hpp"

namespace opencmw::disruptor {

template<typename Container, typename T>
concept ContainsEventTranslators = requires(Container container) {
    { *(container.begin()) } -> std::derived_from<IEventTranslator<T>>;
};

/**
 * Ring based store of reusable entries containing the data representing an event being exchanged between event publisher and IEventProcessors.
 *
 * \tparam T implementation storing the data for sharing during exchange or parallel coordination of an event.
 */
template<typename T>
class RingBuffer : public IEventSequencer<T>, public ICursored, public std::enable_shared_from_this<RingBuffer<T>> {
    char                           padding0[56] = {};
    mutable std::vector<T>         _entries;
    std::int32_t                   _bufferSize{};
    std::int32_t                   _indexMask{};
    std::shared_ptr<ISequencer<T>> _sequencer;
    char                           padding1[40] = {};

    static const std::int32_t      _bufferPad   = 128 / sizeof(int *);

    template<typename... TItem>
    static std::int32_t getGreatestLength(const std::initializer_list<TItem> &...l) {
        const std::vector<std::size_t> lengths = { l.size()... };
        return lengths.empty() ? 0 : static_cast<std::int32_t>(*std::ranges::max_element(lengths.begin(), lengths.end()));
    }

public:
    /**
     * Construct a RingBuffer with the full option set.
     *
     * \param eventFactory eventFactory to create entries for filling the RingBuffer
     * \param sequencer waiting strategy employed by processorsToTrack waiting on entries becoming available.
     */
    explicit RingBuffer(const std::shared_ptr<ISequencer<T>> &sequencer)
        : _bufferSize(sequencer->bufferSize()), _indexMask(sequencer->bufferSize() - 1), _sequencer(sequencer) {
        if (_bufferSize < 1) {
            throw std::invalid_argument("bufferSize must not be less than 1"); // TODO: check with concept
        }

        if (util::ceilingNextPowerOfTwo(_bufferSize) != _bufferSize) {
            throw std::invalid_argument("bufferSize must be a power of 2"); // TODO: check with concept
        }

        _entries.resize(static_cast<std::size_t>(_bufferSize + 2 * _bufferPad));
    }

    explicit RingBuffer(ProducerType producerType, std::int32_t bufferSize, const std::shared_ptr<WaitStrategy> &waitStrategy)
        : _bufferSize(bufferSize), _indexMask(bufferSize - 1) {
        if (_bufferSize < 1) {
            throw std::invalid_argument("bufferSize must not be less than 1"); // TODO: check with concept
        }

        if (util::ceilingNextPowerOfTwo(_bufferSize) != _bufferSize) {
            throw std::invalid_argument("bufferSize must be a power of 2"); // TODO: check with concept
        }
        _entries.resize(static_cast<std::size_t>(_bufferSize + 2 * _bufferPad));

        switch (producerType) {
        case ProducerType::Single:
            _sequencer = std::make_shared<SingleProducerSequencer<T>>(bufferSize, waitStrategy);
            break;
        case ProducerType::Multi:
            _sequencer = std::make_shared<MultiProducerSequencer<T>>(bufferSize, waitStrategy);
            break;
        default:
            throw std::invalid_argument(fmt::format("invalid producer type: {}", producerType));
        }
    }

    /**
     * Get the event for a given sequence in the RingBuffer.
     *
     * \param sequence sequence for the event
     */
    T &operator[](std::int64_t sequence) const override {
        return _entries[static_cast<std::size_t>(_bufferPad + (static_cast<std::int32_t>(sequence) & _indexMask))];
    }

    std::int32_t bufferSize() override {
        return _bufferSize;
    }

    auto getSequencer() {
        return *_sequencer;
    }

    bool hasAvailableCapacity(std::int32_t requiredCapacity) override {
        return _sequencer->hasAvailableCapacity(requiredCapacity);
    }

    std::int64_t next(std::int32_t n_slots_to_claim = 1) override {
        return _sequencer->next(n_slots_to_claim);
    }

    std::int64_t tryNext(std::int32_t n_slots_to_claim = 1) override {
        return _sequencer->tryNext(n_slots_to_claim);
    }

    /**
     * Get the current cursor value for the ring buffer.  The actual value received will depend on the type of ISequencer that is being used.
     */
    std::int64_t cursor() const override {
        return _sequencer->cursor();
    }

    /**
     * Get the remaining capacity for this ringBuffer.
     *
     * \returns The number of slots remaining.
     */
    std::int64_t getRemainingCapacity() override {
        return _sequencer->getRemainingCapacity();
    }

    void publish(std::int64_t sequence) override {
        _sequencer->publish(sequence);
    }

    /**
     * Publish the specified sequences.  This action marks these particular messages as being available to be read.
     *
     * \param lo the lowest sequence number to be published
     * \param hi the highest sequence number to be published
     */
    void publish(std::int64_t lo, std::int64_t hi) override {
        _sequencer->publish(lo, hi);
    }

    bool isPublished(std::int64_t sequence) {
        return _sequencer->isAvailable(sequence);
    }

    void addGatingSequences(const std::vector<std::shared_ptr<ISequence>> &gatingSequences) {
        _sequencer->addGatingSequences(gatingSequences);
    }

    std::int64_t getMinimumGatingSequence() {
        return _sequencer->getMinimumSequence();
    }

    /**
     * Remove the specified sequence from this ringBuffer.
     *
     * \param sequence sequence to be removed.
     * \returns true if this sequence was found, false otherwise.
     */
    bool removeGatingSequence(const std::shared_ptr<ISequence> &sequence) {
        return _sequencer->removeGatingSequence(sequence);
    }

    /**
     * Create a new SequenceBarrier to be used by an EventProcessor to track which messages are available to be read from the ring buffer given a list of sequences to track.
     *
     * \param sequencesToTrack the additional sequences to track
     * \returns A sequence barrier that will track the specified sequences.
     */
    std::shared_ptr<ISequenceBarrier> newBarrier(const std::vector<std::shared_ptr<ISequence>> &sequencesToTrack = {}) {
        return _sequencer->newBarrier(sequencesToTrack);
    }

    /**
     * Creates an event poller for this ring buffer gated on the supplied sequences.
     *
     * \param gatingSequences
     * \returns A poller that will gate on this ring buffer and the supplied sequences.
     */
    std::shared_ptr<EventPoller<T>> newPoller(const std::vector<std::shared_ptr<ISequence>> &gatingSequences = {}) {
        return _sequencer->newPoller(this->shared_from_this(), gatingSequences);
    }

    template<std::derived_from<IEventTranslator<T>> TTranslator>
    void publishEvent(const std::shared_ptr<TTranslator> &translator) {
        auto sequence = _sequencer->next();
        translateAndPublish(translator, sequence);
    }

    template<std::derived_from<IEventTranslator<T>> TTranslator>
    bool tryPublishEvent(const std::shared_ptr<TTranslator> &translator) {
        try {
            auto sequence = _sequencer->tryNext();
            translateAndPublish(translator, sequence);
            return true;
        } catch (const NoCapacityException &) {
            return false;
        }
    }

    template<typename TTranslator, typename... TArgs>
    requires std::derived_from<TTranslator, IEventTranslatorVararg<T, TArgs...>>
    void publishEvent(const std::shared_ptr<TTranslator> &translator, const TArgs &...args) {
        auto sequence = _sequencer->next();
        translateAndPublish(translator, sequence, args...);
    }

    template<typename TTranslator, typename... TArgs>
    requires std::derived_from<TTranslator, IEventTranslatorVararg<T, TArgs...>>
    bool tryPublishEvent(const std::shared_ptr<TTranslator> &translator, const TArgs &...args) {
        try {
            auto sequence = _sequencer->tryNext();
            translateAndPublish(translator, sequence, args...);
            return true;
        } catch (const NoCapacityException &) {
            return false;
        }
    }

    template<ContainsEventTranslators<T> TTranslators>
    void publishEvents(const TTranslators &translators) {
        publishEvents(translators, 0, static_cast<std::int32_t>(translators.size()));
    }

    template<ContainsEventTranslators<T> TTranslators>
    void publishEvents(const TTranslators &translators, std::int32_t batchStartsAt, std::int32_t batchSize) {
        checkBounds(static_cast<std::int32_t>(translators.size()), batchStartsAt, batchSize);
        std::int64_t finalSequence = _sequencer->next(batchSize);
        translateAndPublishBatch(translators, batchStartsAt, batchSize, finalSequence);
    }

    template<ContainsEventTranslators<T> TTranslators>
    bool tryPublishEvents(const TTranslators &translators) {
        return tryPublishEvents(translators, 0, static_cast<std::int32_t>(translators.size()));
    }

    template<ContainsEventTranslators<T> TTranslators>
    bool tryPublishEvents(const TTranslators &translators, std::int32_t batchStartsAt, std::int32_t batchSize) {
        checkBounds(static_cast<std::int32_t>(translators.size()), batchStartsAt, batchSize);
        try {
            auto finalSequence = _sequencer->tryNext(batchSize);
            translateAndPublishBatch(translators, batchStartsAt, batchSize, finalSequence);
            return true;
        } catch (const NoCapacityException &) {
            return false;
        }
    }

    template<typename TTranslator, typename... TArgs>
    requires std::derived_from<TTranslator, IEventTranslatorVararg<T, TArgs...>>
    void publishEvents(const std::shared_ptr<TTranslator> &translator, const std::initializer_list<TArgs> &...args) {
        publishEvents(translator, 0, getGreatestLength(args...), args...);
    }

    template<typename TTranslator, typename... TArgs>
    requires std::derived_from<TTranslator, IEventTranslatorVararg<T, TArgs...>>
    void publishEvents(const std::shared_ptr<TTranslator> &translator, std::int32_t batchStartsAt, std::int32_t batchSize, const std::initializer_list<TArgs> &...args) {
        checkBounds(getGreatestLength(args...), batchStartsAt, batchSize);
        std::int64_t finalSequence = _sequencer->next(batchSize);
        translateAndPublishBatch(translator, batchStartsAt, batchSize, finalSequence, args...);
    }

    template<typename TTranslator, typename... TArgs>
    requires std::derived_from<TTranslator, IEventTranslatorVararg<T, TArgs...>>
    auto tryPublishEvents(const std::shared_ptr<TTranslator> &translator, const std::initializer_list<TArgs> &...args) {
        return tryPublishEvents(translator, 0, getGreatestLength(args...), args...);
    }

    template<typename TTranslator, typename... TArgs>
    requires std::derived_from<TTranslator, IEventTranslatorVararg<T, TArgs...>>
    auto tryPublishEvents(const std::shared_ptr<TTranslator> &translator, std::int32_t batchStartsAt, std::int32_t batchSize, const std::initializer_list<TArgs> &...args) {
        checkBounds(getGreatestLength(args...), batchStartsAt, batchSize);
        try {
            auto finalSequence = _sequencer->tryNext(batchSize);
            translateAndPublishBatch(translator, batchStartsAt, batchSize, finalSequence, args...);
            return true;
        } catch (const NoCapacityException &) {
            return false;
        }
    }

private:
    void checkBounds(std::int32_t argumentCount, std::int32_t batchStartsAt, std::int32_t batchSize) {
        checkBatchSizing(batchStartsAt, batchSize);
        batchOverRuns(argumentCount, batchStartsAt, batchSize);
    }

    void checkBatchSizing(std::int32_t batchStartsAt, std::int32_t batchSize) {
        if (batchStartsAt < 0 || batchSize < 0) {
            throw std::invalid_argument(fmt::format("Both batchStartsAt and batchSize must be positive but got: batchStartsAt {} and batchSize {}", batchStartsAt, batchSize));
        }

        if (batchSize > bufferSize()) {
            throw std::invalid_argument(fmt::format("The ring buffer cannot accommodate {} it only has space for {} entities.", batchSize, bufferSize()));
        }
    }

    static void batchOverRuns(std::int32_t argumentCount, std::int32_t batchStartsAt, std::int32_t batchSize) {
        if (batchStartsAt + batchSize > argumentCount) {
            throw std::invalid_argument(fmt::format("A batchSize of: {} with batchStartsAt of: {} will overrun the available number of arguments: {}", batchSize, batchStartsAt, (argumentCount - batchStartsAt)));
        }
    }

    template<std::derived_from<IEventTranslator<T>> TTranslator>
    void translateAndPublish(const std::shared_ptr<TTranslator> &translator, std::int64_t sequence) {
        try {
            translator->translateTo((*this)[sequence], sequence);
        } catch (...) {
        }

        _sequencer->publish(sequence);
    }

    template<std::derived_from<IEventTranslator<T>> TTranslator, typename... TArgs>
    void translateAndPublish(const std::shared_ptr<TTranslator> &translator, std::int64_t sequence, const TArgs &...args) {
        try {
            translator->translateTo((*this)[sequence], sequence, args...);
        } catch (...) {
        }

        _sequencer->publish(sequence);
    }

    template<ContainsEventTranslators<T> TTranslators>
    void translateAndPublishBatch(const TTranslators &translators, std::int32_t batchStartsAt, std::int32_t batchSize, std::int64_t finalSequence) {
        std::int64_t initialSequence = finalSequence - (batchSize - 1);
        try {
            auto sequence     = initialSequence;
            auto batchEndsAt  = batchStartsAt + batchSize;
            auto translatorIt = translators.begin() + batchStartsAt;
            for (std::int32_t i = batchStartsAt; i < batchEndsAt; ++i, ++sequence, ++translatorIt) {
                auto &translator = *translatorIt;
                translator->translateTo((*this)[sequence], sequence);
            }
        } catch (...) {
        }

        _sequencer->publish(initialSequence, finalSequence);
    }

    template<typename TTranslator, typename... TArgs>
    requires std::derived_from<TTranslator, IEventTranslatorVararg<T, TArgs...>>
    void translateAndPublishBatch(const std::shared_ptr<TTranslator> &translator,
            std::int32_t                                              batchStartsAt,
            std::int32_t                                              batchSize,
            std::int64_t                                              finalSequence,
            const std::initializer_list<TArgs> &...args) {
        std::int64_t initialSequence = finalSequence - (batchSize - 1);
        try {
            auto sequence    = initialSequence;
            auto batchEndsAt = batchStartsAt + batchSize;
            for (std::int32_t i = batchStartsAt; i < batchEndsAt; i++, sequence++) {
                translator->translateTo((*this)[sequence], sequence, *(args.begin() + i)...);
            }
        } catch (...) {
        }

        _sequencer->publish(initialSequence, finalSequence);
    }
};

} // namespace opencmw::disruptor

namespace opencmw {

template<typename T>
std::ostream &operator<<(std::ostream &stream, const opencmw::disruptor::RingBuffer<T> &ringBuffer) {
    return stream << fmt::format("RingBuffer: {{ {} }} \", Sequencer: {{ {} }}", ringBuffer.bufferSize(), ringBuffer.getSequencer());
}

} // namespace opencmw
