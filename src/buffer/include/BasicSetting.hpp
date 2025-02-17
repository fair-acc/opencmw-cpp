#ifndef OPENCMW_CPP_BASICSETTINGS_HPP
#define OPENCMW_CPP_BASICSETTINGS_HPP

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>

namespace opencmw {

enum class MutableOption {
    RealTimeMutable,
    NonRealTimeMutable
};

enum class AccessType {
    RealTime,
    NonRealTime
};

namespace detail {
template<typename T>
class RealtimeMutable;
template<typename T>
class NonRealtimeMutable;
} // namespace detail

/**
 * @brief BasicSetting is a class that synchronises access to a copyable type T object from multiple non-real-time
 * and a single real-time threads. The one designated real-time thread will never wait to get access to the object.
 *
 * The MutableOption determines whether the object is mutable by either the non-real-time threads or a single real-time thread only.
 * Changes to the setting is guarded by a scoped accessGuard (safe recommended use, similar to a ScopedLock), or low-level pair of
 * <code>acquire()</code> and <code>release()</code> (N.B. less safe, requires matching).
 *
 * <pre>
 * Example usage: @code
 * using namespace opencmw;
 * using opencmw::MutableOption::RealTimeMutable;
 * BasicSetting&#60;int, NonRealTimeMutable&#62; setting; &#x2f;&#x2f; N.B. here 'int' is the setting type
 *
 * { &#x2f;&#x2f; calling from non-real-time thread (N.B. mutable setting)
 *   auto guardedValue = setting.accessGuard&#60;AccessType::NonRealTime&#62;();
 *   REQUIRE(guardedValue == 0);
 *   guardedValue = 40;
 *   guardedValue += 2;
 *   REQUIRE(guardedValue == 42);
 * }
 *
 * setting.replace<AccessType::NonRealTime>(43);
 *
 * { &#x2f;&#x2f; calling from real-time thread (N.B. immutable setting)
 *   auto guardedValue = setting.accessGuard&#60;AccessType::RealTime&#62;();
 *   REQUIRE(guardedValue == 43);
 *   REQUIRE(std::is_const_v<std::remove_reference_t<decltype(guardedValue.get())>>);
 *   &#x2f;&#x2f; guardedValue = 43; &#x2f;&#x2f; should not compile
 *   REQUIRE(guardedValue == 43);
 * }
 * </pre>
 *
 * @tparam T object type to synchronise.
 * @tparam mutableBy whether the object is mutable by the non-real-time threads or real-time thread only.
 *
 * N.B. idea inspired by David Rowland's () and Fabian Renn-Giles's (@hogliux) "Real-Time 101" talk at Meeting C++ 2019, https://youtu.be/ndeN983j_GQ
 */
template<typename T, MutableOption mutableBy>
class BasicSetting {
    using SettingStoreType = std::conditional_t<mutableBy == MutableOption::RealTimeMutable, detail::RealtimeMutable<T>, detail::NonRealtimeMutable<T>>;
    SettingStoreType _settingStore;

public:
    template<typename... Args>
    explicit BasicSetting(Args &&...args)
        : _settingStore(std::forward<Args>(args)...) {}
    explicit BasicSetting(const T &obj)
        : _settingStore(obj) {}
    explicit BasicSetting(T &&obj)
        : _settingStore(std::move(obj)) {}

    /**
     * Returns a reference to T.
     *
     * For real-time threads this method is wait- and lock-free.
     * Multiple non-realtime threads can acquire an object at the same time.
     *
     * @tparam threadType the thread type that will be acquiring the object.
     * @return a reference to T
     *
     * This function must be matched with by release<threadType>() when you are finished using the object.
     * Alternatively, use the scoped accessGuard() helper method below.
     */
    template<AccessType threadType>
    [[nodiscard]] std::conditional_t<mutableBy == MutableOption::RealTimeMutable && threadType == AccessType::RealTime, T, const T> &acquire() noexcept {
        if constexpr (threadType == AccessType::RealTime) {
            return _settingStore.realtimeAcquire();
        }
        return _settingStore.nonRealtimeAcquire();
    }

    template<AccessType threadType>
    void release() noexcept {
        if constexpr (threadType == AccessType::RealTime) {
            _settingStore.realtimeRelease();
        } else {
            _settingStore.nonRealtimeRelease();
        }
    }

    template<AccessType threadType>
    [[nodiscard]] auto accessGuard() { return ScopedAccess<threadType>(this->_settingStore); }

    [[nodiscard]] auto nonRealTimeAccessGuard() { return ScopedAccess<AccessType::NonRealTime>(this->_settingStore); }
    [[nodiscard]] auto realTimeAccessGuard() { return ScopedAccess<AccessType::RealTime>(this->_settingStore); }

    template<AccessType threadType, typename... Args>
    void replace(Args &&...args) noexcept {
        if constexpr (threadType == AccessType::RealTime) {
            _settingStore.realtimeReplace(std::forward<Args>(args)...);
        } else {
            _settingStore.nonRealtimeReplace(std::forward<Args>(args)...);
        }
    }

    template<AccessType access>
    class ScopedAccess { // NOSONAR - class destructor is needed for guard functionality
        static constexpr bool enableMutableAccess = (access == AccessType::NonRealTime && mutableBy == MutableOption::NonRealTimeMutable) || (access == AccessType::RealTime && mutableBy == MutableOption::RealTimeMutable);
        using SettingType                         = typename std::conditional_t<enableMutableAccess, T, const T>;
        SettingStoreType &_parentSettingStore;
        SettingType      *_currentValue;

    public:
        ScopedAccess()                     = delete;
        ScopedAccess(const ScopedAccess &) = delete;
        ScopedAccess(ScopedAccess &&)      = delete;
        ScopedAccess &operator=(const ScopedAccess &) = delete;
        ScopedAccess &operator=(ScopedAccess &&) = delete;

        template<bool b = true>
        requires(access == AccessType::RealTime) explicit constexpr ScopedAccess(SettingStoreType &parent)
            : _parentSettingStore(parent), _currentValue(&_parentSettingStore.realtimeAcquire()) {}
        template<bool b = true>
        requires(access != AccessType::RealTime) explicit constexpr ScopedAccess(SettingStoreType &parent)
            : _parentSettingStore(parent), _currentValue(&_parentSettingStore.nonRealtimeAcquire()) {}
        constexpr ~ScopedAccess() noexcept { access == AccessType::RealTime ? _parentSettingStore.realtimeRelease() : _parentSettingStore.nonRealtimeRelease(); }

        // read-only access
        constexpr          operator const SettingType &() const noexcept { return *_currentValue; } // NOSONAR - implicit conversion intended
        constexpr const T &get() const noexcept { return _currentValue; }
        constexpr const T *operator->() const noexcept { return _currentValue; }

        // read-write access
        constexpr operator SettingType &() noexcept { return *_currentValue; } // NOSONAR - implicit conversion intended
        template<bool b = true>
        requires enableMutableAccess T &get() noexcept { return _currentValue; }
        template<bool b = true>
        requires enableMutableAccess T &operator=(const T &newVal) {
            *_currentValue = newVal;
            return *_currentValue;
        }
        template<bool b = true>
        requires enableMutableAccess T *operator->() noexcept { return _currentValue; }
    };
};

namespace detail {

template<typename T>
class NonRealtimeMutable {
    std::unique_ptr<T> _storage;
    std::atomic<T *>   _storagePtr;
    std::mutex         _nonRealtimeLock;
    std::unique_ptr<T> _rtStorageCopy;              // used for realtime access protected by CAS-loop
    T                 *_rtStorageCopyPtr = nullptr; // only accessed by realtime thread

public:
    NonRealtimeMutable()
        : _storage(std::make_unique<T>()), _storagePtr(_storage.get()) {}
    explicit NonRealtimeMutable(const T &obj)
        : _storage(std::make_unique<T>(obj)), _storagePtr(_storage.get()) {}
    explicit NonRealtimeMutable(T &&obj)
        : _storage(std::make_unique<T>(std::move(obj))), _storagePtr(_storage.get()) {}

    ~NonRealtimeMutable() { // NOSONAR - intended behaviour, implements access guard
        assert("object deleted while real-time thread holding the lock" && _storagePtr.load() != nullptr);

        while (_storagePtr.load() == nullptr)
            ;

        [[maybe_unused]] auto accquired = _nonRealtimeLock.try_lock(); // NOSONAR - RAII idiom implemented in BasicSetting::ScopedAccess

        ((void) (accquired));
        assert("unbalanced acquire/release calls before deleting this object" && accquired);

        _nonRealtimeLock.unlock(); // NOSONAR - RAII idiom implemented in BasicSetting::ScopedAccess
    }

    const T &realtimeAcquire() noexcept {
        assert("unbalanced acquire/release calls" && _storagePtr.load() != nullptr);
        _rtStorageCopyPtr = _storagePtr.exchange(nullptr);
        return *_rtStorageCopyPtr;
    }

    void realtimeRelease() noexcept {
        assert("unbalanced acquire/release calls" && _storagePtr.load() == nullptr);
        _storagePtr.store(_rtStorageCopyPtr);
    }

    T &nonRealtimeAcquire() {
        _nonRealtimeLock.lock(); // NOSONAR - RAII idiom implemented in BasicSetting::ScopedAccess
        _rtStorageCopy.reset(new T(*_storage));

        return *_rtStorageCopy.get();
    }

    void nonRealtimeRelease() {
        T *ptr;

        // block until realtime thread is done using the object
        do {
            ptr = _storage.get();
        } while (!_storagePtr.compare_exchange_weak(ptr, _rtStorageCopy.get()));

        _storage = std::move(_rtStorageCopy);
        _nonRealtimeLock.unlock(); // NOSONAR - RAII idiom implemented in BasicSetting::ScopedAccess
    }

    template<typename... Args>
    void nonRealtimeReplace(Args &&...args) {
        _nonRealtimeLock.lock(); // NOSONAR - RAII idiom implemented in BasicSetting::ScopedAccess
        _rtStorageCopy.reset(new T(std::forward<Args>(args)...));

        nonRealtimeRelease();
    }
};

template<typename T>
class RealtimeMutable {
    enum {
        INDEX_BIT   = (1U << 0),
        BUSY_BIT    = (1U << 1),
        NEWDATA_BIT = (1U << 2)
    };

    std::atomic<int> control = { 0 };
    std::array<T, 2> data;
    T                realtimeCopy;
    std::mutex       nonRealtimeLock;

    template<typename... Args>
    explicit RealtimeMutable(bool, Args &&...args)
        : data({ T(std::forward(args)...), T(std::forward(args)...) }), realtimeCopy(std::forward(args)...) {}
    unsigned acquireIndex() noexcept { return control.fetch_or(BUSY_BIT, std::memory_order_acquire) & INDEX_BIT; }
    void     releaseIndex(unsigned idx) noexcept { control.store(static_cast<int>((idx & INDEX_BIT) | NEWDATA_BIT), std::memory_order_release); }

public:
    RealtimeMutable() = default;

    explicit RealtimeMutable(const T &obj)
        : data({ obj, obj }), realtimeCopy(obj) {}

    ~RealtimeMutable() { // NOSONAR - intended behaviour, implements access guard
        assert("delete while in-use by real-time thread" && (control.load() & BUSY_BIT) == 0);
        while ((control.load() & BUSY_BIT) == 1)
            ;

        auto acquired = nonRealtimeLock.try_lock(); // NOSONAR(cpp:S5506) -- RAII idiom implemented in BasicSetting::ScopedAccess

        ((void) (acquired));
        assert("mismatched acquire/release" && acquired);

        nonRealtimeLock.unlock(); // NOSONAR(cpp:S5506) -- RAII idiom implemented in BasicSetting::ScopedAccess
    }

    T &realtimeAcquire() noexcept {
        return realtimeCopy;
    }

    void realtimeRelease() noexcept {
        unsigned idx = acquireIndex();
        data[idx]    = realtimeCopy;
        releaseIndex(idx);
    }

    template<typename... Args>
    void realtimeReplace(Args &&...args) {
        T    obj(std::forward<Args>(args)...);

        auto idx  = acquireIndex();
        data[idx] = std::move(obj);
        releaseIndex(idx);
    }

    const T &nonRealtimeAcquire() {
        nonRealtimeLock.lock(); // NOSONAR(cpp:S5506) -- RAII idiom implemented in BasicSetting::ScopedAccess
        auto current = control.load(std::memory_order_acquire);

        // there is new data so flip the indices around atomically ensuring we are not inside realtimeAssign
        if ((current & NEWDATA_BIT) != 0) {
            int newValue;

            do {
                // expect the realtime thread not to be inside the realtime-assign
                current &= ~BUSY_BIT;

                // realtime thread should flip index value and clear the new data bit
                newValue = (current ^ INDEX_BIT) & INDEX_BIT;
            } while (!control.compare_exchange_weak(current, newValue, std::memory_order_acq_rel));

            current = newValue;
        }

        // flip the index bit as we always use the index that the realtime thread is currently NOT using
        unsigned nonRealtimeIndex = (current & INDEX_BIT) ^ 1;

        return data[nonRealtimeIndex];
    }

    void nonRealtimeRelease() {
        nonRealtimeLock.unlock(); // NOSONAR(cpp:S5506) -- RAII idiom implemented in BasicSetting::ScopedAccess
    }
};

} // namespace detail
} // namespace opencmw

#endif // OPENCMW_CPP_BASICSETTINGS_HPP
