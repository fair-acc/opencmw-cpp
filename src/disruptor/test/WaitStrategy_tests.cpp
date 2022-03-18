
#include <catch2/catch.hpp>

#include <disruptor/WaitStrategy.hpp>
using namespace opencmw::disruptor;

template<WaitStrategy auto wait = NoWaitStrategy()>
struct TestStruct {
    [[nodiscard]] constexpr bool test() const noexcept {
        return true;
    }
};

TEST_CASE("WaitStrategy concept tests", "[Disruptor]") {
    REQUIRE(isWaitStrategy<BlockingWaitStrategy>);
    REQUIRE(isWaitStrategy<BusySpinWaitStrategy>);
    REQUIRE(isWaitStrategy<SleepingWaitStrategy>);
    REQUIRE(isWaitStrategy<SleepingWaitStrategy>);
    REQUIRE(isWaitStrategy<SpinWaitWaitStrategy>);
    REQUIRE(isWaitStrategy<TimeoutBlockingWaitStrategy>);
    REQUIRE(isWaitStrategy<YieldingWaitStrategy>);
    REQUIRE(not isWaitStrategy<int>);

    REQUIRE(WaitStrategy<BlockingWaitStrategy>);
    REQUIRE(WaitStrategy<BusySpinWaitStrategy>);
    REQUIRE(WaitStrategy<SleepingWaitStrategy>);
    REQUIRE(WaitStrategy<SleepingWaitStrategy>);
    REQUIRE(WaitStrategy<SpinWaitWaitStrategy>);
    REQUIRE(WaitStrategy<TimeoutBlockingWaitStrategy>);
    REQUIRE(WaitStrategy<YieldingWaitStrategy>);
    REQUIRE(not WaitStrategy<int>);

    TestStruct a;
    REQUIRE(a.test());
}
