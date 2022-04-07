#include <catch2/catch.hpp>

#include <ReaderWriterLock.hpp>

TEST_CASE("ReaderWriterLock basic tests", "[ReaderWriterLock]") {
    using opencmw::ReaderWriterLockType::READ;
    using opencmw::ReaderWriterLockType::WRITE;
    opencmw::ReaderWriterLock rwlock;

    SECTION("basic read/write lock tests") {
        REQUIRE(rwlock.lock<READ>() == 1);
        REQUIRE(rwlock.lock<READ>() == 2);
        REQUIRE(rwlock.unlock<READ>() == 1);
        REQUIRE(rwlock.unlock<READ>() == 0);

        REQUIRE(rwlock.lock<WRITE>() == -1);
        REQUIRE(rwlock.lock<WRITE>() == -2);
        REQUIRE(rwlock.unlock<WRITE>() == -1);
        REQUIRE(rwlock.unlock<WRITE>() == -0);
    }

    SECTION("try write lock when holding read lock") {
        REQUIRE(rwlock.lock<READ>() == 1);
        REQUIRE(!rwlock.tryLock<WRITE>());
        REQUIRE(rwlock.unlock<READ>() == 0);
        REQUIRE(rwlock.tryLock<WRITE>());
        REQUIRE(rwlock.unlock<WRITE>() == 0);
    }

    SECTION("try read lock when holding write lock") {
        REQUIRE(rwlock.lock<WRITE>() == -1);
        REQUIRE(!rwlock.tryLock<READ>());
        REQUIRE(rwlock.unlock<WRITE>() == -0);
        REQUIRE(rwlock.tryLock<READ>());
        REQUIRE(rwlock.unlock<READ>() == -0);
    }

    REQUIRE(rwlock.value() == 0);
    SECTION("try RAII scoped read lock guard") {
        auto guard = rwlock.scopedGuard<READ>();
        REQUIRE(rwlock.value() == 1);
        REQUIRE(!rwlock.tryLock<WRITE>());
    }
    REQUIRE(rwlock.value() == 0);
    SECTION("try RAII scoped write lock guard") {
        auto guard = rwlock.scopedGuard<WRITE>();
        REQUIRE(rwlock.value() == -1);
        REQUIRE(!rwlock.tryLock<READ>());
    }
    REQUIRE(rwlock.value() == 0);
}