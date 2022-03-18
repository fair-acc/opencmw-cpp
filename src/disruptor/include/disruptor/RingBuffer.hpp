#pragma once

#include <cassert>
#include <concepts>
#include <functional>
#include <iostream>
#include <ostream>
#include <type_traits>

#include <opencmw.hpp>

#include "ClaimStrategy.hpp"
#include "DataProvider.hpp"
#include "Exception.hpp"
#include "ISequenceBarrier.hpp"
#include "WaitStrategy.hpp"

namespace opencmw::disruptor {

template<typename T, std::size_t SIZE, WaitStrategy WAIT_STRATEGY, template<std::size_t, typename> typename CLAIM_STRATEGY>
class EventPoller;

static const std::int32_t rbPad = 128 / sizeof(int *);
/**
 * Ring based store of reusable entries containing the data representing an event being exchanged between event publisher and event receiver (e.g. EventPoller).
 *
 * \tparam T implementation storing the data for sharing during exchange or parallel coordination of an event.
 */
template<typename T, std::size_t SIZE, WaitStrategy WAIT_STRATEGY, template<std::size_t, typename> typename CLAIM_STRATEGY = MultiThreadedStrategy>
requires opencmw::is_power2_v<SIZE>
class RingBuffer : public DataProvider<T>, public std::enable_shared_from_this<RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> {
    const uint8_t                           padding0[56]{}; // NOSONAR
    mutable std::array<T, SIZE + 2 * rbPad> _entries;       // N.B. includes extra padding in front and back
    static constexpr std::int32_t           _indexMask = SIZE - 1;
    const uint8_t                           padding1[44]{}; // NOSONAR
    alignas(kCacheLine) std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> _gatingSequences{ std::make_shared<std::vector<std::shared_ptr<Sequence>>>() };
    alignas(kCacheLine) std::shared_ptr<Sequence> _cursor                                           = std::make_shared<Sequence>();
    alignas(kCacheLine) std::shared_ptr<WAIT_STRATEGY> _waitStrategy                                = std::make_shared<WAIT_STRATEGY>();
    alignas(kCacheLine) mutable std::shared_ptr<CLAIM_STRATEGY<SIZE, WAIT_STRATEGY>> _claimStrategy = std::make_shared<CLAIM_STRATEGY<SIZE, WAIT_STRATEGY>>(*_cursor, *_waitStrategy);

public:
    [[nodiscard]] T &operator[](std::int64_t sequence) const override {
        return _entries[static_cast<std::size_t>(rbPad + (static_cast<std::int32_t>(sequence) & _indexMask))];
    }

    [[nodiscard]] constexpr std::int32_t                    bufferSize() const noexcept override { return SIZE; }
    [[nodiscard]] bool                                      hasAvailableCapacity(std::int32_t requiredCapacity) const noexcept override { return _claimStrategy->hasAvailableCapacity(*_gatingSequences, requiredCapacity, _cursor->value()); }
    [[nodiscard]] std::int64_t                              next(std::int32_t n_slots_to_claim = 1) noexcept override { return _claimStrategy->next(*_gatingSequences, n_slots_to_claim); }
    [[nodiscard]] std::int64_t                              tryNext(std::int32_t n_slots_to_claim = 1) override { return _claimStrategy->tryNext(*_gatingSequences, n_slots_to_claim); }
    [[nodiscard]] std::int64_t                              getRemainingCapacity() const noexcept override { return _claimStrategy->getRemainingCapacity(*_gatingSequences); }
    void                                                    publish(std::int64_t sequence) override { _claimStrategy->publish(sequence); }
    [[nodiscard]] forceinline bool                          isAvailable(std::int64_t sequence) const noexcept override { return _claimStrategy->isAvailable(sequence); }
    [[nodiscard]] std::int64_t                              getHighestPublishedSequence(std::int64_t nextSequence, std::int64_t availableSequence) const noexcept override { return _claimStrategy->getHighestPublishedSequence(nextSequence, availableSequence); }
    [[nodiscard]] std::int64_t                              cursor() const noexcept override { return _cursor->value(); }
    [[nodiscard]] bool                                      isPublished(std::int64_t sequence) const noexcept override { return isAvailable(sequence); }
    void                                                    addGatingSequences(const std::vector<std::shared_ptr<Sequence>> &gatingSequences) override { detail::addSequences(_gatingSequences, *_cursor, gatingSequences); }
    bool                                                    removeGatingSequence(const std::shared_ptr<Sequence> &sequence) override { return detail::removeSequence(_gatingSequences, sequence); }
    std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> getGatingSequences() const noexcept override { return _gatingSequences; }
    [[nodiscard]] std::int64_t                              getMinimumGatingSequence() const noexcept override { return detail::getMinimumSequence(*_gatingSequences, _cursor->value()); }
    [[nodiscard]] auto                                      claimStrategy() { return _claimStrategy; }
    [[nodiscard]] auto                                      waitStrategy() { return _waitStrategy; }
    [[nodiscard]] auto                                      cursorSequence() { return _cursor; }

    /**
     * Creates an event poller for this ring buffer gated on the supplied sequences.
     *
     * \param gatingSequences
     * \returns A poller that will gate on this ring buffer and the supplied sequences.
     */
    std::shared_ptr<EventPoller<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> newPoller(const std::vector<std::shared_ptr<Sequence>> &gatingSequences = {}) {
        if (gatingSequences.empty()) {
            return std::make_shared<EventPoller<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>>(this->shared_from_this(), std::make_shared<Sequence>(), std::vector{ _cursor });
        }
        return std::make_shared<EventPoller<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>>(this->shared_from_this(), std::make_shared<Sequence>(), gatingSequences);
    }

    /**
     * Implementations translate (write) data representations into events claimed from the RingBuffer<T>.
     * When publishing to the RingBuffer, provide an EventTranslator. The RingBuffer will select the next available event by sequence and provide
     * it to the EventTranslator(which should update the event), before publishing the sequence update.
     *
     * @param translator being invoked with <code> &lt;T& event, std::int64_t sequenceID&gt; </code>
     * @param args optional arguments that are being forwarded to the translator
     */
    template<std::invocable<T, std::int64_t> Translator, typename... Args>
    void publishEvent(Translator &&translator, Args &&...args) {
        auto sequence = next();
        translateAndPublish(std::forward<Translator>(translator), sequence, std::forward<Args>(args)...);
    }

    /**
     * Implementations translate (write) data representations into events claimed from the RingBuffer<T>.
     * When publishing to the RingBuffer, provide an EventTranslator. The RingBuffer will select the next available event by sequence and provide
     * it to the EventTranslator(which should update the event), before publishing the sequence update.
     *
     * @param translator being invoked with <code> &lt;T& event, std::int64_t sequenceID&gt; </code>
     * @param args optional arguments that are being forwarded to the translator
     * @return <code> true </code> on success, <code> false </code> otherwise
     */
    template<std::invocable<T, std::int64_t> Translator, typename... Args>
    bool tryPublishEvent(Translator &&translator, Args &&...args) noexcept {
        try {
            auto sequence = tryNext();
            translateAndPublish(std::forward<Translator>(translator), sequence, std::forward<Args>(args)...);
            return true;
        } catch (const NoCapacityException &) {
            return false;
        }
    }

private:
    template<typename... TArgs, std::invocable<T, std::int64_t, TArgs...> Translator>
    void translateAndPublish(Translator &&translator, std::int64_t sequence, const TArgs &...args) noexcept {
        try {
            std::invoke(std::forward<Translator>(translator), std::forward<T>((*this)[sequence]), sequence, args...);
        } catch (...) {
            // blindly catch all exceptions from the user supplied translator function (i.e. unrelated to the RingBuffer mechanics)
            // xTODO: evaluate if these should be thrown back to the user
        }
        publish(sequence);
    }
};

/**
 * Defines producer types to support creation of RingBuffer with correct sequencer and publisher.
 */
enum class ProducerType {
    /**
     * Create a RingBuffer with a single event publisher to the RingBuffer
     */
    Single,

    /**
     * Create a RingBuffer supporting multiple event publishers to the one RingBuffer
     */
    Multi
};

template<typename T, std::size_t SIZE, WaitStrategy WAIT_STRATEGY, ProducerType producerType = ProducerType::Multi>
static auto newRingBuffer() {
    if constexpr (producerType == ProducerType::Single) {
        return std::make_shared<RingBuffer<T, SIZE, WAIT_STRATEGY, SingleThreadedStrategy>>();
    } else {
        return std::make_shared<RingBuffer<T, SIZE, WAIT_STRATEGY, MultiThreadedStrategy>>();
    }
}

enum class PollState {
    Processing,
    Gating,
    Idle,
    UNKNOWN
};

template<typename T, std::size_t SIZE, WaitStrategy WAIT_STRATEGY, template<std::size_t, typename> typename CLAIM_STRATEGY = MultiThreadedStrategy>
class EventPoller {
    std::shared_ptr<RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> _dataProvider;
    std::shared_ptr<Sequence>                                           _sequence;
    std::vector<std::shared_ptr<Sequence>>                              _gatingSequences;
    std::int64_t                                                        _lastAvailableSequence = kInitialCursorValue;

public:
    EventPoller()                     = delete;
    EventPoller(const EventPoller &)  = delete;
    EventPoller(const EventPoller &&) = delete;
    void operator=(const EventPoller &) = delete;
    EventPoller(const std::shared_ptr<RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> &dataProvider,
            const std::shared_ptr<Sequence>                                               &sequence,
            const std::vector<std::shared_ptr<Sequence>>                                  &gatingSequences)
        : _dataProvider(dataProvider)
        , _sequence(sequence)
        , _gatingSequences(gatingSequences) {}

    /**
     * Polls for events using the given handler. <br>
     * <br>
     * This poller will continue to feed events to the given handler until known available events are consumed
     * or {std::invocable<T &, std::int64_t, bool> auto &&eventHandler} returns false. <br>
     * <br>
     * Note that it is possible for more events to become available while the current events
     * are being processed. A further call to this method will process such events.
     *
     * @param eventHandler the handler used to consume events
     * @return the state of the event poller after the poll is attempted
     * @throws Exception exceptions thrown from the event handler are propagated to the caller
     */
    template<std::invocable<T &, std::int64_t, bool> CallBack>
    PollState poll(CallBack &&eventHandler) {
        const auto currentSequence       = _sequence->value();
        auto       nextSequence          = currentSequence + 1;

        const auto lastAvailableSequence = _lastAvailableSequence;
        const bool shareSameSign         = ((nextSequence < 0) == (lastAvailableSequence < 0));
        const auto min                   = shareSameSign && lastAvailableSequence > nextSequence ? lastAvailableSequence : nextSequence;
        const auto availableSequence     = _dataProvider->getHighestPublishedSequence(min, detail::getMinimumSequence(_gatingSequences));
        _lastAvailableSequence           = availableSequence;

        if (nextSequence <= availableSequence) {
            auto processedSequence = currentSequence;

            try {
                bool processNextEvent = true;
                do {
                    auto &event = (*_dataProvider)[nextSequence];
                    if constexpr (std::is_invocable_r_v<bool, CallBack, T &, std::int64_t, bool>) {
                        processNextEvent = std::invoke(eventHandler, event, nextSequence, nextSequence == availableSequence);
                    } else if constexpr (std::is_invocable_r_v<void, CallBack, T &, std::int64_t, bool>) {
                        std::invoke(eventHandler, event, nextSequence, nextSequence == availableSequence);
                    } else {
                        static_assert(!std::is_same<T, T>::value && "function signature mismatch");
                    }
                    processedSequence = nextSequence;
                    nextSequence++;
                } while (processNextEvent && nextSequence <= availableSequence);
            } catch (...) {
                _sequence->setValue(processedSequence);
                throw std::current_exception();
            }

            _sequence->setValue(processedSequence);

            return PollState::Processing;
        }

        if (_dataProvider->cursor() >= nextSequence) {
            return PollState::Gating;
        }

        return PollState::Idle;
    }

    [[nodiscard]] std::shared_ptr<Sequence> sequence() const {
        return _sequence;
    };
};

} // namespace opencmw::disruptor

template<typename T, std::size_t SIZE, opencmw::disruptor::WaitStrategy WAIT_STRATEGY, template<std::size_t, opencmw::disruptor::WaitStrategy> typename CLAIM_STRATEGY>
struct fmt::formatter<opencmw::disruptor::RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const opencmw::disruptor::RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY> &ringBuffer, FormatContext &ctx) {
        std::stringstream stream;
        stream << fmt::format("RingBuffer<{}, {}> - WaitStrategy: {{ {} }}, Cursor: {}, GatingSequences: [ ",
                opencmw::typeName<T>, ringBuffer.bufferSize(), opencmw::typeName<WAIT_STRATEGY>, ringBuffer.cursor());

        auto firstItem = true;
        for (const auto &sequence : *ringBuffer.getGatingSequences()) {
            if (firstItem) {
                firstItem = false;
            } else {
                stream << ", ";
            }
            stream << fmt::format("{{ {} }}", sequence->value());
        }

        stream << " ]";
        return fmt::format_to(ctx.out(), "{}", stream.str());
    }
};

template<>
struct fmt::formatter<opencmw::disruptor::PollState> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(opencmw::disruptor::PollState const &value, FormatContext &ctx) {
        switch (value) {
        case opencmw::disruptor::PollState::Processing:
            return fmt::format_to(ctx.out(), "PollState::Processing");
        case opencmw::disruptor::PollState::Gating:
            return fmt::format_to(ctx.out(), "PollState::Gating");
        case opencmw::disruptor::PollState::Idle:
            return fmt::format_to(ctx.out(), "PollState::Idle");
        default:
            return fmt::format_to(ctx.out(), "PollState::UNKNOWN");
        }
        throw std::invalid_argument(fmt::format("unhandled PollState::{}", static_cast<int>(value)));
    }
};

namespace opencmw {

template<typename T, std::size_t SIZE, disruptor::WaitStrategy WAIT_STRATEGY, template<std::size_t, disruptor::WaitStrategy> typename CLAIM_STRATEGY>
std::ostream &operator<<(std::ostream &stream, const opencmw::disruptor::RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY> &ringBuffer) { return stream << fmt::format("{}", ringBuffer); }
std::ostream &operator<<(std::ostream &stream, opencmw::disruptor::PollState &pollState) { return stream << fmt::format("{}", pollState); }

} // namespace opencmw
