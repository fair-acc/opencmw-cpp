#pragma once

#include <memory>

#include "FixedSequenceGroup.hpp"
#include "IDataProvider.hpp"
#include "ISequence.hpp"
#include "ISequencer.hpp"
#include "ProducerType.hpp"

namespace opencmw::disruptor {

enum class PollState {
    Processing,
    Gating,
    Idle
};

template<typename T>
class EventPoller {
private:
    std::shared_ptr<IDataProvider<T>> _dataProvider;
    std::shared_ptr<ISequencer<T>>    _sequencer;
    std::shared_ptr<ISequence>        _sequence;
    std::shared_ptr<ISequence>        _gatingSequence;

public:
    EventPoller(const std::shared_ptr<IDataProvider<T>> &dataProvider,
            const std::shared_ptr<ISequencer<T>>        &sequencer,
            const std::shared_ptr<ISequence>            &sequence,
            const std::shared_ptr<ISequence>            &gatingSequence)
        : _dataProvider(dataProvider)
        , _sequencer(sequencer)
        , _sequence(sequence)
        , _gatingSequence(gatingSequence) {}

    template<typename TEventHandler>
    PollState poll(TEventHandler &&eventHandler) {
        static_assert(std::is_invocable_r<bool, TEventHandler, T &, std::int64_t, bool>::value, "eventHandler should have the following signature: bool(T&, std::int64_t, bool)");

        auto currentSequence   = _sequence->value();
        auto nextSequence      = currentSequence + 1;
        auto availableSequence = _sequencer->getHighestPublishedSequence(nextSequence, _gatingSequence->value());

        if (nextSequence <= availableSequence) {
            bool processNextEvent;
            auto processedSequence = currentSequence;

            try {
                do {
                    auto &event       = (*_dataProvider)[nextSequence];
                    processNextEvent  = eventHandler(event, nextSequence, nextSequence == availableSequence);
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

    static std::shared_ptr<EventPoller<T>> newInstance(const std::shared_ptr<IDataProvider<T>> &dataProvider,
            const std::shared_ptr<ISequencer<T>>                                               &sequencer,
            const std::shared_ptr<ISequence>                                                   &sequence,
            const std::shared_ptr<ISequence>                                                   &cursorSequence,
            const std::vector<std::shared_ptr<ISequence>>                                      &gatingSequences) {
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

    std::shared_ptr<ISequence> sequence() const {
        return _sequence;
    };
};

} // namespace opencmw::disruptor

#include <ostream>

namespace std {

inline ostream &operator<<(ostream &stream, const opencmw::disruptor::PollState &value) {
    switch (value) {
    case opencmw::disruptor::PollState::Processing:
        return stream << "Processing";
    case opencmw::disruptor::PollState::Gating:
        return stream << "Gating";
    case opencmw::disruptor::PollState::Idle:
        return stream << "Idle";
    default:
        return stream << static_cast<int>(value);
    }
}

} // namespace std
