#include <TimingCtx.hpp>

#include <catch2/catch.hpp>

using opencmw::TimingCtx;

TEST_CASE("Basic TimingCtx tests", "[TimingCtx][basic]") {
    REQUIRE_NOTHROW(TimingCtx());
    REQUIRE_NOTHROW(TimingCtx("FAIR.SELECTOR.ALL"));
    REQUIRE(TimingCtx() == TimingCtx("FAIR.SELECTOR.ALL"));

    const auto timestamp = std::chrono::microseconds(1234);

    TimingCtx  ctx;
    REQUIRE_NOTHROW(ctx = TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=3", timestamp));
    REQUIRE(ctx == TimingCtx(0, 1, 2, 3, timestamp));
    REQUIRE(ctx != TimingCtx(0, 1, 2, 3));
    REQUIRE(ctx.toString() == "FAIR.SELECTOR.C=0:S=1:P=2:T=3");

    REQUIRE_THROWS_AS(TimingCtx("FAIR.SELECTOR.C0:S=1:P=2:T=3"), std::invalid_argument);
    REQUIRE_THROWS_AS(TimingCtx("FAIR.SELECTOR.X=1"), std::invalid_argument);

    REQUIRE_NOTHROW(ctx = TimingCtx("FAIR.SELECTOR.C=2", timestamp));
    REQUIRE(ctx.cid().has_value());
    REQUIRE(ctx.cid() == 2);
    REQUIRE(!ctx.sid());
    REQUIRE(!ctx.pid());
    REQUIRE(!ctx.gid());
    REQUIRE(ctx.bpcts() == timestamp);
}

TEST_CASE("Basic TimingCtx ALL selector tests", "[TimingCtx][all_selector]") {
    const auto timestamp = std::chrono::microseconds(1234);

    REQUIRE_NOTHROW(TimingCtx());
    REQUIRE_NOTHROW(TimingCtx("", timestamp));
    REQUIRE_NOTHROW(TimingCtx("ALL", timestamp));
    REQUIRE_NOTHROW(TimingCtx("FAIR.SELECTOR.ALL", timestamp));

    const auto fromNoString    = TimingCtx(timestamp);
    const auto fromEmptyString = TimingCtx("", timestamp);
    const auto fromAll         = TimingCtx("ALL", timestamp);
    const auto fromFSA         = TimingCtx("FAIR.SELECTOR.ALL", timestamp);

    REQUIRE(fromNoString.toString() == "FAIR.SELECTOR.ALL");
    REQUIRE(fromEmptyString.toString() == "FAIR.SELECTOR.ALL");
    REQUIRE(fromAll.toString() == "FAIR.SELECTOR.ALL");
    REQUIRE(fromFSA.toString() == "FAIR.SELECTOR.ALL");

    REQUIRE(fromNoString == fromEmptyString);
    REQUIRE(fromNoString == fromAll);
    REQUIRE(fromNoString == fromFSA);

    REQUIRE(fromNoString.bpcts() == timestamp);
    REQUIRE(fromEmptyString.bpcts() == timestamp);
    REQUIRE(fromAll.bpcts() == timestamp);
    REQUIRE(fromFSA.bpcts() == timestamp);
}

TEST_CASE("TimingCtx equality operator", "[TimingCtx][equality]") {
    const auto timestamp = std::chrono::microseconds(1234);
    const auto ctx       = TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=3", timestamp);
    REQUIRE(ctx == ctx);
    REQUIRE(ctx == TimingCtx(ctx));
    REQUIRE(ctx == TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=3", timestamp));
    REQUIRE(ctx != TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=3", timestamp + std::chrono::microseconds(1)));
    REQUIRE(ctx != TimingCtx("FAIR.SELECTOR.C=ALL:S=1:P=2:T=3", timestamp));
    REQUIRE(ctx != TimingCtx("FAIR.SELECTOR.C=0:S=ALL:P=2:T=3", timestamp));
    REQUIRE(ctx != TimingCtx("FAIR.SELECTOR.C=0:S=1:P=ALL:T=3", timestamp));
    REQUIRE(ctx != TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=ALL", timestamp));
}

TEST_CASE("TimingCtx matching tests", "[TimingCtx][matches]") {
    const auto timestamp = std::chrono::microseconds(1234);
    const auto ctx       = TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=3", timestamp);
    REQUIRE(ctx.matches(ctx));
    REQUIRE(ctx.matches(TimingCtx(ctx.toString())));
    REQUIRE(ctx.matches(TimingCtx("FAIR.SELECTOR.ALL")));
    REQUIRE(ctx.matchesWithBpcts(TimingCtx(0, {}, {}, {}, timestamp)));
    REQUIRE(ctx.matchesWithBpcts(TimingCtx(0, 1, {}, {}, timestamp)));
    REQUIRE(ctx.matchesWithBpcts(TimingCtx(0, 1, 2, {}, timestamp)));
    REQUIRE(ctx.matches(TimingCtx(0, 1, 2, {})));
    REQUIRE(ctx.matches(TimingCtx({}, 1, 2, {})));
    REQUIRE_FALSE(ctx.matches(TimingCtx(0, 0, 2, {})));
    REQUIRE_FALSE(ctx.matches(TimingCtx(0, 1, 0, {})));

    const auto ctx2 = TimingCtx("FAIR.SELECTOR.C=0:S=1", timestamp);
    REQUIRE(ctx2.cid() == 0);
    REQUIRE(ctx2.sid() == 1);
    REQUIRE(!ctx2.pid());
    REQUIRE(!ctx2.gid());
    REQUIRE(ctx.matches(TimingCtx(0, 1, {}, {})));
    REQUIRE(ctx.matches(TimingCtx(0, 1, {}, {}, timestamp)));
}
