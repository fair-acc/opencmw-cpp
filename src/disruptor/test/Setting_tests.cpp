#include <catch2/catch.hpp>

#include <BasicSetting.hpp>
#include <Transactions.hpp>

TEST_CASE("BasicSetting Real-Time tests", "[BasicSetting]") {
    using namespace opencmw;
    using opencmw::MutableOption::RealTimeMutable;
    BasicSetting<int, RealTimeMutable> setting;

    SECTION("setting value in real-time thread") {
        auto guardedValue = setting.accessGuard<AccessType::RealTime>();
        REQUIRE(guardedValue == 0);
        REQUIRE(!std::is_const_v<std::remove_reference_t<decltype(guardedValue.get())>>);
        guardedValue = 40;
        guardedValue += 2;
        REQUIRE(guardedValue == 42);
    }
    setting.replace<AccessType::RealTime>(43);

    SECTION("getting value in non-real-time thread") {
        auto guardedValue = setting.accessGuard<AccessType::NonRealTime>();
        REQUIRE(guardedValue == 43);
        REQUIRE(std::is_const_v<std::remove_reference_t<decltype(guardedValue.get())>>);
        // guardedValue = 43; // should not compile
        REQUIRE(guardedValue == 43);
    }

    REQUIRE_NOTHROW([]<typename T>(BasicSetting<T, RealTimeMutable> &s) {
        auto guardedValue = s.template accessGuard<AccessType::NonRealTime>();
        REQUIRE(guardedValue == 43);
    });
    REQUIRE_NOTHROW([]<typename T>(BasicSetting<T, RealTimeMutable> &s) {
        auto guardedValue = s.nonRealTimeAccessGuard();
        REQUIRE(guardedValue == 43);
    });
}

TEST_CASE("BasicSetting Non-Real-Time tests", "[BasicSetting]") {
    using namespace opencmw;
    using opencmw::MutableOption::NonRealTimeMutable;
    BasicSetting<int, NonRealTimeMutable> setting;

    SECTION("setting value in real-time thread") {
        auto guardedValue = setting.accessGuard<AccessType::NonRealTime>();
        REQUIRE(guardedValue == 0);
        REQUIRE(!std::is_const_v<std::remove_reference_t<decltype(guardedValue.get())>>);
        guardedValue = 42;
        REQUIRE(guardedValue == 42);
    }

    setting.replace<AccessType::NonRealTime>(43);

    SECTION("getting value in non-real-time thread") {
        auto guardedValue = setting.accessGuard<AccessType::RealTime>();
        REQUIRE(guardedValue == 43);
        REQUIRE(std::is_const_v<std::remove_reference_t<decltype(guardedValue.get())>>);
        // guardedValue = 43; // should not compile
        REQUIRE(guardedValue == 43);
    }

    REQUIRE_NOTHROW([]<typename T>(BasicSetting<T, NonRealTimeMutable> &s) {
        auto guardedValue = s.template accessGuard<AccessType::RealTime>();
        REQUIRE(guardedValue == 43);
    });
    REQUIRE_NOTHROW([]<typename T>(BasicSetting<T, NonRealTimeMutable> &s) {
        auto guardedValue = s.realTimeAccessGuard();
        REQUIRE(guardedValue == 43);
    });
}

TEST_CASE("SettingBase basic tests", "[SettingBase]") {
    using namespace std::chrono_literals;
    opencmw::SettingBase<int, int, std::string, 1024> a;

    REQUIRE(a.nHistory() == 1);
    auto [r1, t1] = a.commit(42);
    REQUIRE(r1);
    REQUIRE(a.nHistory() == 2);
    auto [r2, t2] = a.commit(43);
    REQUIRE(r2);
    REQUIRE(a.nHistory() == 3);

    REQUIRE(a.get() == 43);
    REQUIRE(a.get(t2) == 43);
    REQUIRE(a.get(-1) == 42);
    REQUIRE(a.get(t1) == 42);

    REQUIRE(a.getPendingTransactions().size() == 0);
    auto [r3, t3] = a.stage(53, "transactionToken#1");
    REQUIRE(!r3);
    REQUIRE(a.getPendingTransactions().size() == 1);
    REQUIRE(a.nHistory() == 3);
    auto [r4, t4] = a.commit(40);
    REQUIRE(r4);
    REQUIRE(a.nHistory() == 4);
    REQUIRE(a.get(t4) == 40);
    REQUIRE(a.get(t3) == 43); // transaction not yet committed
    REQUIRE(a.get() == 40);   // transaction not yet committed

    auto [r5, t5] = a.commit("transactionToken#1"); // commit transaction
    REQUIRE(r5);
    REQUIRE(a.nHistory() == 5);
    REQUIRE(a.get() == 53);
    REQUIRE(a.get(t5) == 53);
    REQUIRE(a.getPendingTransactions().size() == 0);

    auto [r6, t] = a.stage(80, "transactionToken#2");
    REQUIRE(!r6);
    REQUIRE(a.nHistory() == 5);
    REQUIRE(a.getPendingTransactions().size() == 1);
    REQUIRE(a.getPendingTransactions()[0] == "transactionToken#2");
    REQUIRE(!a.retireStaged("unknownToken"));
    REQUIRE(a.nHistory() == 5);
    REQUIRE(a.getPendingTransactions().size() == 1);
    REQUIRE(a.retireStaged("transactionToken#2"));
    REQUIRE(a.nHistory() == 5);
    REQUIRE(a.getPendingTransactions().size() == 0);

    // test staging duplicate transaction token -- last should survive
    REQUIRE(a.getPendingTransactions().size() == 0);
    REQUIRE(not a.stage(80, "transactionToken#2").isCommitted);
    REQUIRE(a.getPendingTransactions().size() == 1);
    REQUIRE(not a.stage(81, "transactionToken#2").isCommitted);
    REQUIRE(a.getPendingTransactions().size() == 1);
    REQUIRE(a.commit("transactionToken#2").isCommitted);
    REQUIRE(a.get() == 81);
}

TEST_CASE("SettingBase time-out and expiry", "[SettingBase]") {
    using namespace std::chrono_literals;
    opencmw::SettingBase<int, int, std::string, 16, std::chrono::milliseconds, 100> a;
    REQUIRE(a.nHistory() == 1U);

    for (int i = 0; i < 8; ++i) {
        auto [r1, t1] = a.commit(FWD(i));
        REQUIRE(r1);
        REQUIRE(a.nHistory() == static_cast<std::size_t>(i + 2));
    }

    for (int i = 0; i < 8; ++i) {
        auto [r1, t1] = a.commit(FWD(i));
        REQUIRE(r1);
    }
    REQUIRE(a.nHistory() == 16 - 8 + 1);

    opencmw::SettingBase<int, int, std::string, 16, std::chrono::milliseconds, -1, 100> b;
    REQUIRE(b.getPendingTransactions().size() == 0);
    for (int i = 0; i < 5; ++i) {
        auto [r1, t1] = b.stage(FWD(i), fmt::format("token#{}", i));
        REQUIRE(!r1);
    }
    REQUIRE(b.getPendingTransactions().size() == 5);

    std::this_thread::sleep_for(200ms); // wait for timeout to expire for both 'a' and 'b'
    REQUIRE(a.nHistory() == static_cast<std::size_t>(16 - 8 + 1));
    a.retireExpired();
    REQUIRE(a.nHistory() == 1U);

    REQUIRE(b.getPendingTransactions().size() == 5);
    b.retireExpired();
    REQUIRE(b.getPendingTransactions().size() == 0);

    REQUIRE(a.nHistory() == 1U);
    REQUIRE(a.modifySetting([](const int &oldValue) -> int { return oldValue; }));
    REQUIRE(a.nHistory() == 2U);
}

TEST_CASE("SettingBase constructors", "[SettingBase]") {
    opencmw::Setting<int, 128> a;
    REQUIRE(a.nHistory() == 1);
    opencmw::Setting<int, 128, std::chrono::milliseconds, 100> b;
    REQUIRE(b.nHistory() == 1);
    opencmw::TransactionSetting<int, std::string, 128> c;
    REQUIRE(c.nHistory() == 1);
    opencmw::TransactionSetting<int, std::string, 128, std::chrono::milliseconds, 100> d;
    REQUIRE(d.nHistory() == 1);
    opencmw::CtxSetting<int, std::string, 128> e;
    REQUIRE(e.nHistory() == 1);
}

TEST_CASE("CtxSetting", "[SettingBase]") {
    using opencmw::NullTimingCtx;
    using opencmw::TimingCtx;
    opencmw::CtxSetting<int, std::string, 16> a;
    REQUIRE(a.nHistory() == 1);

    auto [r1, t1] = a.commit(NullTimingCtx, 42);
    REQUIRE(r1);
    REQUIRE(a.nHistory() == 2);
    auto [r2, t2] = a.commit(NullTimingCtx, 43);
    REQUIRE(r2);
    REQUIRE(a.nHistory() == 3);

    REQUIRE(a.get().second == 43);
    REQUIRE(a.get(NullTimingCtx, t2).second == 43);
    REQUIRE(a.get(NullTimingCtx, -1).second == 42);
    REQUIRE(a.get(NullTimingCtx, t1).second == 42);

    auto [r3, t3] = a.commit(TimingCtx(1), 55);
    REQUIRE(r3);
    REQUIRE(a.nHistory() == 4);
    REQUIRE(a.get(TimingCtx(1)).second == 55);
    REQUIRE(a.get(TimingCtx(2)).first != TimingCtx(2)); // non-matching context

    auto [r4, t4] = a.commit(TimingCtx(2), 56);
    REQUIRE(r4);
    REQUIRE(a.nHistory() == 5);
    REQUIRE(a.get(NullTimingCtx).second == 43);
    REQUIRE(a.get(TimingCtx(1)).second == 55);
    REQUIRE(a.get(TimingCtx(2)).second == 56);

    REQUIRE(a.commit(TimingCtx(1, 1, 1, 1), 101).isCommitted);
    REQUIRE(a.commit(TimingCtx(1, 1, 1), 102).isCommitted);
    REQUIRE(a.commit(TimingCtx(1, 1), 103).isCommitted);
    REQUIRE(a.get(TimingCtx(1)).second == 55);
    REQUIRE(a.get(TimingCtx(1, 1, 1, 1)).second == 101);
    REQUIRE(a.get(TimingCtx(1, 1, 1, 1)).first == TimingCtx(1, 1, 1, 1));
    REQUIRE(a.get(TimingCtx(1, 1, 1, 2)).second == 102);
    REQUIRE(a.get(TimingCtx(1, 1, 1, 2)).first == TimingCtx(1, 1, 1));
    REQUIRE(a.get(TimingCtx(1, 1, 2, 2)).second == 103);
    REQUIRE(a.get(TimingCtx(1, 1, 2, 2)).first == TimingCtx(1, 1));

    // fill settings with high-update for a particular chain
    for (int i = 0; i < 100; i++) {
        REQUIRE(a.commit(TimingCtx(1), 55 + i).isCommitted);
    }
    REQUIRE(a.nHistory() == 9);
    REQUIRE(a.commit(TimingCtx(1), 55).isCommitted);

    // check that unrelated old settings prevailed
    REQUIRE(a.get(TimingCtx(1, 1, 1, 1)).second == 101);
    REQUIRE(a.get(TimingCtx(1, 1, 1, 1)).first == TimingCtx(1, 1, 1, 1));
    REQUIRE(a.get(TimingCtx(1, 1, 1, 2)).second == 102);
    REQUIRE(a.get(TimingCtx(1, 1, 1, 2)).first == TimingCtx(1, 1, 1));
    REQUIRE(a.get(TimingCtx(1, 1, 2, 2)).second == 103);
    REQUIRE(a.get(TimingCtx(1, 1, 2, 2)).first == TimingCtx(1, 1));

    // explicitely retire a given setting
    REQUIRE(a.nCtxHistory() == 6);
    REQUIRE(a.get(TimingCtx(1, 1, 1, 2)).second == 102);
    REQUIRE(not a.retire<true>(TimingCtx(1, 1, 1, 2)));
    REQUIRE(a.get(TimingCtx(1, 1, 1, 2)).second == 102);
    REQUIRE(a.retire<true>(TimingCtx(1, 1, 1)));
    REQUIRE(a.nCtxHistory() == 5);
    REQUIRE(a.get(TimingCtx(1, 1, 1, 2)).second == 103);
    REQUIRE(a.retire<true>(TimingCtx(1, 1)));
    REQUIRE(a.nCtxHistory() == 4);
    REQUIRE(a.get(TimingCtx(1, 1, 1, 2)).second == 55);

    REQUIRE(a.commit(TimingCtx(1, 1, 1, 1), 101).isCommitted);
    REQUIRE(a.commit(TimingCtx(1, 1, 1), 102).isCommitted);
    REQUIRE(a.commit(TimingCtx(1, 1), 103).isCommitted);
    REQUIRE(a.commit(TimingCtx(1), 104).isCommitted);
    REQUIRE(a.nCtxHistory() == 6);
    REQUIRE(a.retire<false>(TimingCtx(1, 1, 1)));
    REQUIRE(a.nCtxHistory() == 4);
    REQUIRE(a.get(TimingCtx(1, 1, 1, 2)).second == 103);

    // check transactions
    REQUIRE(a.getPendingTransactions().size() == 0);
    REQUIRE(not a.stage(TimingCtx(1), 555, "token#1").isCommitted);
    REQUIRE(not a.stage(TimingCtx(1), 556, "token#2").isCommitted);
    REQUIRE(a.get(TimingCtx(1)).second == 104);

    REQUIRE(a.commit("token#1").isCommitted);
    REQUIRE(a.get(TimingCtx(1)).second == 555);
    REQUIRE(a.commit("token#2").isCommitted);
    REQUIRE(a.get(TimingCtx(1)).second == 556);
}

TEST_CASE("CtxSetting time-out and expiry", "[SettingBase]") {
    using namespace std::chrono_literals;
    using opencmw::NullTimingCtx;
    using opencmw::TimingCtx;
    opencmw::CtxSetting<int, std::string, 16, std::chrono::milliseconds, 100> a;
    REQUIRE(a.nHistory() == 1U);

    for (int i = 0; i < 8; ++i) {
        REQUIRE(a.commit(TimingCtx(1), 55 + i).isCommitted);
        REQUIRE(a.nHistory() == static_cast<std::size_t>(i + 2));
    }
    REQUIRE(a.commit(TimingCtx(1), 55).isCommitted);
    REQUIRE(a.commit(TimingCtx(2), 55).isCommitted);
    REQUIRE(a.commit(TimingCtx(3), 55).isCommitted);
    REQUIRE(a.nHistory() == 16 - 8 + 1);
    REQUIRE(a.nCtxHistory() == 3);

    for (int i = 0; i < 8; ++i) {
        REQUIRE(a.commit(TimingCtx(1), 55 + i).isCommitted);
    }
    REQUIRE(a.nHistory() == 16 - 8 + 1);

    REQUIRE(a.nCtxHistory() == 3);
    opencmw::CtxSetting<int, std::string, 16, std::chrono::milliseconds, -1, 100> b;
    REQUIRE(b.getPendingTransactions().size() == 0);
    for (int i = 0; i < 6; ++i) {
        REQUIRE(not b.stage(TimingCtx(1), FWD(i), fmt::format("token#{}", i)).isCommitted);
    }
    REQUIRE(b.retireStaged("token#5"));
    REQUIRE(b.getPendingTransactions().size() == 5);

    std::this_thread::sleep_for(200ms);        // wait for timeout to expire for both 'a' and 'b'
    REQUIRE(a.get(TimingCtx(1)).second == 62); // update read for CID == 1 -> others should be expired
    REQUIRE(a.nHistory() == static_cast<std::size_t>(16 - 8 + 1));
    REQUIRE(a.nCtxHistory() == 3);
    a.retireExpired();
    REQUIRE(a.nHistory() == 1U);
    REQUIRE(a.nCtxHistory() == 1);

    REQUIRE(b.getPendingTransactions().size() == 5);
    b.retireExpired();
    REQUIRE(b.getPendingTransactions().size() == 0);
}

namespace detail {
struct NotCopyable {
    int value     = -1;
    NotCopyable() = default;
    explicit NotCopyable(int i)
        : value(i) {}
    NotCopyable(const NotCopyable &)            = delete;
    NotCopyable &operator=(const NotCopyable &) = delete;
    NotCopyable(NotCopyable &&other) noexcept {
        std::cout << "move constructor called" << std::endl;
        if (this == &other) {
            return;
        }
        std::swap(value, other.value);
    };
    NotCopyable &operator=(NotCopyable &&rhs) noexcept {
        std::cout << "move assignment called" << std::endl;
        std::swap(value, rhs.value);
        return *this;
    }
    ~NotCopyable() { std::cout << "destructor called" << std::endl; }

    explicit(false) constexpr operator int const &() const noexcept { return value; }
    [[nodiscard]] auto        operator<=>(const NotCopyable &other) const noexcept = default;
};
} // namespace detail

TEST_CASE("CtxSetting check reference semantic", "[SettingBase]") {
    using detail::NotCopyable;
    using opencmw::TimingCtx;
    opencmw::CtxSetting<NotCopyable, std::string, 16> a;
    std::cout << "constructed" << std::endl;
    REQUIRE(a.commit(TimingCtx(1), NotCopyable(42)).isCommitted);
    std::cout << "committed - NotCopyable(42)" << std::endl;
    REQUIRE(a.commit(TimingCtx(2), NotCopyable(43)).isCommitted);
    std::cout << "committed - NotCopyable(43)" << std::endl;
    REQUIRE(a.commit(TimingCtx(1), NotCopyable(44)).isCommitted);
    std::cout << "committed - NotCopyable(44)" << std::endl;
    REQUIRE(a.nCtxHistory() == 2);
    REQUIRE(a.get(TimingCtx(1)).first == TimingCtx(1));
    REQUIRE(*(a.get(TimingCtx(1)).second.value) == 44);
}
