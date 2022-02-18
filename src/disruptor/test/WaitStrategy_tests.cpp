
#include <catch2/catch.hpp>

#include <disruptor/WaitStrategy.hpp>
using namespace opencmw::disruptor;

template<WaitStrategyConcept auto wait = NoWaitStrategy()>
struct TestStruct {
    [[nodiscard]] constexpr bool test() const noexcept {
        // wait.waitFor(std::declval<std::int64_t>(), std::declval<Sequence &>(), std::declval<ISequence &>(), std::declval<ISequenceBarrier &>());
        //         wait.waitFor(std::int64_t{}, Sequence{}, std::declval<ISequence &>(), std::declval<ISequenceBarrier &>());
        //         if constexpr ( requires { wait.test();}) {
        //             return true;
        //         }
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

    REQUIRE(WaitStrategyConcept<BlockingWaitStrategy>);
    REQUIRE(WaitStrategyConcept<BusySpinWaitStrategy>);
    REQUIRE(WaitStrategyConcept<SleepingWaitStrategy>);
    REQUIRE(WaitStrategyConcept<SleepingWaitStrategy>);
    REQUIRE(WaitStrategyConcept<SpinWaitWaitStrategy>);
    REQUIRE(WaitStrategyConcept<TimeoutBlockingWaitStrategy>);
    REQUIRE(WaitStrategyConcept<YieldingWaitStrategy>);
    REQUIRE(not WaitStrategyConcept<int>);

    TestStruct a;
    REQUIRE(a.test());
}
