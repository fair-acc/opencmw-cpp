#include <catch2/catch.hpp>

#include <SpinWait.hpp>

template class opencmw::SpinWait<10, 5, 20>;

TEST_CASE("SpinWait basic tests", "[SpinWait]") {
    opencmw::SpinWait<10, 5, 20> waiter;
    REQUIRE(waiter.count() == 0);
    REQUIRE(!waiter.nextSpinWillYield());

    waiter.spinOnce();
    REQUIRE(waiter.count() == 1);
    REQUIRE(!waiter.nextSpinWillYield());

    for (int i = 0; i < 9; ++i) {
        waiter.spinOnce();
        REQUIRE(!waiter.nextSpinWillYield());
    }
    waiter.spinOnce();
    REQUIRE(waiter.nextSpinWillYield());

    REQUIRE_NOTHROW(waiter.reset());
    REQUIRE(waiter.getTickCount() > 0);

    constexpr auto validate = []() noexcept -> bool { static int counter = 0; return ++counter > 3; };
    REQUIRE(waiter.spinUntil(validate));
    REQUIRE_THROWS(waiter.spinUntil(validate, -2));
    REQUIRE(waiter.spinUntil(validate, 10));
    REQUIRE(!waiter.spinUntil([]() noexcept -> bool { return false; }, 0));
}

TEST_CASE("AtomicMutex basic tests", "[AtomicMutex]") {
    using namespace opencmw;
    opencmw::AtomicMutex<NO_SPIN_WAIT> mutex1;
    REQUIRE_NOTHROW(mutex1.lock());
    REQUIRE_NOTHROW(mutex1.unlock());

    opencmw::AtomicMutex<SpinWait<10, 5, 20>> mutex2;
    REQUIRE_NOTHROW(mutex2.lock());
    REQUIRE_NOTHROW(mutex2.unlock());

    REQUIRE_NOTHROW(opencmw::AtomicMutex());
}