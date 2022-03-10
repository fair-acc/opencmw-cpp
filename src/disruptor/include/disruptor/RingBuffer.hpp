#pragma once

#include <cassert>
#include <concepts>
#include <functional>
#include <iostream>
#include <ostream>
#include <type_traits>

#include <opencmw.hpp>

#include "Exception.hpp"
#include "ICursored.hpp"
#include "IEventSequencer.hpp"
#include "ISequenceBarrier.hpp"
#include "ProcessingSequenceBarrier.hpp"
#include "RingBuffer.hpp"
#include "SequenceGroups.hpp"
#include "Util.hpp"
#include "WaitStrategy.hpp"

namespace opencmw::disruptor {

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

template<typename T, std::size_t SIZE>
requires opencmw::is_power2_v<SIZE>
class RingBuffer;
template<typename T, std::size_t SIZE>
class EventPoller;
namespace detail {
template<typename T, std::size_t N>
class SingleProducerSequencer;
template<typename T, std::size_t N>
class MultiProducerSequencer;
} // namespace detail

/**
 * Coordinator for claiming sequences for access to a data structure while tracking dependent Sequences
 */
template<typename T, std::size_t SIZE>
class Sequencer : public ISequenced, public ICursored, public IHighestPublishedSequenceProvider {
public:
    /**
     * Confirms if a sequence is published and the event is available for use; non-blocking.
     *
     * \param sequence sequence of the buffer to check
     * \returns true if the sequence is available for use, false if not
     */
    virtual bool isAvailable(std::int64_t sequence) const = 0;

    /**
     * Add the specified gating sequences to this instance of the Disruptor.  They will safely and atomically added to the list of gating sequences.
     *
     * \param gatingSequences The sequences to add.
     */
    virtual void addGatingSequences(const std::vector<std::shared_ptr<ISequence>> &gatingSequences) = 0;

    /**
     * Remove the specified sequence from this sequencer.
     *
     * \param sequence to be removed.
     * \returns true if this sequence was found, false otherwise.
     */
    virtual bool removeGatingSequence(const std::shared_ptr<ISequence> &sequence) = 0;

    /**
     * Create a ISequenceBarrier that gates on the the cursor and a list of Sequences
     *
     * \param sequencesToTrack
     */
    virtual std::shared_ptr<ISequenceBarrier> newBarrier(const std::vector<std::shared_ptr<ISequence>> &sequencesToTrack) = 0;

    /**
     * Get the minimum sequence value from all of the gating sequences added to this ringBuffer.
     *
     * \returns The minimum gating sequence or the cursor sequence if no sequences have been added.
     */
    virtual std::int64_t                          getMinimumSequence()                                                                                                            = 0;

    virtual std::shared_ptr<EventPoller<T, SIZE>> newPoller(const std::shared_ptr<RingBuffer<T, SIZE>> &provider, const std::vector<std::shared_ptr<ISequence>> &gatingSequences) = 0;

    virtual void                                  writeDescriptionTo(std::ostream &stream) const                                                                                  = 0;
    virtual ~Sequencer()                                                                                                                                                          = default;
};

static const std::int32_t rbPad = 128 / sizeof(int *);
/**
 * Ring based store of reusable entries containing the data representing an event being exchanged between event publisher and IEventProcessors.
 *
 * \tparam T implementation storing the data for sharing during exchange or parallel coordination of an event.
 */
template<typename T, std::size_t SIZE>
requires opencmw::is_power2_v<SIZE>
class RingBuffer : public IEventSequencer<T>, public ICursored, public std::enable_shared_from_this<RingBuffer<T, SIZE>> {
    const uint8_t                           padding0[56]{}; // NOSONAR
    mutable std::array<T, SIZE + 2 * rbPad> _entries;       // N.B. includes extra padding in front and back
    static constexpr std::int32_t           _indexMask = SIZE - 1;
    std::shared_ptr<Sequencer<T, SIZE>>     _sequencer;
    const uint8_t                           padding1[44]{}; // NOSONAR

public:
    RingBuffer() = delete;
    explicit RingBuffer(ProducerType producerType, const std::shared_ptr<WaitStrategy> &waitStrategy) {
        switch (producerType) {
        case ProducerType::Single:
            _sequencer = std::make_shared<detail::SingleProducerSequencer<T, SIZE>>(waitStrategy);
            break;
        case ProducerType::Multi:
            _sequencer = std::make_shared<detail::MultiProducerSequencer<T, SIZE>>(waitStrategy);
            break;
        default:
            throw std::invalid_argument(fmt::format("invalid producer type: {}", producerType));
        }
    }

    [[nodiscard]] T &operator[](std::int64_t sequence) const override {
        return _entries[static_cast<std::size_t>(rbPad + (static_cast<std::int32_t>(sequence) & _indexMask))];
    }

    [[nodiscard]] constexpr std::int32_t bufferSize() const override { return SIZE; }
    [[nodiscard]] Sequencer<T, SIZE>    &getSequencer() const { return *_sequencer; }
    [[nodiscard]] bool                   hasAvailableCapacity(std::int32_t requiredCapacity) override { return _sequencer->hasAvailableCapacity(requiredCapacity); }
    std::int64_t                         next(std::int32_t n_slots_to_claim = 1) override { return _sequencer->next(n_slots_to_claim); }
    std::int64_t                         tryNext(std::int32_t n_slots_to_claim = 1) override { return _sequencer->tryNext(n_slots_to_claim); }
    [[nodiscard]] std::int64_t           cursor() const override { return _sequencer->cursor(); }
    [[nodiscard]] std::int64_t           getRemainingCapacity() override { return _sequencer->getRemainingCapacity(); }
    void                                 publish(std::int64_t sequence) override { _sequencer->publish(sequence); }
    bool                                 isPublished(std::int64_t sequence) { return _sequencer->isAvailable(sequence); }
    void                                 addGatingSequences(const std::vector<std::shared_ptr<ISequence>> &gatingSequences) { _sequencer->addGatingSequences(gatingSequences); }
    bool                                 removeGatingSequence(const std::shared_ptr<ISequence> &sequence) { return _sequencer->removeGatingSequence(sequence); }
    std::int64_t                         getMinimumGatingSequence() { return _sequencer->getMinimumSequence(); }

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
    std::shared_ptr<EventPoller<T, SIZE>> newPoller(const std::vector<std::shared_ptr<ISequence>> &gatingSequences = {}) {
        return _sequencer->newPoller(this->shared_from_this(), gatingSequences);
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
        auto sequence = _sequencer->next();
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
            auto sequence = _sequencer->tryNext();
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
        _sequencer->publish(sequence);
    }
};

template<typename T, std::size_t SIZE>
class SequencerBase : public Sequencer<T, SIZE>, public std::enable_shared_from_this<SequencerBase<T, SIZE>> {
protected:
    /**
     * Volatile in the Java version => always use Volatile.Read/Write or Interlocked methods to access this field.
     */
    std::vector<std::shared_ptr<ISequence>> _gatingSequences;
    std::shared_ptr<WaitStrategy>           _waitStrategy;
    std::shared_ptr<Sequence>               _cursor = std::make_shared<Sequence>();
    WaitStrategy                           &_waitStrategyRef;
    Sequence                               &_cursorRef;

public:
    explicit SequencerBase(std::shared_ptr<WaitStrategy> waitStrategy)
        : _waitStrategy(std::move(waitStrategy))
        , _waitStrategyRef(*_waitStrategy)
        , _cursorRef(*_cursor) {}

    [[nodiscard]] std::shared_ptr<ISequenceBarrier> newBarrier(const std::vector<std::shared_ptr<ISequence>> &sequencesToTrack) override {
        return std::make_shared<ProcessingSequenceBarrier>(this->shared_from_this(), _waitStrategy, _cursor, sequencesToTrack);
    }

    [[nodiscard]] std::int32_t            bufferSize() const override { return SIZE; }
    [[nodiscard]] std::int64_t            cursor() const override { return _cursorRef.value(); }
    void                                  addGatingSequences(const std::vector<std::shared_ptr<ISequence>> &gatingSequences) override { SequenceGroups::addSequences(_gatingSequences, *this, gatingSequences); }
    bool                                  removeGatingSequence(const std::shared_ptr<ISequence> &sequence) override { return SequenceGroups::removeSequence(_gatingSequences, sequence); }
    [[nodiscard]] std::int64_t            getMinimumSequence() override { return util::getMinimumSequence(_gatingSequences, _cursorRef.value()); }

    std::shared_ptr<EventPoller<T, SIZE>> newPoller(const std::shared_ptr<RingBuffer<T, SIZE>> &provider, const std::vector<std::shared_ptr<ISequence>> &gatingSequences) override {
        return EventPoller<T, SIZE>::newInstance(provider, this->shared_from_this(), std::make_shared<Sequence>(), _cursor, gatingSequences);
    }

    void writeDescriptionTo(std::ostream &stream) const override {
        stream << fmt::format("WaitStrategy: {{ {}, Cursor: ", typeName<std::remove_cvref_t<decltype(opencmw::unwrapPointer(_waitStrategy))>>);
        _cursor->writeDescriptionTo(stream);
        stream << ", GatingSequences: [ ";

        auto firstItem = true;
        for (auto &&sequence : _gatingSequences) {
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
};

namespace detail {

template<typename T, std::size_t SIZE>
class SingleProducerSequencer : public SequencerBase<T, SIZE> {
    struct Fields {
        const uint8_t padding0[56]{}; // NOSONAR
        std::int64_t  nextValue;
        std::int64_t  cachedValue;
        const uint8_t padding1[56]{}; // NOSONAR

        Fields(std::int64_t _nextValue = Sequence::InitialCursorValue, std::int64_t _cachedValue = Sequence::InitialCursorValue)
            : nextValue(_nextValue)
            , cachedValue(_cachedValue) {}
    };

protected:
    Fields _fields;

public:
    SingleProducerSequencer() = delete;
    explicit SingleProducerSequencer(const std::shared_ptr<WaitStrategy> &waitStrategy)
        : SequencerBase<T, SIZE>(waitStrategy) {}

    bool hasAvailableCapacity(const int requiredCapacity) override {
        const std::int64_t nextValue            = _fields.nextValue;
        const std::int64_t wrapPoint            = (nextValue + requiredCapacity) - static_cast<std::int64_t>(SIZE);
        const std::int64_t cachedGatingSequence = _fields.cachedValue;

        if (wrapPoint > cachedGatingSequence || cachedGatingSequence > nextValue) {
            auto minSequence    = util::getMinimumSequence(this->_gatingSequences, nextValue);
            _fields.cachedValue = minSequence;

            if (wrapPoint > minSequence) {
                return false;
            }
        }

        return true;
    }

    std::int64_t next(const std::int32_t n_slots_to_claim = 1) override {
        assert((n_slots_to_claim > 0 && n_slots_to_claim < static_cast<std::int32_t>(SIZE)) && "n_slots_to_claim must be > 0 and < bufferSize");

        auto nextValue            = _fields.nextValue;

        auto nextSequence         = nextValue + n_slots_to_claim;
        auto wrapPoint            = nextSequence - static_cast<std::int64_t>(SIZE);
        auto cachedGatingSequence = _fields.cachedValue;

        if (wrapPoint > cachedGatingSequence || cachedGatingSequence > nextValue) {
            this->_cursorRef.setValue(nextValue);

            SpinWait     spinWait;
            std::int64_t minSequence;
            while (wrapPoint > (minSequence = util::getMinimumSequence(this->_gatingSequences, nextValue))) {
                if constexpr (requires { this->_waitStrategyRef.signalAllWhenBlocking(); }) {
                    this->_waitStrategyRef.signalAllWhenBlocking();
                }
                spinWait.spinOnce();
            }

            _fields.cachedValue = minSequence;
        }

        _fields.nextValue = nextSequence;

        return nextSequence;
    }

    std::int64_t tryNext(const std::int32_t n_slots_to_claim) override {
        assert((n_slots_to_claim > 0) && "n_slots_to_claim must be > 0");

        if (!hasAvailableCapacity(n_slots_to_claim)) {
            throw NoCapacityException();
        }

        const auto nextSequence = _fields.nextValue + n_slots_to_claim;
        _fields.nextValue       = nextSequence;

        return nextSequence;
    }

    std::int64_t getRemainingCapacity() override {
        const auto nextValue = _fields.nextValue;
        const auto consumed  = util::getMinimumSequence(this->_gatingSequences, nextValue);
        const auto produced  = nextValue;

        return this->bufferSize() - (produced - consumed);
    }

    void publish(std::int64_t sequence) override {
        this->_cursorRef.setValue(sequence);
        if constexpr (requires { this->_waitStrategyRef.signalAllWhenBlocking(); }) {
            this->_waitStrategyRef.signalAllWhenBlocking();
        }
    }

    forceinline bool isAvailable(std::int64_t sequence) const noexcept override { return sequence <= this->_cursorRef.value(); }
    std::int64_t     getHighestPublishedSequence(std::int64_t /*nextSequence*/, std::int64_t availableSequence) override { return availableSequence; }
};

/**
 * Coordinator for claiming sequences for access to a data structure while tracking dependent Sequences. Suitable for use for sequencing across multiple publisher threads.
 * Note on SequencerBase.cursor:  With this sequencer the cursor value is updated after the call to SequencerBase::next(), to determine the highest available sequence that can be read,
 * then getHighestPublishedSequence should be used.
 */
template<typename T, std::size_t SIZE>
class MultiProducerSequencer : public SequencerBase<T, SIZE> {
    std::shared_ptr<Sequence> _gatingSequenceCache = std::make_shared<Sequence>();

    // availableBuffer tracks the state of each ringbuffer slot see below for more details on the approach
    std::array<std::int32_t, SIZE> _availableBuffer;
    static constexpr std::int32_t  _indexMask  = SIZE - 1;
    static constexpr std::int32_t  _indexShift = ceillog2(SIZE);

public:
    MultiProducerSequencer() = delete;
    explicit MultiProducerSequencer(const std::shared_ptr<WaitStrategy> &waitStrategy)
        : SequencerBase<T, SIZE>(waitStrategy) {
        for (std::size_t i = SIZE - 1; i != 0; i--) {
            setAvailableBufferValue(i, -1);
        }
        setAvailableBufferValue(0, -1);
    }

    bool hasAvailableCapacity(std::int32_t requiredCapacity) override {
        return hasAvailableCapacity(this->_gatingSequences, requiredCapacity, this->_cursorRef.value());
    }

    std::int64_t next(std::int32_t n_slots_to_claim) override {
        if (n_slots_to_claim < 1) {
            throw std::out_of_range("n_slots_to_claim must be > 0");
        }

        std::int64_t current;
        std::int64_t next;

        SpinWait     spinWait;
        do {
            current                           = this->_cursorRef.value();
            next                              = current + n_slots_to_claim;

            std::int64_t wrapPoint            = next - static_cast<std::int64_t>(SIZE);
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
            } else if (this->_cursorRef.compareAndSet(current, next)) {
                break;
            }
        } while (true);

        return next;
    }

    std::int64_t tryNext(std::int32_t n_slots_to_claim = 1) override {
        assert((n_slots_to_claim > 0) && "n_slots_to_claim must be > 0");

        std::int64_t current;
        std::int64_t next;

        do {
            current = this->_cursorRef.value();
            next    = current + n_slots_to_claim;

            if (!hasAvailableCapacity(this->_gatingSequences, n_slots_to_claim, current)) {
                throw NoCapacityException();
            }
        } while (!this->_cursorRef.compareAndSet(current, next));

        return next;
    }

    std::int64_t getRemainingCapacity() override {
        auto consumed = util::getMinimumSequence(this->_gatingSequences, this->_cursorRef.value());
        auto produced = this->_cursorRef.value();

        return this->bufferSize() - (produced - consumed);
    }

    void publish(std::int64_t sequence) override {
        setAvailable(sequence);
        if constexpr (requires { this->_waitStrategy->signalAllWhenBlocking(); }) {
            this->_waitStrategyRef.signalAllWhenBlocking();
        }
    }

    forceinline bool isAvailable(std::int64_t sequence) const noexcept override {
        const auto index = calculateIndex(sequence);
        const auto flag  = calculateAvailabilityFlag(sequence);

        return _availableBuffer[static_cast<std::size_t>(index)] == flag;
    }

    forceinline std::int64_t getHighestPublishedSequence(const std::int64_t lowerBound, const std::int64_t availableSequence) noexcept override {
        for (std::int64_t sequence = lowerBound; sequence <= availableSequence; sequence++) {
            if (!isAvailable(sequence)) {
                return sequence - 1;
            }
        }

        return availableSequence;
    }

private:
    [[nodiscard]] bool hasAvailableCapacity(const std::vector<std::shared_ptr<ISequence>> &gatingSequences, std::int32_t requiredCapacity, std::int64_t cursorValue) const noexcept {
        const auto wrapPoint = (cursorValue + requiredCapacity) - static_cast<std::int64_t>(SIZE);

        if (const auto cachedGatingSequence = _gatingSequenceCache->value(); wrapPoint > cachedGatingSequence || cachedGatingSequence > cursorValue) {
            const auto minSequence = util::getMinimumSequence(gatingSequences, cursorValue);
            _gatingSequenceCache->setValue(minSequence);

            if (wrapPoint > minSequence) {
                return false;
            }
        }

        return true;
    }

    void             setAvailable(std::int64_t sequence) noexcept { setAvailableBufferValue(calculateIndex(sequence), calculateAvailabilityFlag(sequence)); }
    forceinline void setAvailableBufferValue(std::size_t index, std::int32_t flag) noexcept { _availableBuffer[index] = flag; }
    forceinline std::int32_t calculateAvailabilityFlag(const std::int64_t sequence) const noexcept { return static_cast<std::int32_t>(static_cast<std::uint64_t>(sequence) >> _indexShift); }
    forceinline std::size_t calculateIndex(const std::int64_t sequence) const noexcept { return static_cast<std::size_t>(static_cast<std::int32_t>(sequence) & _indexMask); }
};

} // namespace detail

enum class PollState {
    Processing,
    Gating,
    Idle,
    UNKNOWN
};

template<typename T, std::size_t SIZE>
class EventPoller {
    std::shared_ptr<RingBuffer<T, SIZE>> _dataProvider;
    std::shared_ptr<Sequencer<T, SIZE>>  _sequencer;
    std::shared_ptr<ISequence>           _sequence;
    std::shared_ptr<ISequence>           _gatingSequence;
    std::int64_t                         _lastAvailableSequence = Sequence::InitialCursorValue;

public:
    EventPoller(const std::shared_ptr<RingBuffer<T, SIZE>> &dataProvider,
            const std::shared_ptr<Sequencer<T, SIZE>>      &sequencer,
            const std::shared_ptr<ISequence>               &sequence,
            const std::shared_ptr<ISequence>               &gatingSequence)
        : _dataProvider(dataProvider)
        , _sequencer(sequencer)
        , _sequence(sequence)
        , _gatingSequence(gatingSequence) {}

    static std::shared_ptr<EventPoller<T, SIZE>> newInstance(const std::shared_ptr<RingBuffer<T, SIZE>> &dataProvider,
            const std::shared_ptr<Sequencer<T, SIZE>>                                                   &sequencer,
            const std::shared_ptr<ISequence>                                                            &sequence,
            const std::shared_ptr<ISequence>                                                            &cursorSequence,
            const std::vector<std::shared_ptr<ISequence>>                                               &gatingSequences) {
        std::shared_ptr<ISequence> gatingSequence;

        if (gatingSequences.empty()) {
            gatingSequence = cursorSequence;
        } else if (gatingSequences.size() == 1) {
            gatingSequence = *gatingSequences.begin();
        } else {
            gatingSequence = std::make_shared<FixedSequenceGroup>(gatingSequences);
        }

        return std::make_shared<EventPoller<T, SIZE>>(dataProvider, sequencer, sequence, gatingSequence);
    }

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
        const auto availableSequence     = _sequencer->getHighestPublishedSequence(min, _gatingSequence->value());
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

        if (_sequencer->cursor() >= nextSequence) {
            return PollState::Gating;
        }

        return PollState::Idle;
    }

    std::shared_ptr<ISequence> sequence() const {
        return _sequence;
    };
};

} // namespace opencmw::disruptor

template<>
struct fmt::formatter<opencmw::disruptor::ProducerType> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(opencmw::disruptor::ProducerType const &value, FormatContext &ctx) {
        if (value == opencmw::disruptor::ProducerType::Single) {
            return fmt::format_to(ctx.out(), "ProducerType::Single");
        } else if (value == opencmw::disruptor::ProducerType::Multi) {
            return fmt::format_to(ctx.out(), "ProducerType::Multi");
        }
        throw std::invalid_argument(fmt::format("unhandled ProducerType::{}", static_cast<int>(value)));
    }
};

template<typename T, std::size_t SIZE>
struct fmt::formatter<opencmw::disruptor::RingBuffer<T, SIZE>> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const opencmw::disruptor::RingBuffer<T, SIZE> &ringBuffer, FormatContext &ctx) {
        std::stringstream stream;
        ringBuffer.getSequencer().writeDescriptionTo(stream);
        return fmt::format_to(ctx.out(), "RingBuffer<{}, {}> - SequencerBase: {{ {} }}", opencmw::typeName<T>, ringBuffer.bufferSize(), stream.str());
    }
};

template<template<typename, std::size_t SIZE> typename SequencerType, typename T, std::size_t SIZE>
// requires opencmw::is_instance_of_v<SequencerType<T, SIZE>, opencmw::disruptor::Sequencer>
struct fmt::formatter<SequencerType<T, SIZE>> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const SequencerType<T, SIZE> &sequencer, FormatContext &ctx) {
        std::stringstream stream;
        stream << "WaitStrategy: { ";
        // stream << typeName<decltype(_waitStrategy)>(); //xTODO: change
        stream << typeid(decltype(*sequencer._waitStrategy)).name();
        stream << " }, Cursor: { ";
        sequencer._cursor->writeDescriptionTo(stream);
        stream << " }, GatingSequences: [ ";

        auto firstItem = true;
        for (auto &&sequence : sequencer._gatingSequences) {
            if (firstItem)
                firstItem = false;
            else
                stream << ", ";
            stream << "{ ";
            sequence->writeDescriptionTo(stream);
            stream << " }";
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

std::ostream &operator<<(std::ostream &stream, const opencmw::disruptor::ProducerType &value) { return stream << fmt::format("{}", value); }
template<typename T, std::size_t SIZE>
std::ostream &operator<<(std::ostream &stream, const opencmw::disruptor::RingBuffer<T, SIZE> &ringBuffer) { return stream << fmt::format("{}", ringBuffer); }
template<typename T, std::size_t SIZE>
std::ostream &operator<<(std::ostream &stream, const opencmw::disruptor::SequencerBase<T, SIZE> &sequencer) { return stream << fmt::format("{}", sequencer); }
std::ostream &operator<<(std::ostream &stream, opencmw::disruptor::PollState &pollState) { return stream << fmt::format("{}", pollState); }

} // namespace opencmw
