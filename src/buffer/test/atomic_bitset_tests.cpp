#include <catch2/catch.hpp>

#include <array>
#include <numeric>
#include <tuple>
#include <thread>

#include <BufferSkeleton.hpp>
#include <CircularBuffer.hpp>

namespace opencmw_bitset_test {

template<typename TBitset>
void runAtomicBitsetTest(TBitset& bitset, std::size_t bitsetSize) {

    REQUIRE(bitset.size() == bitsetSize);

    // test default values
    for (std::size_t i = 0; i < bitset.size(); i++) {
        REQUIRE(!bitset.test(i));
    }

    // set true for test positions
    std::vector<std::size_t> testPositions = {0UZ, 1UZ, 5UZ, 31UZ, 32UZ, 47UZ, 63UZ, 64UZ, 100UZ, 127UZ};
    for (const std::size_t pos : testPositions) {
        bitset.set(pos);
    }

    // only test positions should be set
    for (std::size_t i = 0; i < bitset.size(); i++) {
        if (std::ranges::find(testPositions, i) != testPositions.end()) {
            REQUIRE(bitset.test(i));
        } else {
            REQUIRE(!bitset.test(i));
        }
    }

    // reset test positions
    for (const std::size_t pos : testPositions) {
        bitset.reset(pos);
    }

    // all positions should be reset
    for (std::size_t i = 0; i < bitset.size(); i++) {
        REQUIRE(!bitset.test(i));
    }

    // Bulk operations
    std::vector<std::pair<std::size_t, std::size_t>> testPositionsBulk = {{10UZ, 20UZ}, {10UZ, 10UZ}, {50UZ, 70UZ}, {0UZ, 127UZ}, {0UZ, 128UZ}, {63UZ, 64UZ}, {127UZ, 128UZ}, {0UZ, 1UZ}, {128UZ, 128UZ}};
    for (const auto& pos : testPositionsBulk) {
        bitset.set(pos.first, pos.second);

        for (std::size_t i = 0; i < bitset.size(); ++i) {
            if (i >= pos.first && i < pos.second) {
                REQUIRE(bitset.test(i));
            } else {
                REQUIRE(!bitset.test(i));
            }
        }

        // all positions should be reset
        bitset.reset(pos.first, pos.second);
        for (std::size_t i = 0; i < bitset.size(); i++) {
            REQUIRE(!bitset.test(i));
        }
    }
/* Catch2 does not support checking for abort
#if not defined(__EMSCRIPTEN__) && not defined(NDEBUG)
    expect(aborts([&] { bitset.set(bitsetSize); })) << "Setting bit should throw an assertion.";
    expect(aborts([&] { bitset.reset(bitsetSize); })) << "Resetting bit should throw an assertion.";
    expect(aborts([&] { bitset.test(bitsetSize); })) << "Testing bit should throw an assertion.";
    // bulk operations
    expect(aborts([&] { bitset.set(100UZ, 200UZ); })) << "Setting bulk bits should throw an assertion.";
    expect(aborts([&] { bitset.reset(100UZ, 200UZ); })) << "Resetting bulk bits should throw an assertion.";
    expect(aborts([&] { bitset.set(200UZ, 100UZ); })) << "Setting bulk begin > end should throw an assertion.";
    expect(aborts([&] { bitset.reset(200UZ, 100UZ); })) << "Resetting bulk begin > end should throw an assertion.";
#endif
*/
}

TEST_CASE("AtomicBitsetTests", "[AtomicBitset]") {
    using namespace opencmw;
    using namespace opencmw::buffer;

    SECTION("basics set/reset/test") {
        auto dynamicBitset = AtomicBitset<>(128UZ);
        runAtomicBitsetTest(dynamicBitset, 128UZ);

        auto staticBitset = AtomicBitset<128UZ>();
        runAtomicBitsetTest(staticBitset, 128UZ);
    }

    SECTION("multithreads") {
        constexpr std::size_t    bitsetSize = 256UZ;
        constexpr std::size_t    nThreads   = 16UZ;
        constexpr std::size_t    nRepeats   = 100UZ;
        AtomicBitset<bitsetSize> bitset;
        std::vector<std::thread> threads;

        for (std::size_t iThread = 0; iThread < nThreads; iThread++) {
            threads.emplace_back([&] {
                for (std::size_t iR = 0; iR < nRepeats; iR++) {
                    for (std::size_t i = 0; i < bitsetSize; i++) {
                        if (i < bitsetSize / 2) {
                            bitset.set(i);
                            bitset.reset(i);
                            std::ignore = bitset.test(i);
                        } else {
                            bitset.reset(i);
                            bitset.set(i);
                            std::ignore = bitset.test(i);
                        }
                    }
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        // Verify final state: first half should be reset, second half should be set
        for (std::size_t i = 0; i < bitsetSize; i++) {
            if (i < bitsetSize / 2) {
                REQUIRE(!bitset.test(i));
            } else {
                REQUIRE(bitset.test(i));
            }
        }
    }

    SECTION("multithreads bulk") {
        constexpr std::size_t    bitsetSize = 2000UZ;
        constexpr std::size_t    nThreads   = 10UZ;
        constexpr std::size_t    chunkSize  = bitsetSize / nThreads;
        constexpr std::size_t    nRepeats   = 1000UZ;
        AtomicBitset<bitsetSize> bitset;
        std::vector<std::thread> threads;

        for (std::size_t iThread = 0; iThread < nThreads; iThread++) {
            threads.emplace_back([&bitset, iThread] {
                for (std::size_t iR = 0; iR < nRepeats; iR++) {
                    if (iThread % 2 == 0) {
                        bitset.set(iThread * chunkSize, (iThread + 1) * chunkSize);
                        bitset.reset(iThread * chunkSize, (iThread + 1) * chunkSize);
                    } else {
                        bitset.reset(iThread * chunkSize, (iThread + 1) * chunkSize);
                        bitset.set(iThread * chunkSize, (iThread + 1) * chunkSize);
                    }
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        // Verify final state
        for (std::size_t i = 0; i < bitsetSize; i++) {
            if ((i / chunkSize) % 2 == 0) {
                REQUIRE(!bitset.test(i));
            } else {
                REQUIRE(bitset.test(i));
            }
        }
    }
};

}
