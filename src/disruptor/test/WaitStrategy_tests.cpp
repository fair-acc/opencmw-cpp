
#include <catch2/catch.hpp>

#include <disruptor/WaitStrategy.hpp>
 using namespace opencmw::disruptor;

 TEST_CASE("WaitStrategy concept tests", "[Disruptor]") {
    REQUIRE(isWaitStrategy<BlockingWaitStrategy>);
    REQUIRE(isWaitStrategy<BusySpinWaitStrategy>);
    REQUIRE(isWaitStrategy<SleepingWaitStrategy>);
    REQUIRE(isWaitStrategy<SleepingWaitStrategy>);
    REQUIRE(isWaitStrategy<SpinWaitWaitStrategy>);
    REQUIRE(isWaitStrategy<TimeoutBlockingWaitStrategy>);
    REQUIRE(isWaitStrategy<YieldingWaitStrategy>);
    REQUIRE(not isWaitStrategy<int>);

    REQUIRE(WaitStrategyConcept<BlockingWaitStrategy>);
    REQUIRE(WaitStrategyConcept<BusySpinWaitStrategy>);
    REQUIRE(WaitStrategyConcept<SleepingWaitStrategy>);
    REQUIRE(WaitStrategyConcept<SleepingWaitStrategy>);
    REQUIRE(WaitStrategyConcept<SpinWaitWaitStrategy>);
    REQUIRE(WaitStrategyConcept<TimeoutBlockingWaitStrategy>);
    REQUIRE(WaitStrategyConcept<YieldingWaitStrategy>);
    REQUIRE(not WaitStrategyConcept<int>);
}

