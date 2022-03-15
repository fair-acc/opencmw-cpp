#include <catch2/catch.hpp>

#include <fmt/format.h>
#include <iostream>

#include "disruptor/Sequence.hpp"

TEST_CASE("Sequence basic tests", "[Disruptor]") {
    using namespace opencmw::disruptor;
    REQUIRE(alignof(Sequence) == kCacheLine);
    REQUIRE(-1L == kInitialCursorValue);
    REQUIRE_NOTHROW(Sequence());
    REQUIRE_NOTHROW(Sequence(2));

    auto s1 = Sequence();
    REQUIRE(s1.value() == kInitialCursorValue);

    const auto s2 = Sequence(2);
    REQUIRE(s2.value() == 2);

    REQUIRE_NOTHROW(s1.setValue(3));
    REQUIRE(s1.value() == 3);

    REQUIRE_NOTHROW(s1.compareAndSet(3, 4));
    REQUIRE(s1.value() == 4);
    REQUIRE_NOTHROW(s1.compareAndSet(3, 5));
    REQUIRE(s1.value() == 4);

    REQUIRE(s1.incrementAndGet() == 5);
    REQUIRE(s1.value() == 5);
    REQUIRE(s1.addAndGet(2) == 7);
    REQUIRE(s1.value() == 7);

    std::shared_ptr<std::vector<std::shared_ptr<Sequence>>> sequences{ std::make_shared<std::vector<std::shared_ptr<Sequence>>>() };
    REQUIRE(detail::getMinimumSequence(*sequences) == std::numeric_limits<std::int64_t>::max());
    REQUIRE(detail::getMinimumSequence(*sequences, 2) == 2);
    sequences->emplace_back(std::make_shared<Sequence>(4));
    REQUIRE(detail::getMinimumSequence(*sequences) == 4);
    REQUIRE(detail::getMinimumSequence(*sequences, 5) == 4);
    REQUIRE(detail::getMinimumSequence(*sequences, 2) == 2);

    auto cursor = std::make_shared<Sequence>(10);
    auto s3     = std::make_shared<Sequence>(1);
    REQUIRE(sequences->size() == 1);
    REQUIRE(detail::getMinimumSequence(*sequences) == 4);
    REQUIRE_NOTHROW(detail::addSequences(sequences, *cursor, { s3 }));
    REQUIRE(sequences->size() == 2);
    REQUIRE(s3->value() == 10); // newly added sequences are set automatically to the cursor/write position
    REQUIRE(detail::getMinimumSequence(*sequences) == 4);

    REQUIRE_NOTHROW(detail::removeSequence(sequences, cursor));
    REQUIRE(sequences->size() == 2);
    REQUIRE_NOTHROW(detail::removeSequence(sequences, s3));
    REQUIRE(sequences->size() == 1);

    using namespace opencmw;
    std::stringstream ss;
    REQUIRE(ss.str().size() == 0);
    REQUIRE_NOTHROW(ss << fmt::format("{}", *s3));
    REQUIRE(ss.str().size() != 0);
}
