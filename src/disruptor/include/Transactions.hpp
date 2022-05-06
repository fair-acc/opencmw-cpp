#ifndef OPENCMW_CPP_TRANSACTIONS_HPP
#define OPENCMW_CPP_TRANSACTIONS_HPP

#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <list>
#include <unordered_map>
#include <utility>

#include <disruptor/RingBuffer.hpp>
#include <opencmw.hpp>
#include <ReaderWriterLock.hpp>
#include <TimingCtx.hpp>

namespace opencmw {

namespace settings {
template<typename T>
struct alignas(disruptor::kCacheLine) node {
    using TimeStamp               = std::chrono::system_clock::time_point;
    std::shared_ptr<T> value      = std::make_shared<T>();
    TimeStamp          validSince = std::chrono::system_clock::now();
    mutable TimeStamp  lastAccess = std::chrono::system_clock::now();
    node()                        = default;
    explicit node(T &&in)
        : value(std::make_shared<T>(FWD(in))){};
    constexpr void            touch() const noexcept { lastAccess = std::chrono::system_clock::now(); }
    explicit(false) constexpr operator T const &() const noexcept { return *value; }
};
static_assert(sizeof(node<int>) % disruptor::kCacheLine == 0, "node size must be cache line aligned");
} // namespace settings

template<std::equality_comparable TransactionToken>
inline static const TransactionToken NullToken = TransactionToken{};

/**
 * @brief A thread-safe settings wrapper that supports multiple stage/commit transactions and history functionality.
 *
 * Example:
 * @code
 * opencmw::SettingBase<int,int, std::string, 16, std::chrono::seconds, 3600 * 24, 10> settings;
 *
 * auto [ok1, timeStamp1] = a.commit(42); // store 42 in settings
 * auto [ok2, timeStamp2] = a.commit(43); // store 43 in settings
 *
 * assert(settings.get() == 43); // get the latest value
 * assert(settings.get(timeStamp2) == 43); // get the first valid value since timeStamp2
 * assert(settings.get(-1) == 42); // get the second to last value
 * assert(settings.get(timeStamp1) == 42); // get the first valid value since timeStamp2
 *
 * auto [ok3, timeStamp3] = a.stage(53, "transactionToken#1"); // stage 53 in settings, N.B. setting is not yet committed
 * assert(settings.get() == 43); // get the latest value
 * auto [ok4, timeStamp4] = a.commit("transactionToken#1"); // commit transaction
 * assert(settings.get() == 53); // get the latest value
 * @endcode
 *
 * @tparam T is the user-supplied setting type, for simple settings U and T are identical (see CtxSettings for an example where it isn't).
 * @tparam U is the internally stored setting type that may include additional meta data.
 * @tparam TransactionToken unique identifier with which to store/commit transactions.
 * @tparam N_HISTORY the maximum number of setting history
 * @tparam TimeDiff the std::chrono::duration time-base for the time-outs
 * @tparam timeOut maximum time given in units of TimeDiff after which a setting automatically expires if unused. (default: -1 -> disabled)
 * @tparam timeOutTransactions maximum time given in units of TimeDiff after which a transaction automatically expires if not being committed. (default: -1 -> disabled)
 */
template<std::movable T, std::movable U = T, std::equality_comparable TransactionToken = std::string, std::size_t N_HISTORY = 1024, typename TimeDiff = std::chrono::seconds, int timeOut = -1, int timeOutTransactions = -1>
requires(opencmw::is_power2_v<N_HISTORY> &&N_HISTORY > 8) class SettingBase {
    using TimeStamp  = std::chrono::system_clock::time_point;
    using RingBuffer = opencmw::disruptor::RingBuffer<settings::node<U>, N_HISTORY, disruptor::BusySpinWaitStrategy, disruptor::MultiThreadedStrategy>;
    using Sequence   = disruptor::Sequence;
    //
    constexpr static std::size_t BUFFER_MARGIN                             = 8;
    alignas(disruptor::kCacheLine) std::shared_ptr<RingBuffer> _ringBuffer = std::make_shared<RingBuffer>();
    alignas(disruptor::kCacheLine) std::shared_ptr<Sequence> _sequenceHead = std::make_shared<Sequence>();
    alignas(disruptor::kCacheLine) std::shared_ptr<Sequence> _sequenceTail = std::make_shared<Sequence>(0);
    alignas(disruptor::kCacheLine) mutable ReaderWriterLock _historyLock{};
    alignas(disruptor::kCacheLine) mutable std::atomic<bool> _transactionListLock{ false };
    std::list<std::pair<TransactionToken, settings::node<T>>> _transactionList;
    alignas(disruptor::kCacheLine) const std::function<U(const U &, T &&)> _onCommit;

public:
    using Node = settings::node<U>;
    SettingBase()
        : SettingBase([](const U & /*old*/, T &&in) -> U { return static_cast<U>(std::move(in)); }){};
    template<class Fn>
    requires std::is_invocable_r_v<U, Fn &&, const U &, T &&>
    explicit SettingBase(Fn &&onCommit)
        : _onCommit(FWD(onCommit)) {
        _ringBuffer->tryPublishEvent([](Node &&eventData, std::int64_t) { eventData.value = std::make_shared<U>(U()); }); // init with default setting
        _sequenceHead->setValue(_sequenceHead->value());
        _ringBuffer->addGatingSequences(std::vector{ _sequenceHead, _sequenceTail });
    }
    [[nodiscard]] std::size_t nHistory() const noexcept {
        _historyLock.scopedGuard<ReaderWriterLockType::READ>();
        return static_cast<std::size_t>(_sequenceHead->value() - _sequenceTail->value() + 1);
    }
    ReaderWriterLock          &historyLock() noexcept { return _historyLock; }

    std::pair<bool, TimeStamp> stage(T &&t, const TransactionToken &transactionToken = NullToken<TransactionToken>, const TimeStamp &now = std::chrono::system_clock::now()) {
        if (transactionToken.empty()) {
            const auto result = _ringBuffer->tryPublishEvent([&t, this, &now](Node &&eventData, std::int64_t sequence) {
                const auto oldValue  = get();
                eventData.value      = std::make_shared<U>(_onCommit(*oldValue.value, FWD(t)));
                eventData.validSince = now;
                eventData.lastAccess = now;
                _sequenceHead->setValue(sequence);
            });
            retireExpired();

            return { result, now };
        }

        bool expected = false;
        while (std::atomic_compare_exchange_strong(&_transactionListLock, &expected, true)) // spin-lock
            ;
        if (auto it = std::ranges::find_if(_transactionList, [&transactionToken](const auto &pair) { return pair.first == transactionToken; }); it != _transactionList.end()) {
            it->second = settings::node<T>(FWD(t)); // update value of existing transaction
        } else {
            _transactionList.push_back(std::make_pair(transactionToken, settings::node<T>(FWD(t))));
        }
        std::atomic_store_explicit(&_transactionListLock, false, std::memory_order_release);

        retireExpired();
        return { false, now };
    }

    std::pair<bool, TimeStamp> commit(T &&t, const TimeStamp &now = std::chrono::system_clock::now()) {
        return stage(std::move(t), NullToken<TransactionToken>, now);
    }

    std::pair<bool, TimeStamp> commit(const TransactionToken &transactionToken = NullToken<TransactionToken>, const TimeStamp &now = std::chrono::system_clock::now()) {
        bool expected  = false;
        bool submitted = false;
        while (std::atomic_compare_exchange_strong(&_transactionListLock, &expected, true)) // spin-lock
            ;

        const auto [first, last] = std::ranges::remove_if(_transactionList, [&transactionToken, &submitted, this, &now](const auto &setting) {
            if (transactionToken == NullToken<TransactionToken> || setting.first == transactionToken) {
                commit(std::move(*setting.second.value), now);
                submitted = true;
                return true;
            }
            return false;
        });
        _transactionList.erase(first, last);
        std::atomic_store_explicit(&_transactionListLock, false, std::memory_order_release);

        return { submitted, now };
    }

    template<class Fn>
    requires std::is_invocable_r_v<U, Fn &&, const U &>
    bool
    modifySetting(Fn &&modFunction, const TimeStamp &now = std::chrono::system_clock::now()) {
        const auto result = _ringBuffer->tryPublishEvent([this, &modFunction, &now](Node &&eventData, std::int64_t sequence) {
            const auto oldValue  = get();
            eventData.value      = std::make_shared<U>(modFunction(*oldValue.value));
            eventData.validSince = now;
            eventData.lastAccess = now;
            _sequenceHead->setValue(sequence);
        });
        retireExpired();
        return result;
    }

    std::vector<TransactionToken> getPendingTransactions() const {
        std::vector<TransactionToken> result;
        bool                          expected = false;
        while (std::atomic_compare_exchange_strong(&_transactionListLock, &expected, true)) // spin-lock
            ;
        result.reserve(_transactionList.size());
        std::ranges::transform(_transactionList, std::back_inserter(result), [](const auto &setting) { return setting.first; });
        std::atomic_store_explicit(&_transactionListLock, false, std::memory_order_release);
        return result;
    }

    [[nodiscard]] Node get(const TimeStamp &timeStamp) const noexcept {
        auto lHead = _sequenceHead->value();
        auto guard = _historyLock.scopedGuard<ReaderWriterLockType::READ>(); // to prevent the writer/clean-up task to potentially expire a node at the tail
        while ((*_ringBuffer)[lHead].validSince > timeStamp && lHead != _sequenceTail->value()) {
            lHead--;
        }
        (*_ringBuffer)[lHead].touch();
        return (*_ringBuffer)[lHead]; // performs thread-safe copy of immutable object
    }

    [[nodiscard]] Node get(const std::int64_t idx = 0) const noexcept {
        auto       guard   = _historyLock.scopedGuard<ReaderWriterLockType::READ>(); // to prevent the writer/clean-up task to potentially expire a node at the tail
        const auto readIdx = _sequenceHead->value() + idx;
        assert(readIdx >= _sequenceTail->value() && "index out of range");
        (*_ringBuffer)[readIdx].touch();
        return (*_ringBuffer)[readIdx]; // performs thread-safe copy of immutable object
    }

    bool retireStaged(const TransactionToken &transactionToken = NullToken<TransactionToken>) {
        bool retired  = false;
        bool expected = false;
        while (std::atomic_compare_exchange_strong(&_transactionListLock, &expected, true)) // spin-lock
            ;

        auto [first, last] = std::ranges::remove_if(_transactionList, [&transactionToken, &retired, this](const auto &setting) {
            if (transactionToken == NullToken<TransactionToken> || setting.first == transactionToken) {
                retired = true;
                return true;
            }
            return false;
        });
        _transactionList.erase(first, last);
        std::atomic_store_explicit(&_transactionListLock, false, std::memory_order_release);

        return retired;
    }

    void retireExpired() {
        const auto now = std::chrono::system_clock::now();
        if (timeOutTransactions > 0) {
            // time-out old transactions
            bool expected = false;
            while (std::atomic_compare_exchange_strong(&_transactionListLock, &expected, true)) // spin-lock
                ;
            const auto [first, last] = std::ranges::remove_if(_transactionList, [&now, this](const auto &setting) { return setting.second.lastAccess - now + TimeDiff{ timeOutTransactions } < TimeDiff{ 0 }; });
            _transactionList.erase(first, last);
            std::atomic_store_explicit(&_transactionListLock, false, std::memory_order_release);
        }

        auto guard = _historyLock.scopedGuard<ReaderWriterLockType::WRITE>();
        // expire old settings based on count
        while (_sequenceTail->value() != _sequenceHead->value() && (_sequenceHead->value() - _sequenceTail->value() > static_cast<std::int64_t>(N_HISTORY - BUFFER_MARGIN))) {
            [[maybe_unused]] auto unusedTail = _sequenceTail->incrementAndGet();
        }
        // expire old settings based on time
        if constexpr (timeOut > 0) {
            auto tailPosition = _sequenceTail->value();
            while (tailPosition != _sequenceHead->value()) {
                if ((*_ringBuffer)[tailPosition].lastAccess - now + TimeDiff{ timeOut } < TimeDiff{ 0 }) {
                    tailPosition = _sequenceTail->incrementAndGet();
                } else {
                    tailPosition++;
                }
            }
        }
    }
};

template<std::movable T, std::size_t N_HISTORY, typename TimeDiff = std::chrono::seconds, int timeOut = -1>
requires(opencmw::is_power2_v<N_HISTORY> &&N_HISTORY > 8) class Setting : public SettingBase<T, T, std::string, N_HISTORY, TimeDiff, timeOut> {};

template<std::movable T, std::equality_comparable TransactionToken, std::size_t N_HISTORY = 1024, typename TimeDiff = std::chrono::seconds, int timeOut = -1, int timeOutTransactions = -1>
requires(opencmw::is_power2_v<N_HISTORY> &&N_HISTORY > 8) class TransactionSetting : public SettingBase<T, T, TransactionToken, N_HISTORY, TimeDiff, timeOut, timeOutTransactions> {};

/**
 * @brief A transactional setting that can be used to store a value of the user-defined type T for a given FAIR TimingCtx, user-defined transaction token, and for a given history length.
 *
 * Example:
 * @code
 * using opencmw::NullTimingCtx;
 * using opencmw::TimingCtx;
 * opencmw::CtxSetting<int, std::string, 16, std::chrono::seconds, 3600 * 24, 10> settings; // history length of 24 hours, time-out of 10 seconds
 * auto [ok1, timeStamp1]= settings.commit(NullTimingCtx, 42); // store 42 in settings
 * auto [ok2, timeStamp2] = a.commit(43); // store 43 in settings
 *
 * assert(settings.get().second == 43); // get the last committed value
 * assert(settings.get(NullTimingCtx, timeStamp2).second == 43); // get the first valid value since timeStamp2 for the NullTimingCtx
 * assert(settings.get(NullTimingCtx, -1).second == 42); // get the second last value for the NullTimingCtx
 * assert(settings.get(NullTimingCtx, t1).second == 42);
 *
 * assert(settings.commit(TimingCtx(1, 1, 1, 1), 101).first); // store 101 in settings for 'TimingCtx(cid=1, sid=1, pid=1, gid=1)'
 * assert(settings.commit(TimingCtx(1, 1, 1), 102).first); // store 102 in settings for 'TimingCtx(cid=1, sid=1, pid=1, gid=-1)'
 * assert(settings.commit(TimingCtx(1, 1), 103).first); // store 103 in settings for 'TimingCtx(cid=1, sid=1, pid=-1, gid=-1)'
 * assert(settings.get(TimingCtx(1)).second == 55); // get the last committed value for 'TimingCtx(cid=1, sid=1, pid=-1, gid=-1)'
 * assert(settings.get(TimingCtx(1, 1, 1, 1)).second == 101);
 * assert(settings.get(TimingCtx(1, 1, 1, 1)).first == TimingCtx(1, 1, 1, 1));
 * assert(settings.get(TimingCtx(1, 1, 1, 2)).second == 102); // no setting for 'gid=2' exists yet -> fall back to 'gid=-1'
 * assert(settings.get(TimingCtx(1, 1, 1, 2)).first == TimingCtx(1, 1, 1)); // assert that the fallback to 'gid=-1' is equivalent to the higher-level 'TimingCtx(cid=1, sid=1, pid=1)'
 * assert(settings.get(TimingCtx(1, 1, 2, 2)).second == 103); // no setting for 'pid=2' and 'gid=2' exists yet -> fall back to 'pid=gid=-1'
 * assert(settings.get(TimingCtx(1, 1, 2, 2)).first == TimingCtx(1, 1)); // assert correct fall-back
 *
 * assert(not settings.stage(TimingCtx(1), 555, "token#1").first);
 * assert(not settings.stage(TimingCtx(1), 556, "token#2").first);
 *
 * auto [r3, t3] = settings.commit(TimingCtx(1), 55); // store 55 in settings for the 'TimingCtx(cid=1)'
 * assert(settings.get(TimingCtx(1)).second == 55);
 * assert(settings.get(TimingCtx(2)).first != TimingCtx(2)); // non-matching context
 * assert(settings.commit("token#1").first);
 * assert(settings.get(TimingCtx(1)).second == 555);
 * assert(settings.commit("token#2").first);
 * assert(settings.get(TimingCtx(1)).second == 556);
 *
 * settings.retire<true>(TimingCtx(1, 1, 1)); // remove all settings for 'TimingCtx(cid=1, sid=1, pid=1)' -- exact match
 * settings.retire<false>(TimingCtx(1, 1, 1)); // remove all settings for 'TimingCtx(cid=1, sid=1, pid=1)' -- remove also dependent settings (i.e. ignore pid)
 * @endcode
 *
 *
 * @tparam T user-supplied type that is stored in the setting
 * @tparam TransactionToken user-supplied type that is used to identify a transaction
 * @tparam N_HISTORY history length of the setting
 * @tparam TimeDiff the std::chrono::duration time-base for the time-outs
 * @tparam timeOut maximum time given in units of TimeDiff after which a setting automatically expires if unused. (default: -1 -> disabled)
 * @tparam timeOutTransactions maximum time given in units of TimeDiff after which a transaction automatically expires if not being committed. (default: -1 -> disabled)
 */
template<std::movable T, std::equality_comparable TransactionToken, std::size_t N_HISTORY = 1024, typename TimeDiff = std::chrono::seconds, int timeOut = -1, int timeOutTransactions = -1>
requires(opencmw::is_power2_v<N_HISTORY> &&N_HISTORY > 8) class CtxSetting {
    using TimeStamp = std::chrono::system_clock::time_point;
    using Setting   = std::pair<TimingCtx, T>;
    //
    SettingBase<std::pair<TimingCtx, T>, std::unordered_map<TimingCtx, settings::node<T>>, TransactionToken, N_HISTORY, TimeDiff, timeOut, timeOutTransactions> _setting{
        [](const std::unordered_map<TimingCtx, settings::node<T>> &oldMap, std::pair<TimingCtx, T> &&newValue) -> std::unordered_map<TimingCtx, settings::node<T>> {
            auto newMap = oldMap;
            if (auto it = newMap.find(newValue.first); it != newMap.end()) {
                it->second = settings::node(FWD(newValue.second));
            } else {
                newMap.emplace(newValue.first, settings::node(FWD(std::move(newValue.second))));
            }
            return newMap;
        }
    };

public:
    using Node                         = settings::node<T>;
    CtxSetting()                       = default;
    CtxSetting(const CtxSetting &)     = delete;
    CtxSetting                &operator=(const CtxSetting &) = delete;

    std::pair<bool, TimeStamp> stage(const TimingCtx &timingCtx, T &&newValue, const TransactionToken &transactionToken = NullToken<TransactionToken>) {
        return _setting.stage({ timingCtx, FWD(newValue) }, transactionToken);
    }
    [[maybe_unused]] bool                            retireStaged(const TransactionToken &transactionToken = NullToken<TransactionToken>) { return _setting.retireStaged(transactionToken); }
    std::pair<bool, TimeStamp>                       commit(const TimingCtx &timingCtx, T &&newValue) { return stage(timingCtx, FWD(newValue)); }
    std::pair<bool, TimeStamp>                       commit(const TransactionToken &transactionToken = NullToken<TransactionToken>) { return _setting.commit(transactionToken); }

    [[nodiscard]] std::pair<TimingCtx, const Node &> get(const TimingCtx &timingCtx = NullTimingCtx, const std::int64_t idx = 0) const noexcept { return get(*_setting.get(idx).value, timingCtx); }
    [[nodiscard]] std::pair<TimingCtx, const Node &> get(const TimingCtx &timingCtx, const TimeStamp &timeStamp) const noexcept { return get(*_setting.get(timeStamp).value, timingCtx); }
    [[nodiscard]] std::size_t                        nHistory() const { return _setting.nHistory(); }
    [[nodiscard]] std::size_t                        nCtxHistory(const std::int64_t idx = 0) const { return _setting.get(idx).value->size(); }
    [[nodiscard]] std::vector<TransactionToken>      getPendingTransactions() const { return _setting.getPendingTransactions(); }
    void                                             retireExpired() {
        _setting.historyLock().template scopedGuard<ReaderWriterLockType::WRITE>();
        _setting.retireExpired();
        retireOldSettings(*_setting.get().value);
    }
    template<bool exactMatch = false>
    [[maybe_unused]] bool retire(const TimingCtx &ctx) {
        bool modifiedSettings = false;
        _setting.modifySetting([&ctx, &modifiedSettings](const std::unordered_map<TimingCtx, settings::node<T>> &oldSetting) {
            auto newSetting = oldSetting;
            if constexpr (exactMatch) {
                modifiedSettings = std::erase_if(newSetting, [&ctx](const std::pair<TimingCtx, settings::node<T>> &pair) { return pair.first == ctx; });
            } else {
                modifiedSettings = std::erase_if(newSetting, [&ctx](const auto &pair) { return pair.first.matches(ctx); });
            }
            return newSetting;
        });
        return modifiedSettings;
    }

private:
    [[nodiscard]] std::pair<TimingCtx, const Node &> get(const auto &settingsMap, const TimingCtx &timingCtx) const noexcept {
        for (const auto &[key, value] : settingsMap) {
            if (key == timingCtx) {
                value.touch();
                return { timingCtx, value };
            }
        }
        std::chrono::microseconds bpcts{ timingCtx.bpcts.value() };
        // did not find an exact match the setting for the specific timing context
        const TimingCtx withoutGID(timingCtx.cid(), timingCtx.sid(), timingCtx.pid(), -1, bpcts); // eliminate the gid variability
        for (const auto &[key, value] : settingsMap) {
            if (key.matches(withoutGID) && key.gid() == -1) {
                value.touch();
                return { withoutGID, value };
            }
        }
        const TimingCtx withoutPID(timingCtx.cid(), timingCtx.sid(), -1, -1, bpcts); // eliminate the pid & gid variability
        for (const auto &[key, value] : settingsMap) {
            if (key.matches(withoutPID) && key.gid() == -1 && key.pid() == -1) {
                value.touch();
                return { withoutPID, value };
            }
        }
        const TimingCtx withoutSID(timingCtx.cid(), -1, -1, -1, bpcts); // eliminate the sid & pid & gid variability
        for (const auto &[key, value] : settingsMap) {
            if (key.matches(withoutSID) && key.gid() == -1 && key.pid() == -1 && key.sid() == -1) {
                value.touch();
                return { withoutSID, value };
            }
        }
        // did not find an exact match the setting for the specific timing context
        if (const TimingCtx globalContext(-1, -1, -1, -1, bpcts); settingsMap.find(globalContext) != settingsMap.end()) {
            settingsMap.at(globalContext).touch();
            return { NullTimingCtx, settingsMap.at(globalContext) }; // explicitly stored non-multiplexed value
        }
        return { NullTimingCtx, settings::node<T>(T{}) }; // implicitly stored non-multiplexed value
    }

    void retireOldSettings(auto &settingsMap) const noexcept {
        const auto now = std::chrono::system_clock::now();
        for (auto it = settingsMap.begin(); it != settingsMap.end();) {
            if (it->second.lastAccess - now + TimeDiff{ timeOut } < TimeDiff{ 0 }) {
                it = settingsMap.erase(it);
            } else {
                ++it;
            }
        }
    }
};

} // namespace opencmw

#endif // OPENCMW_CPP_TRANSACTIONS_HPP
