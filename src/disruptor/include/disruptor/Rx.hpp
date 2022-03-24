#pragma once

#include <optional>

#pragma gcc diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <rxcpp/rx.hpp>
#pragma GCC diagnostic pop

#include <disruptor/Disruptor.hpp>

namespace opencmw::disruptor::rx {

namespace detail {
// Implements a bridge between the Disruptor and the Rx library
template<typename EventType>
class RxSubscriberBridgeImpl {
private:
    // List of Rx subscribers that listen to our events
    mutable std::vector<rxcpp::subscriber<EventType>> _subscribers;

    struct SetSubscriberFunctionObject {
        RxSubscriberBridgeImpl<EventType> *_this;

        // This is not really const (a shallow const), but Rx requires
        // the subscriber registration command to be const
        void operator()(const rxcpp::subscriber<EventType> &subscriber) const {
            _this->_subscribers.push_back(subscriber);
        }
    };

    // Returns a function object that sets the subscriber to this,
    // passed to the create method of rxcpp::observable
    auto setSubscriberFunctionObject() {
        return SetSubscriberFunctionObject{ this };
    }

    std::shared_ptr<
            opencmw::disruptor::FunctionObjectEventHandler<EventType, RxSubscriberBridgeImpl<EventType>>>
            _handlerWrapper;

    // std::optional used for lazy initialization of the Rx stream when
    std::optional<decltype(rxcpp::observable<>::create<EventType>(SetSubscriberFunctionObject{ nullptr }))>
            _stream;

public:
    const auto &stream() {
        if (!_stream) {
            _handlerWrapper = makeEventHandler<EventType>(RxSubscriberBridgeImpl<EventType>());
            _stream         = rxcpp::observable<>::create<EventType>(
                    _handlerWrapper->function().setSubscriberFunctionObject());
        }

        return _stream.value();
    }

    auto &handlerWrapper() {
        return _handlerWrapper;
    }

    void operator()(const EventType &data, [[maybe_unused]] std::int64_t sequence, [[maybe_unused]] bool endOfBatch) {
        for (auto &subscriber : _subscribers) {
            subscriber.on_next(data);
        }
    }

    bool isStreamInitialized() const { return _stream.has_value(); }
};
} // namespace detail

// Rx observable which listens for the Disruptor events and passes them
// on to the Rx library
template<typename Disruptor, typename EventType = typename Disruptor::EventType>
class Source {
private:
    Disruptor                                &_disruptor;
    detail::RxSubscriberBridgeImpl<EventType> _impl;

public:
    explicit Source(Disruptor &disruptor)
        : _disruptor(disruptor) {}

    const auto &stream() & {
        if (_impl.isStreamInitialized()) {
            return _impl.stream();
        }

        const auto &result = _impl.stream();
        _disruptor->handleEventsWith(_impl.handlerWrapper());
        return result;
    }
};

// Tuple-specialized meta-functions
namespace detail {
// std::for_each-like algorithm for processing tuple elemnts.
// Usage:
//    tuple_for_each(tuple, [] <typename Index>(Index, const auto& tupleField) {
//        std::cout << "Field index is " << Index::value <<
//                  << ", field value is " << tupleField;
//    });
template<typename Tuple, typename Func, std::size_t... Is>
constexpr void tuple_for_each_impl(Tuple &&tuple, Func &&function, std::index_sequence<Is...> /*indices*/) {
    ((function(std::integral_constant<std::size_t, Is>{}, std::get<Is>(tuple))), ...);
}

template<typename Tuple, typename Func>
constexpr void tuple_for_each(Tuple &&tuple, Func &&function) {
    tuple_for_each_impl(std::forward<Tuple>(tuple), std::forward<Func>(function), std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>{});
}

// std::transform-like algorithm for transforming tuple elements
template<typename Tuple, typename Func, std::size_t... Is>
constexpr auto tuple_transform_impl(Tuple &&tuple, Func &&function, std::index_sequence<Is...> /*indices*/) {
    return std::make_tuple(function(std::integral_constant<std::size_t, Is>{}, std::get<Is>(tuple))...);
}

template<typename Tuple, typename Func>
constexpr auto tuple_transform(Tuple &&tuple, Func &&function) {
    return tuple_transform_impl(std::forward<Tuple>(tuple), std::forward<Func>(function), std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>{});
}
} // namespace detail

// Policies to customize the aggregator behaviour
namespace Aggreagate {

// Basic aggregation policy which waits for events to be completed
// without any timeouts
struct Unlimited {
    template<typename... InputTypes>
    using OutputEventType = std::tuple<InputTypes...>;

    template<typename InputType>
    using Collection = std::deque<InputType>;

    template<typename WrappedType>
    static decltype(auto) eventFor(WrappedType &&item) {
        return std::forward<WrappedType>(item);
    }
};

// Aggregation policy which waits for events to be completed for
// as long as the number of incomplete events is not above a specified limit.
// @param Limit the maximum number of events to keep in the waiting state
// @param Wrapper a customization point to allow fields in incomplete events
//        to be explicitly specified. For example, if Wrapper = std::optional,
//        the missing values will be nullopt. By default, the missing values
//        will be default-constructed.
template<std::size_t Limit, template<typename...> typename Wrapper = std::type_identity>
struct Limited {
    static constexpr auto eventCountLimit = Limit;

    template<typename InputType>
    using WrappedType = std::conditional_t<std::is_same_v<Wrapper<InputType>, std::type_identity<InputType>>, InputType, Wrapper<InputType>>;

    template<typename... InputTypes>
    using OutputEventType = std::tuple<WrappedType<InputTypes>...>;

    template<typename InputType>
    using Collection = std::deque<InputType>;

    template<typename WrappedType>
    static decltype(auto) eventFor(WrappedType &&item) {
        return std::forward<WrappedType>(item);
    }
};

// Aggregation policy which waits for events to be completed until
// a specified number of milliseconds passes.
// @param TimeLimit number of milliseconds to wait for an event to be completed
// @param Wrapper see the same parameter of the Limited policy
template<std::size_t TimeLimit, template<typename...> typename Wrapper = std::type_identity>
struct Timed {
    static constexpr auto timeLimit = TimeLimit;

    template<typename InputType>
    using WrappedType = std::conditional_t<std::is_same_v<Wrapper<InputType>, std::type_identity<InputType>>, InputType, Wrapper<InputType>>;

    template<typename... InputTypes>
    using OutputEventType = std::tuple<WrappedType<InputTypes>...>;

    template<typename InputType>
    struct WithTimestamp {
        WithTimestamp(InputType _event)
            : timestamp(std::chrono::steady_clock::now())
            , event(std::move(_event)) {
        }
        std::chrono::time_point<std::chrono::steady_clock> timestamp;
        InputType                                          event;
    };

    template<typename InputType>
    using Collection = std::deque<WithTimestamp<InputType>>;

    template<typename WrappedType>
    static auto eventFor(WrappedType &&item) {
        return std::forward<WrappedType>(item).event;
    }
};

// Concept that tests whether a custom user policy wants
// the number of events that are being waited on to be limited
template<typename Policy>
concept EventCountLimitedPolicy = requires(Policy policy) {
    policy.eventCountLimit;
};
static_assert(EventCountLimitedPolicy<Limited<0>>);
static_assert(not EventCountLimitedPolicy<Unlimited>);
static_assert(not EventCountLimitedPolicy<Timed<0>>);

// Concept that tests whether a custom user policy wants
// to limit the time of how long to wait until an
// event is completed
template<typename Policy>
concept TimeLimitedPolicy = requires(Policy policy) {
    policy.timeLimit;
};
static_assert(TimeLimitedPolicy<Timed<0>>);
static_assert(not TimeLimitedPolicy<Unlimited>);
static_assert(not TimeLimitedPolicy<Limited<0>>);

// Concept to test for non-limiting policies
template<typename Policy>
concept UnlimitedPolicy = !TimeLimitedPolicy<Policy> && !EventCountLimitedPolicy<Policy>;

// Concept that tests whether a custom user policy wants
// the events to be grouped based on a specified event property.
// When implementing custom policies that use this,
// you need to add an IdMatcherPolicyTag to your policy class
// and a member function idForEvent which returns the property
// you want the events to be grouped on.
// See the TestAggregatorPolicy.
template<typename, typename = void>
struct IdMatcherPolicyImpl : std::false_type {};
template<typename T>
struct IdMatcherPolicyImpl<T, std::void_t<typename T::IdMatcherPolicyTag>> : std::true_type {};

template<typename Policy>
concept IdMatcherPolicy = IdMatcherPolicyImpl<Policy>::value;
static_assert(not IdMatcherPolicy<Unlimited>);
static_assert(not IdMatcherPolicy<Limited<0>>);

// Implementation of the aggregation node for Rx
// It takes several streams as inputs, and aggregates them according
// to the provided policy and emits tuples of aggregated input events.
//
// The policy customization points are:
//  - Collection type used for the backlog
//  - Whether or not to limit the number of items in the backlog
//  - Whether or not to limit the age of items in the backlog
//  - Whether or not to group events based on a specified property
template<typename Policy, typename... InputTypes>
class AggregateImpl {
    static_assert(sizeof...(InputTypes) <= 32);

private:
    using OutputEventType = typename Policy::template OutputEventType<InputTypes...>;
    detail::RxSubscriberBridgeImpl<OutputEventType> _impl;
    [[no_unique_address]] Policy                    _policy;

    // Keeps the input stream and caches the events it received from that stream
    // until in can be sent as a part of an output stream.
    template<typename InputType>
    struct ObservableBacklog {
        explicit ObservableBacklog(rxcpp::observable<InputType> &&_observable)
            : observable(std::move(_observable)) {}

        rxcpp::observable<InputType>                    observable;
        mutable std::mutex                              backlogLock;

        typename Policy::template Collection<InputType> backlog;
    };

    std::tuple<ObservableBacklog<InputTypes>...> _observableBacklogs;

    constexpr static std::uint32_t               s_allQueuesFlags = (1U << sizeof...(InputTypes)) - 1;
    std::uint32_t                                _fullQueuesFlags = 0;

    void                                         setFullQueueFlag(std::size_t index, bool value) {
        if (value) {
            _fullQueuesFlags |= (1U << index);
        } else {
            _fullQueuesFlags &= ~(1U << index);
        }
    }

    template<typename Event>
    static auto idForWrappedEvent(const Event &event) {
        if constexpr (IdMatcherPolicy<Policy>) {
            return Policy::idForEvent(Policy::eventFor(event));
        } else {
            return 0;
        }
    }

    template<typename TriggerEventId>
    void sendEvent([[maybe_unused]] TriggerEventId triggerEventId, bool forceSending) {
        bool allFound = true;
        auto event    = detail::tuple_transform(_observableBacklogs,
                   [&]<typename Index>(Index, auto &observableBacklog) {
            std::unique_lock lock{ observableBacklog.backlogLock };

            if constexpr (IdMatcherPolicy<Policy>) {
                auto iter = std::ranges::find_if(observableBacklog.backlog, [this, triggerEventId](const auto &backlogEvent) {
                    return triggerEventId == idForWrappedEvent(backlogEvent);
                     });

                if (iter != observableBacklog.backlog.end()) {
                    auto item = Policy::eventFor(*iter);
                    observableBacklog.backlog.erase(iter);
                    setFullQueueFlag(Index::value, !observableBacklog.backlog.empty());
                    return item;
                } else {
                    setFullQueueFlag(Index::value, !observableBacklog.backlog.empty());
                    allFound = false;
                    return decltype(Policy::eventFor(*iter))();
                }

            } else {
                if (!observableBacklog.backlog.empty()) {
                    auto item = Policy::eventFor(observableBacklog.backlog.front());
                    observableBacklog.backlog.pop_front();
                    setFullQueueFlag(Index::value, !observableBacklog.backlog.empty());
                    return item;
                } else {
                    setFullQueueFlag(Index::value, false);
                    allFound = false;
                    return decltype(Policy::eventFor(observableBacklog.backlog.front()))();
                }
            } });

        if (allFound || forceSending) {
            _impl.handlerWrapper()->function()(std::move(event), 0, false);
        }
    }

    void sendExpiredEvents() {
        if constexpr (!UnlimitedPolicy<Policy>) {
            detail::tuple_for_each(_observableBacklogs, [this]<typename IndexIn>(IndexIn, auto &observableBacklogIn) {
                auto sendFirstEvent = [&] {
                    sendEvent(idForWrappedEvent(observableBacklogIn.backlog.front()), true);
                };

                if constexpr (EventCountLimitedPolicy<Policy>) {
                    while (observableBacklogIn.backlog.size() > Policy::eventCountLimit) {
                        sendFirstEvent();
                    }
                }

                if constexpr (TimeLimitedPolicy<Policy>) {
                    auto expired = [now = std::chrono::steady_clock::now()](auto timestamp) {
                        return static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - timestamp).count()) > Policy::timeLimit;
                    };

                    while (!observableBacklogIn.backlog.empty() && expired(observableBacklogIn.backlog.front().timestamp)) {
                        sendFirstEvent();
                    }
                }
            });
        }
    }

public:
    explicit AggregateImpl(rxcpp::observable<InputTypes>... observables)
        : _observableBacklogs(std::move(observables)...) {
        detail::tuple_for_each(_observableBacklogs, [&]<typename Index>(Index, auto &observableBacklog) {
            observableBacklog.observable.subscribe(
                    // observableBacklog is a reference to a field in this,
                    // so async call of this lambda is not an issue.
                    [this, &observableBacklog](const auto &value) {
                        const auto currentFlag = 1U << Index::value;
                        {
                            std::unique_lock lock{ observableBacklog.backlogLock };
                            _fullQueuesFlags |= currentFlag;
                            observableBacklog.backlog.push_back(value);
                        }

                        sendExpiredEvents();

                        // Each queue has something in it
                        if (_fullQueuesFlags == s_allQueuesFlags) {
                            if constexpr (IdMatcherPolicy<Policy>) {
                                sendEvent(Policy::idForEvent(value), false);
                            } else {
                                sendEvent(/* unused id */ 0, false);
                            }
                        }
                    });
        });
    }

    AggregateImpl(const AggregateImpl &) = delete;
    AggregateImpl &operator=(const AggregateImpl &) = delete;
    AggregateImpl(AggregateImpl &&)                 = delete;
    AggregateImpl &operator=(AggregateImpl &&) = delete;
    ~AggregateImpl() {
        flush();
    }

    void flush() {
        if constexpr (!UnlimitedPolicy<Policy>) {
            detail::tuple_for_each(_observableBacklogs, [this]<typename IndexIn>(IndexIn, auto &observableBacklogIn) {
                auto sendFirstEvent = [&] {
                    sendEvent(idForWrappedEvent(observableBacklogIn.backlog.front()), true);
                };
                while (!observableBacklogIn.backlog.empty()) {
                    sendFirstEvent();
                }
            });
        }
    }

    void sendHeartbeat() {
        sendExpiredEvents();
    }

    const auto &stream() & {
        return _impl.stream();
    }
};

} // namespace Aggreagate

template<typename Policy, typename... InputObservables>
auto aggreagate(InputObservables &&...input) {
    return Aggreagate::AggregateImpl<Policy, typename std::remove_cvref_t<InputObservables>::value_type...>(std::forward<InputObservables>(input)...);
}

} // namespace opencmw::disruptor::rx
