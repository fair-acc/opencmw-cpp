#pragma once

#include <optional>

#include <rxcpp/rx.hpp>

#include <disruptor/Disruptor.hpp>

namespace opencmw::disruptor::rx {

namespace detail {
template<typename EventType>
class RxSubscriberBridgeImpl {
private:
    mutable std::vector<rxcpp::subscriber<EventType>> _subscribers;

    // Returns a function object that sets the subscriber to this
    auto setSubscriberFunctionObject() {
        struct FunctionObject {
            RxSubscriberBridgeImpl<EventType> *_this;

            // This is not really const (a shallow const), but Rx requires
            // the subscriber registration command to be const
            void operator()(const rxcpp::subscriber<EventType> &subscriber) const {
                _this->_subscribers.push_back(subscriber);
            }
        };
        return FunctionObject{ this };
    }

    std::shared_ptr<
            opencmw::disruptor::FunctionObjectEventHandler<EventType, RxSubscriberBridgeImpl<EventType>>>
            _handlerWrapper;

    std::optional<decltype(rxcpp::observable<>::create<EventType>(std::declval<RxSubscriberBridgeImpl<EventType>>().setSubscriberFunctionObject()))>
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
};
} // namespace detail

template<typename Disruptor, typename EventType = typename Disruptor::EventType>
class Source {
private:
    Disruptor                                &_disruptor;
    detail::RxSubscriberBridgeImpl<EventType> _impl;

public:
    Source(Disruptor &disruptor)
        : _disruptor(disruptor) {}

    const auto &stream() & {
        const auto &result = _impl.stream();
        _disruptor->handleEventsWith(_impl.handlerWrapper());
        return result;
    }
};

// based on MIME.hpp, which includes the whole opencmw.hpp with refl, units, fmt...
namespace detail {
template<typename Tuple, typename Func, std::size_t... Is>
constexpr void tuple_for_each_impl(Tuple &&tuple, Func &&f, std::index_sequence<Is...>) {
    ((f(std::integral_constant<std::size_t, Is>{}, std::get<Is>(tuple))), ...);
}

template<typename Tuple, typename Func>
constexpr void tuple_for_each(Tuple &&tuple, Func &&f) {
    tuple_for_each_impl(std::forward<Tuple>(tuple), std::forward<Func>(f), std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>{});
}

template<typename Tuple, typename Func, std::size_t... Is>
constexpr auto tuple_transform_impl(Tuple &&tuple, Func &&f, std::index_sequence<Is...>) {
    return std::make_tuple(f(std::integral_constant<std::size_t, Is>{}, std::get<Is>(tuple))...);
}

template<typename Tuple, typename Func>
constexpr auto tuple_transform(Tuple &&tuple, Func &&f) {
    return tuple_transform_impl(std::forward<Tuple>(tuple), std::forward<Func>(f), std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>{});
}
} // namespace detail

namespace Join {

struct Unlimited {
    template<typename... InputTypes>
    using OutputEventType = std::tuple<InputTypes...>;

    template<typename InputType>
    using Collection = std::deque<InputType>;
};

template<std::size_t Limit, template<typename...> typename Wrapper = std::type_identity>
struct Limited {
    static constexpr auto eventCountLimit = Limit;

    template<typename InputType>
    using WrappedType = std::conditional_t<std::is_same_v<Wrapper<InputType>, std::type_identity<InputType>>, InputType, Wrapper<InputType>>;

    template<typename... InputTypes>
    using OutputEventType = std::tuple<WrappedType<InputTypes>...>;

    template<typename InputType>
    using Collection = std::deque<InputType>;
};

template<std::size_t TimeLimit, template<typename...> typename Wrapper = std::type_identity>
struct Timed {
    static constexpr auto timeLimit = TimeLimit;

    template<typename InputType>
    using WrappedType = std::conditional_t<std::is_same_v<Wrapper<InputType>, std::type_identity<InputType>>, InputType, Wrapper<InputType>>;

    template<typename... InputTypes>
    using OutputEventType = std::tuple<WrappedType<InputTypes>...>;

    template<typename InputType>
    using Collection = std::deque<InputType>;

    std::chrono::time_point<std::chrono::steady_clock> lastEventTimestamp;

    Timed()
        : lastEventTimestamp(std::chrono::steady_clock::now()) {
    }
};

template<class Policy>
concept EventCountLimitedPolicy = requires(Policy policy) {
    policy.eventCountLimit;
};
static_assert(EventCountLimitedPolicy<Limited<0>>);
static_assert(not EventCountLimitedPolicy<Unlimited>);
static_assert(not EventCountLimitedPolicy<Timed<0>>);

template<class Policy>
concept TimeLimitedPolicy = requires(Policy policy) {
    policy.timeLimit;
};
static_assert(TimeLimitedPolicy<Timed<0>>);
static_assert(not TimeLimitedPolicy<Unlimited>);
static_assert(not TimeLimitedPolicy<Limited<0>>);

template<typename Policy, typename... InputTypes>
class JoinImpl {
private:
    using OutputEventType = typename Policy::template OutputEventType<InputTypes...>;
    detail::RxSubscriberBridgeImpl<OutputEventType> _impl;
    [[no_unique_address]] Policy                    _policy;

    template<typename InputType>
    struct ObservableBacklog {
        ObservableBacklog(rxcpp::observable<InputType> &&_observable)
            : observable(std::move(_observable)) {}

        rxcpp::observable<InputType>                    observable;
        mutable std::mutex                              backlogLock;

        typename Policy::template Collection<InputType> backlog;
    };

    std::tuple<ObservableBacklog<InputTypes>...> _observableBacklogs;
    std::uint32_t                                _emptyQueues = (1 << sizeof...(InputTypes)) - 1;

    //
    void sendEvent() {
        auto event   = detail::tuple_transform(_observableBacklogs,
                  []<typename Index>(Index, auto &observableBacklog) {
                    std::unique_lock lock{ observableBacklog.backlogLock };
                    if (observableBacklog.backlog.size() > 0) {
                        auto item = observableBacklog.backlog.front();
                        observableBacklog.backlog.pop_front();
                        return item;
                    } else {
                        return typename decltype(observableBacklog.backlog)::value_type();
                    }
                  });

        _emptyQueues = 0;
        detail::tuple_for_each(_observableBacklogs, [&]<typename Index>(Index, const auto &observableBacklog) {
            std::unique_lock lock{ observableBacklog.backlogLock };
            if (observableBacklog.backlog.empty()) {
                _emptyQueues |= (1u << Index::value);
            }
        });

        _impl.handlerWrapper()->function()(std::move(event), 0, false);
    }

public:
    JoinImpl(rxcpp::observable<InputTypes>... observables)
        : _observableBacklogs(std::move(observables)...) {
        detail::tuple_for_each(_observableBacklogs, [&]<typename Index>(Index, auto &observableBacklog) {
            observableBacklog.observable.subscribe(
                    // observableBacklog is a reference to a field in this,
                    // so async call of this lambda is not an issue.
                    [this, &observableBacklog](auto &&value) {
                        const auto currentFlag = 1u << Index::value;
                        _emptyQueues &= ~currentFlag;
                        {
                            std::unique_lock lock{ observableBacklog.backlogLock };
                            observableBacklog.backlog.push_back(std::forward<decltype(value)>(value));
                        }

                        // Each queue has something in it
                        bool toSend = (_emptyQueues == 0);

                        if constexpr (EventCountLimitedPolicy<Policy>) {
                            // If there is any queue with more than allowed items,
                            // force sending
                            if (!toSend) {
                                detail::tuple_for_each(_observableBacklogs, [&toSend]<typename IndexIn>(IndexIn, auto &observableBacklogIn) { toSend |= observableBacklogIn.backlog.size() > Policy::eventCountLimit; });
                            }
                        }

                        if constexpr (TimeLimitedPolicy<Policy>) {
                            const auto now = std::chrono::steady_clock::now();
                            // Has the time limit expired?
                            if (!toSend) {
                                toSend = std::chrono::duration_cast<std::chrono::milliseconds>(now - _policy.lastEventTimestamp).count() > _policy.timeLimit;
                            }

                            if (toSend) {
                                _policy.lastEventTimestamp = now;
                            }
                        }

                        if (toSend) {
                            sendEvent();
                        }
                    });
        });
    }
    ~JoinImpl() {
        std::cerr << "DESTRUCTOR JoinImpl <-------------\n";
    }

    JoinImpl(const JoinImpl &)     = delete;
    JoinImpl              &operator=(const JoinImpl &) = delete;

    rxcpp::subscriber<int> operator()(rxcpp::subscriber<int> s) const {
        return rxcpp::make_subscriber<int>([s](const int &next) { s.on_next(std::move(next + 1)); }, [&s](const std::exception_ptr &e) { s.on_error(e); }, [&s]() { s.on_completed(); });
    }

    const auto &stream() & {
        return _impl.stream();
    }
};
} // namespace Join

template<typename Policy, typename... InputObservables>
auto join(InputObservables &&...input) {
    // TODO: Remove new, make values
    return new Join::JoinImpl<Policy, typename std::remove_cvref_t<InputObservables>::value_type...>(std::forward<InputObservables>(input)...);
}

} // namespace opencmw::disruptor::rx
