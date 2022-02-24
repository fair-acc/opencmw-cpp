#pragma once

#include <cassert>
#include <concepts>
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

template<typename T>
class RingBuffer;
template<typename T>
class EventPoller;
namespace detail {
template<typename T>
class SingleProducerSequencer;
template<typename T>
class MultiProducerSequencer;
} // namespace detail

/**
 * Coordinator for claiming sequences for access to a data structure while tracking dependent Sequences
 */
template<typename T>
class Sequencer : public ISequenced, public ICursored, public IHighestPublishedSequenceProvider {
public:
    /**
     * Confirms if a sequence is published and the event is available for use; non-blocking.
     *
     * \param sequence sequence of the buffer to check
     * \returns true if the sequence is available for use, false if not
     */
    virtual bool isAvailable(std::int64_t sequence) = 0;

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
    virtual std::int64_t                    getMinimumSequence()                                                                                                      = 0;

    virtual std::shared_ptr<EventPoller<T>> newPoller(const std::shared_ptr<RingBuffer<T>> &provider, const std::vector<std::shared_ptr<ISequence>> &gatingSequences) = 0;

    virtual void                            writeDescriptionTo(std::ostream &stream) const                                                                            = 0;
    virtual ~Sequencer()                                                                                                                                              = default;
};

/**
 * Ring based store of reusable entries containing the data representing an event being exchanged between event publisher and IEventProcessors.
 *
 * \tparam T implementation storing the data for sharing during exchange or parallel coordination of an event.
 */
template<typename T>
class RingBuffer : public IEventSequencer<T>, public ICursored, public std::enable_shared_from_this<RingBuffer<T>> {
    const uint8_t                 padding0[56]{}; // NOSONAR
    mutable std::vector<T>        _entries;
    const std::int32_t            _bufferSize{};
    std::int32_t                  _indexMask{};
    std::shared_ptr<Sequencer<T>> _sequencer;
    const uint8_t                 padding1[40]{}; // NOSONAR

    static const std::int32_t     _bufferPad = 128 / sizeof(int *);

public:
    RingBuffer() = delete;
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
            _sequencer = std::make_shared<detail::SingleProducerSequencer<T>>(bufferSize, waitStrategy);
            break;
        case ProducerType::Multi:
            _sequencer = std::make_shared<detail::MultiProducerSequencer<T>>(bufferSize, waitStrategy);
            break;
        default:
            throw std::invalid_argument(fmt::format("invalid producer type: {}", producerType));
        }
    }

    [[nodiscard]] T &operator[](std::int64_t sequence) const override {
        return _entries[static_cast<std::size_t>(_bufferPad + (static_cast<std::int32_t>(sequence) & _indexMask))];
    }

    [[nodiscard]] std::int32_t  bufferSize() const override { return _bufferSize; }
    [[nodiscard]] Sequencer<T> &getSequencer() const { return *_sequencer; }
    [[nodiscard]] bool          hasAvailableCapacity(std::int32_t requiredCapacity) override { return _sequencer->hasAvailableCapacity(requiredCapacity); }
    std::int64_t                next(std::int32_t n_slots_to_claim = 1) override { return _sequencer->next(n_slots_to_claim); }
    std::int64_t                tryNext(std::int32_t n_slots_to_claim = 1) override { return _sequencer->tryNext(n_slots_to_claim); }
    [[nodiscard]] std::int64_t  cursor() const override { return _sequencer->cursor(); }
    [[nodiscard]] std::int64_t  getRemainingCapacity() override { return _sequencer->getRemainingCapacity(); }
    void                        publish(std::int64_t sequence) override { _sequencer->publish(sequence); }
    bool                        isPublished(std::int64_t sequence) { return _sequencer->isAvailable(sequence); }
    void                        addGatingSequences(const std::vector<std::shared_ptr<ISequence>> &gatingSequences) { _sequencer->addGatingSequences(gatingSequences); }
    bool                        removeGatingSequence(const std::shared_ptr<ISequence> &sequence) { return _sequencer->removeGatingSequence(sequence); }
    std::int64_t                getMinimumGatingSequence() { return _sequencer->getMinimumSequence(); }

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

    /**
     * Implementations translate (write) data representations into events claimed from the RingBuffer<T>.
     * When publishing to the RingBuffer, provide an EventTranslator. The RingBuffer will select the next available event by sequence and provide
     * it to the EventTranslator(which should update the event), before publishing the sequence update.
     *
     * @param translator being invoked with <T&, sequence>
     * @param args optional arguments that are being forwarded to the translator
     */
    template<typename... Args>
    void publishEvent(std::invocable<T, std::int64_t> auto &&translator, Args &&...args) {
        auto sequence = _sequencer->next();
        translateAndPublish(std::forward<decltype(translator)>(translator), sequence, std::forward<Args>(args)...);
    }

    /**
     * Implementations translate (write) data representations into events claimed from the RingBuffer<T>.
     * When publishing to the RingBuffer, provide an EventTranslator. The RingBuffer will select the next available event by sequence and provide
     * it to the EventTranslator(which should update the event), before publishing the sequence update.
     *
     * @param translator being invoked with <T&, sequence>
     * @param args optional arguments that are being forwarded to the translator
     * @return @code true on success, @code false otherwise
     */
    template<typename... Args>
    bool tryPublishEvent(std::invocable<T, std::int64_t> auto &&translator, Args &&...args) {
        try {
            auto sequence = _sequencer->tryNext();
            translateAndPublish(std::forward<decltype(translator)>(translator), sequence, std::forward<Args>(args)...);
            return true;
        } catch (const NoCapacityException &) {
            return false;
        }
    }

private:
    template<typename... TArgs>
    void translateAndPublish(std::invocable<T, std::int64_t, TArgs...> auto &&translator, std::int64_t sequence, const TArgs &...args) {
        try {
            std::invoke(std::forward<decltype(translator)>(translator), std::forward<T>((*this)[sequence]), sequence, args...);
        } catch (...) {
            // blindly catch all exceptions from the user supplied translator function (i.e. unrelated to the RingBuffer mechanics)
            // xTODO: evaluate if these should be thrown back to the user
        }
        _sequencer->publish(sequence);
    }
};

template<typename T>
class SequencerBase : public Sequencer<T>, public std::enable_shared_from_this<SequencerBase<T>> {
protected:
    /**
     * Volatile in the Java version => always use Volatile.Read/Write or Interlocked methods to access this field.
     */
    std::vector<std::shared_ptr<ISequence>> _gatingSequences;

    std::int32_t                            _bufferSize;
    std::shared_ptr<WaitStrategy>           _waitStrategy;
    std::shared_ptr<Sequence>               _cursor;
    WaitStrategy                           &_waitStrategyRef;
    Sequence                               &_cursorRef;

public:
    SequencerBase(std::int32_t bufferSize, std::shared_ptr<WaitStrategy> waitStrategy)
        : _bufferSize(bufferSize)
        , _waitStrategy(std::move(waitStrategy))
        , _cursor(std::make_shared<Sequence>())
        , _waitStrategyRef(*_waitStrategy)
        , _cursorRef(*_cursor) {}

    [[nodiscard]] std::shared_ptr<ISequenceBarrier> newBarrier(const std::vector<std::shared_ptr<ISequence>> &sequencesToTrack) override {
        return std::make_shared<ProcessingSequenceBarrier>(this->shared_from_this(), _waitStrategy, _cursor, sequencesToTrack);
    }

    [[nodiscard]] std::int32_t      bufferSize() const override { return _bufferSize; }
    [[nodiscard]] std::int64_t      cursor() const override { return _cursorRef.value(); }
    void                            addGatingSequences(const std::vector<std::shared_ptr<ISequence>> &gatingSequences) override { SequenceGroups::addSequences(_gatingSequences, *this, gatingSequences); }
    bool                            removeGatingSequence(const std::shared_ptr<ISequence> &sequence) override { return SequenceGroups::removeSequence(_gatingSequences, sequence); }
    [[nodiscard]] std::int64_t      getMinimumSequence() override { return util::getMinimumSequence(_gatingSequences, _cursorRef.value()); }

    std::shared_ptr<EventPoller<T>> newPoller(const std::shared_ptr<RingBuffer<T>> &provider, const std::vector<std::shared_ptr<ISequence>> &gatingSequences) override {
        return EventPoller<T>::newInstance(provider, this->shared_from_this(), std::make_shared<Sequence>(), _cursor, gatingSequences);
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

template<typename T>
class SingleProducerSequencer : public SequencerBase<T> {
    struct Fields {
        const uint8_t padding0[56]{}; // NOSONAR
        std::int64_t  nextValue;
        std::int64_t  cachedValue;
        const uint8_t padding1[56]{}; // NOSONAR

        Fields(std::int64_t _nextValue, std::int64_t _cachedValue)
            : nextValue(_nextValue)
            , cachedValue(_cachedValue) {}
    };

protected:
    Fields _fields;

public:
    /**
     * Construct a SequencerBase with the selected strategies.
     *
     * \param bufferSize
     * \param waitStrategy waitStrategy for those waiting on sequences.
     */
    SingleProducerSequencer(std::int32_t bufferSize, const std::shared_ptr<WaitStrategy> &waitStrategy)
        : SequencerBase<T>(bufferSize, waitStrategy)
        , _fields(Sequence::InitialCursorValue, Sequence::InitialCursorValue) {}

    bool hasAvailableCapacity(int requiredCapacity) override {
        std::int64_t nextValue            = _fields.nextValue;
        std::int64_t wrapPoint            = (nextValue + requiredCapacity) - this->_bufferSize;
        std::int64_t cachedGatingSequence = _fields.cachedValue;

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
        assert((n_slots_to_claim < 1 || n_slots_to_claim > this->_bufferSize) && "n_slots_to_claim must be > 0 and < bufferSize");

        auto nextValue            = _fields.nextValue;

        auto nextSequence         = nextValue + n_slots_to_claim;
        auto wrapPoint            = nextSequence - this->_bufferSize;
        auto cachedGatingSequence = _fields.cachedValue;

        if (wrapPoint > cachedGatingSequence || cachedGatingSequence > nextValue) {
            this->_cursor->setValue(nextValue);

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
        assert((n_slots_to_claim < 1) && "n_slots_to_claim must be > 0");

        if (!hasAvailableCapacity(n_slots_to_claim)) {
            throw NoCapacityException();
        }

        auto nextSequence = _fields.nextValue + n_slots_to_claim;
        _fields.nextValue = nextSequence;

        return nextSequence;
    }

    std::int64_t getRemainingCapacity() override {
        auto                  nextValue = _fields.nextValue;
        [[maybe_unused]] auto sizeTemp  = this->_gatingSequences.size();
        auto                  consumed  = util::getMinimumSequence(this->_gatingSequences, nextValue);
        auto                  produced  = nextValue;

        return this->bufferSize() - (produced - consumed);
    }

    void publish(std::int64_t sequence) override {
        this->_cursorRef.setValue(sequence);
        if constexpr (requires { this->_waitStrategyRef.signalAllWhenBlocking(); }) {
            this->_waitStrategyRef.signalAllWhenBlocking();
        }
    }

    bool         isAvailable(std::int64_t sequence) override { return sequence <= this->_cursorRef.value(); }
    std::int64_t getHighestPublishedSequence(std::int64_t /*nextSequence*/, std::int64_t availableSequence) override { return availableSequence; }
};

/**
 * Coordinator for claiming sequences for access to a data structure while tracking dependent Sequences. Suitable for use for sequencing across multiple publisher threads.
 * Note on SequencerBase.cursor:  With this sequencer the cursor value is updated after the call to SequencerBase::next(), to determine the highest available sequence that can be read,
 * then getHighestPublishedSequence should be used.
 */
template<typename T>
class MultiProducerSequencer : public SequencerBase<T> {
private:
    std::shared_ptr<Sequence> _gatingSequenceCache = std::make_shared<Sequence>();

    // availableBuffer tracks the state of each ringbuffer slot
    // see below for more details on the approach
    std::unique_ptr<std::int32_t[]> _availableBuffer;
    std::int32_t                    _indexMask;
    std::int32_t                    _indexShift;

public:
    /**
     * Construct a SequencerBase with the selected strategies.
     *
     * \param bufferSize
     * \param waitStrategy waitStrategy for those waiting on sequences.
     */
    MultiProducerSequencer(std::int32_t bufferSize, const std::shared_ptr<WaitStrategy> &waitStrategy)
        : SequencerBase<T>(bufferSize, waitStrategy), _indexMask(bufferSize - 1), _indexShift(util::log2(bufferSize)) {
        _availableBuffer = std::make_unique<std::int32_t[]>(static_cast<std::size_t>(bufferSize)),
        initializeAvailableBuffer();
    }

    bool hasAvailableCapacity(std::int32_t requiredCapacity) override {
        return hasAvailableCapacity(this->_gatingSequences, requiredCapacity, this->_cursor->value());
    }

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

    bool isAvailable(std::int64_t sequence) override {
        auto index = calculateIndex(sequence);
        auto flag  = calculateAvailabilityFlag(sequence);

        return _availableBuffer[static_cast<std::size_t>(index)] == flag;
    }

    std::int64_t getHighestPublishedSequence(std::int64_t lowerBound, std::int64_t availableSequence) override {
        for (std::int64_t sequence = lowerBound; sequence <= availableSequence; sequence++) {
            if (!isAvailable(sequence)) {
                return sequence - 1;
            }
        }

        return availableSequence;
    }

private:
    [[nodiscard]] bool hasAvailableCapacity(const std::vector<std::shared_ptr<ISequence>> &gatingSequences, std::int32_t requiredCapacity, std::int64_t cursorValue) const noexcept {
        const auto wrapPoint = (cursorValue + requiredCapacity) - this->_bufferSize;

        if (const auto cachedGatingSequence = _gatingSequenceCache->value(); wrapPoint > cachedGatingSequence || cachedGatingSequence > cursorValue) {
            const auto minSequence = util::getMinimumSequence(gatingSequences, cursorValue);
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

    void         setAvailable(std::int64_t sequence) { setAvailableBufferValue(calculateIndex(sequence), calculateAvailabilityFlag(sequence)); }
    void         setAvailableBufferValue(std::int32_t index, std::int32_t flag) { _availableBuffer[static_cast<std::size_t>(index)] = flag; }
    std::int32_t calculateAvailabilityFlag(std::int64_t sequence) { return static_cast<std::int32_t>(static_cast<std::uint64_t>(sequence) >> _indexShift); }
    std::int32_t calculateIndex(std::int64_t sequence) { return static_cast<std::int32_t>(sequence) & _indexMask; }
};

} // namespace detail

enum class PollState {
    Processing,
    Gating,
    Idle,
    UNKNOWN
};

template<typename T>
class EventPoller {
private:
    std::shared_ptr<RingBuffer<T>> _dataProvider;
    std::shared_ptr<Sequencer<T>>  _sequencer;
    std::shared_ptr<ISequence>     _sequence;
    std::shared_ptr<ISequence>     _gatingSequence;

public:
    EventPoller(const std::shared_ptr<RingBuffer<T>> &dataProvider,
            const std::shared_ptr<Sequencer<T>>      &sequencer,
            const std::shared_ptr<ISequence>         &sequence,
            const std::shared_ptr<ISequence>         &gatingSequence)
        : _dataProvider(dataProvider)
        , _sequencer(sequencer)
        , _sequence(sequence)
        , _gatingSequence(gatingSequence) {}

    static std::shared_ptr<EventPoller<T>> newInstance(const std::shared_ptr<RingBuffer<T>> &dataProvider,
            const std::shared_ptr<Sequencer<T>>                                             &sequencer,
            const std::shared_ptr<ISequence>                                                &sequence,
            const std::shared_ptr<ISequence>                                                &cursorSequence,
            const std::vector<std::shared_ptr<ISequence>>                                   &gatingSequences) {
        std::shared_ptr<ISequence> gatingSequence;

        if (gatingSequences.empty()) {
            gatingSequence = cursorSequence;
        } else if (gatingSequences.size() == 1) {
            gatingSequence = *gatingSequences.begin();
        } else {
            gatingSequence = std::make_shared<FixedSequenceGroup>(gatingSequences);
        }

        return std::make_shared<EventPoller<T>>(dataProvider, sequencer, sequence, gatingSequence);
    }

    PollState poll(opencmw::invocable_r<bool, T &, std::int64_t, bool> auto &&eventHandler) {
        const auto currentSequence   = _sequence->value();
        auto       nextSequence      = currentSequence + 1;
        const auto availableSequence = _sequencer->getHighestPublishedSequence(nextSequence, _gatingSequence->value());

        if (nextSequence <= availableSequence) {
            bool processNextEvent;
            auto processedSequence = currentSequence;

            try {
                do {
                    auto &event       = (*_dataProvider)[nextSequence];
                    processNextEvent  = std::invoke(eventHandler, event, nextSequence, nextSequence == availableSequence);
                    processedSequence = nextSequence;
                    nextSequence++;
                } while (nextSequence <= availableSequence && processNextEvent);
            } catch (...) {
                _sequence->setValue(processedSequence);
                throw;
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

template<typename T>
struct fmt::formatter<opencmw::disruptor::RingBuffer<T>> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const opencmw::disruptor::RingBuffer<T> &ringBuffer, FormatContext &ctx) {
        std::stringstream stream;
        ringBuffer.getSequencer().writeDescriptionTo(stream);
        return fmt::format_to(ctx.out(), "RingBuffer<{}, {}> - SequencerBase: {{ {} }}", opencmw::typeName<T>, ringBuffer.bufferSize(), stream.str());
    }
};

template<template<typename> typename SequencerType, typename T>
requires opencmw::is_instance_of_v<SequencerType<T>, opencmw::disruptor::Sequencer>
struct fmt::formatter<SequencerType<T>> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const SequencerType<T> &sequencer, FormatContext &ctx) {
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
template<typename T>
std::ostream &operator<<(std::ostream &stream, const opencmw::disruptor::RingBuffer<T> &ringBuffer) { return stream << fmt::format("{}", ringBuffer); }
template<typename T>
std::ostream &operator<<(std::ostream &stream, const opencmw::disruptor::SequencerBase<T> &sequencer) { return stream << fmt::format("{}", sequencer); }
std::ostream &operator<<(std::ostream &stream, opencmw::disruptor::PollState &pollState) { return stream << fmt::format("{}", pollState); }

} // namespace opencmw
