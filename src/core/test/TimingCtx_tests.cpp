#include <Debug.hpp>
#include <TimingCtx.hpp>

#include <catch2/catch.hpp>

using opencmw::TimingCtx;

TEST_CASE("Basic TimingCtx tests", "[TimingCtx][basic]") {
    REQUIRE_NOTHROW(TimingCtx());
    REQUIRE_NOTHROW(TimingCtx("FAIR.SELECTOR.ALL"));
    REQUIRE(TimingCtx() == TimingCtx("FAIR.SELECTOR.ALL"));
    REQUIRE(TimingCtx("ALL") == "ALL");
    REQUIRE("ALL" == TimingCtx("ALL"));
    REQUIRE(TimingCtx("all") == "ALL");
    REQUIRE(TimingCtx("ALL").bpcts.value() == 0);
    REQUIRE(TimingCtx("All").hash() != 0);

    auto changeMyFields = TimingCtx("ALL");
    REQUIRE(changeMyFields == "ALL");
    REQUIRE(changeMyFields.cid() == -1);
    REQUIRE(changeMyFields.sid() == -1);
    REQUIRE(changeMyFields.pid() == -1);
    REQUIRE(changeMyFields.gid() == -1);
    changeMyFields.selector = "FAIR.SELECTOR.C=1:S=2:P=3:T=4";
    REQUIRE(changeMyFields.cid() == 1);
    REQUIRE(changeMyFields.sid() == 2);
    REQUIRE(changeMyFields.pid() == 3);
    REQUIRE(changeMyFields.gid() == 4);
    changeMyFields.selector = "FAIR.SELECTOR.ALL";
    REQUIRE(changeMyFields.cid() == -1);
    REQUIRE(changeMyFields.sid() == -1);
    REQUIRE(changeMyFields.pid() == -1);
    REQUIRE(changeMyFields.gid() == -1);

    const auto timestamp = std::chrono::microseconds(1234);

    TimingCtx  ctx;
    REQUIRE_NOTHROW(ctx = TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=3", timestamp));
    REQUIRE(ctx == TimingCtx(0, 1, 2, 3, timestamp));
    REQUIRE(ctx != TimingCtx(0, 1, 2, 3));
    REQUIRE(ctx.toString() == "FAIR.SELECTOR.C=0:S=1:P=2:T=3");

    REQUIRE_THROWS_AS(TimingCtx("FAIR.SELECTOR.C0:S=1:P=2:T=3"), std::invalid_argument);
    REQUIRE_THROWS_AS(TimingCtx("FAIR.SELECTOR.C0=:S=1:P=2:T=3"), std::invalid_argument);
    REQUIRE_THROWS_AS(TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=ABC"), std::invalid_argument);
    REQUIRE_THROWS_AS(TimingCtx("FAIR.SELECTOR.X=1"), std::invalid_argument);
    REQUIRE_THROWS_AS(TimingCtx("NON_DEFAULT_SELECTOR.X=1"), std::invalid_argument);

    REQUIRE_NOTHROW(ctx = TimingCtx("FAIR.SELECTOR.C=2", timestamp));
    REQUIRE(ctx.cid() != -1);
    REQUIRE(ctx.cid() == 2);
    REQUIRE(ctx.sid() == -1);
    REQUIRE(ctx.pid() == -1);
    REQUIRE(ctx.gid() == -1);
    REQUIRE(ctx.bpcts.value() == timestamp.count());

    REQUIRE(TimingCtx("FAIR.SELECTOR.C=0:S=1").toString() == "FAIR.SELECTOR.C=0:S=1");
}

TEST_CASE("Basic TimingCtx ALL selector tests", "[TimingCtx][all_selector]") {
    constexpr auto timestamp = std::chrono::microseconds(1234);

    REQUIRE_NOTHROW(TimingCtx());
    REQUIRE_NOTHROW(TimingCtx("", timestamp));
    REQUIRE_NOTHROW(TimingCtx("ALL", timestamp));
    REQUIRE_NOTHROW(TimingCtx("FAIR.SELECTOR.ALL", timestamp));

    const auto fromEmptyString = TimingCtx("", timestamp);
    auto       fromOptionals   = TimingCtx({}, {}, {}, {}, timestamp);
    const auto fromAll         = TimingCtx("ALL", timestamp);
    const auto fromFSA         = TimingCtx("FAIR.SELECTOR.ALL", timestamp);

    REQUIRE(fromEmptyString.toString() == "FAIR.SELECTOR.ALL");
    REQUIRE(fromEmptyString.toString() == "FAIR.SELECTOR.ALL");
    REQUIRE(fromOptionals.toString() == "FAIR.SELECTOR.C=0:S=0:P=0:T=0");
    REQUIRE(fromAll.toString() == "FAIR.SELECTOR.ALL");
    REQUIRE(fromFSA.toString() == "FAIR.SELECTOR.ALL");

    REQUIRE(fromEmptyString == fromEmptyString);
    REQUIRE(fromEmptyString == fromAll);
    REQUIRE(fromEmptyString == fromFSA);

    REQUIRE(fromEmptyString.bpcts.value() == timestamp.count());
    REQUIRE(fromEmptyString.bpcts.value() == timestamp.count());
    REQUIRE(fromOptionals.bpcts.value() == timestamp.count());
    REQUIRE(fromAll.bpcts.value() == timestamp.count());
    REQUIRE(fromFSA.bpcts.value() == timestamp.count());
}

TEST_CASE("TimingCtx equality operator", "[TimingCtx][equality]") {
    constexpr auto timestamp = std::chrono::microseconds(1234);
    const auto     ctx       = TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=3", timestamp);
    REQUIRE(ctx == ctx);
    REQUIRE(ctx == TimingCtx(ctx));
    REQUIRE(ctx == TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=3", timestamp));
    REQUIRE(ctx != TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=3", timestamp + std::chrono::microseconds(1)));
    REQUIRE(ctx != TimingCtx("FAIR.SELECTOR.C=-1:S=1:P=2:T=3", timestamp));
    REQUIRE(ctx != TimingCtx("FAIR.SELECTOR.C=0:S=-1:P=2:T=3", timestamp));
    REQUIRE(ctx != TimingCtx("FAIR.SELECTOR.C=0:S=1:P=-1:T=3", timestamp));
    REQUIRE(ctx != TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=-1", timestamp));
}

TEST_CASE("TimingCtx matching tests", "[TimingCtx][matches]") {
    constexpr auto timestamp = std::chrono::microseconds(1234);
    const auto     ctx       = TimingCtx("FAIR.SELECTOR.C=0:S=1:P=2:T=3", timestamp);
    REQUIRE(ctx.matches(ctx));
    REQUIRE(ctx.matches(TimingCtx(ctx.toString())));
    REQUIRE(ctx.matches(TimingCtx("FAIR.SELECTOR.ALL")));
    REQUIRE(ctx.matchesWithBpcts(TimingCtx(0)));
    REQUIRE(ctx.matchesWithBpcts(TimingCtx(0, 1)));
    REQUIRE(ctx.matchesWithBpcts(TimingCtx(0, 1, 2)));
    REQUIRE(ctx.matches(TimingCtx(0, 1, 2)));
    REQUIRE(ctx.matches(TimingCtx({}, 1, 2)));
    REQUIRE_FALSE(ctx.matches(TimingCtx(0, 0, 2)));
    REQUIRE_FALSE(ctx.matches(TimingCtx(0, 1, 0)));

    const auto ctx2 = TimingCtx("FAIR.SELECTOR.C=0:S=1", timestamp);
    REQUIRE(ctx2.cid() == 0);
    REQUIRE(ctx2.sid() == 1);
    REQUIRE(ctx2.pid() == -1);
    REQUIRE(ctx2.gid() == -1);
    REQUIRE(ctx.matches(TimingCtx(0, 1)));
    REQUIRE(ctx.matches(TimingCtx(0, 1)));

    REQUIRE(TimingCtx().matches(TimingCtx()));
    REQUIRE_FALSE(TimingCtx().matches(TimingCtx(0, 1, 2, {}, timestamp)));
    REQUIRE(TimingCtx(0, 1, 2, {}, timestamp).matches(TimingCtx()));
}

TEST_CASE("TimingCtx benchmark", "[TimingCtx][benchmark]") {
    opencmw::debug::resetStats();
    opencmw::debug::Timer timer("TimingCtx benchmark", 40);

    using namespace std::literals::string_literals;
    static const std::array selectors = {
        ""s,
        "ALL"s,
        "FAIR.SELECTOR.ALL"s,
        "FAIR.SELECTOR.C=0:S=1:P=2:T=3"s,
        "FAIR.SELECTOR.C=0:S=1:T=3"s,
        "FAIR.SELECTOR.C=0:T=3"s,
        "FAIR.SELECTOR.S=1:P=2:T=3"s,
        "FAIR.SELECTOR.C=0:S=ALL:P=2:T=3"s,
    };

    uint           matchCount      = 0;
    constexpr auto outerIterations = 10000;
    constexpr auto totalIterations = outerIterations * selectors.size() * selectors.size();

    for (int i = 0; i < outerIterations; ++i) {
        for (std::size_t dist = 0; dist < selectors.size(); ++dist) {
            for (std::size_t first = 0; first < selectors.size(); ++first) {
                const auto second = (first + dist) % selectors.size();
                TimingCtx  ctx1(selectors[first]);
                TimingCtx  ctx2(selectors[second]);
                if (ctx1.matches(ctx2)) {
                    matchCount++;
                }
            }
        }
    }

    std::cout << fmt::format("Total iterations: {}; Parsed: {}, matches() calls: {}; matched: {}\n", totalIterations, totalIterations * 2, totalIterations, matchCount);
}

TEST_CASE("TimingCtx operators", "[TimingCtx][operators]") {
    std::stringstream s;
    s << TimingCtx("FAIR.SELECTOR.ALL");
    REQUIRE(s.str() == "FAIR.SELECTOR.ALL");

    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1") == TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1"));
    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1") != TimingCtx("FAIR.SELECTOR.C=2:S=1:P=1:T=1"));
    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1") != TimingCtx("FAIR.SELECTOR.C=1:S=2:P=1:T=1"));
    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1") != TimingCtx("FAIR.SELECTOR.C=1:S=1:P=2:T=1"));
    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1") != TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=2"));
    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1", std::chrono::microseconds(1234)) != TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1"));

    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1") < TimingCtx("FAIR.SELECTOR.C=2:S=1:P=1:T=1"));
    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1") < TimingCtx("FAIR.SELECTOR.C=1:S=2:P=1:T=1"));
    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1") < TimingCtx("FAIR.SELECTOR.C=1:S=1:P=2:T=1"));
    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1") < TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=2"));
    REQUIRE(TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1", std::chrono::microseconds(1)) < TimingCtx("FAIR.SELECTOR.C=1:S=1:P=1:T=1", std::chrono::microseconds(2)));
}
