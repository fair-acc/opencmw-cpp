#include "IoClassSerialiserBenchmark.hpp"
#include <IoClassSerialiser.hpp>
#include <Utils.hpp>
#include <iostream>

using namespace opencmw;
using namespace opencmw::utils; // for operator<< and fmt::format overloading

template<ReflectableClass T>
std::size_t checkSerialiserIdentity(IoBuffer &buffer, const T &inputObject, T &outputObject) {
    assert(inputObject == inputObject); // tests that equal operator works correctly
    using namespace opencmw;
    buffer.clear();
    //    if constexpr (requires { outputObject.clear();}) {
    //        outputObject.clear();
    //    }
    opencmw::serialise<YaS>(buffer, inputObject);

    std::cout << ClassInfoVerbose << "before: ";
    diffView(std::cout, inputObject, outputObject);
    assert(inputObject != outputObject);
    buffer.reset();

    try {
        opencmw::deserialise<YaS>(buffer, outputObject);
    } catch (std::exception &e) {
        std::cout << "caught exception " << typeName<std::remove_reference_t<decltype(e)>>() << std::endl;
    } catch (...) {
        std::cout << "caught unknown exception " << std::endl;
    }
    std::cout << "after: " << std::flush;
    diffView(std::cout, inputObject, outputObject);
    assert(inputObject == outputObject);

    return buffer.size();
}

std::string humanReadableByteCount(long bytes, const bool si) {
    const int unit = si ? 1000 : 1024;
    if (bytes < unit) {
        return fmt::format("{} B", bytes);
    }

    const int  exp = static_cast<int>((log(static_cast<double>(bytes)) / log(unit)));
    const char pre = (si ? "kMGTPE" : "KMGTPE")[exp - 1];
    return fmt::format("{:.1f} {}{}B", static_cast<double>(bytes) / pow(unit, exp), pre, (si ? "" : "i"));
}

template<ReflectableClass T>
void testPerformancePojo(IoBuffer &buffer, const T &inputObject, T &outputObject, const std::size_t iterations) {
    bool          putFieldMetaData = false;

    const clock_t startTime        = clock();
    for (std::size_t i = 0; i < iterations; i++) {
        if (i == 1) {
            // only stream meta-data the first iteration
            putFieldMetaData = false;
        }
        buffer.clear();
        opencmw::serialise<YaS>(buffer, inputObject, putFieldMetaData);

        buffer.reset();
        try {
            opencmw::deserialise<YaS>(buffer, outputObject);
        } catch (std::exception &e) {
            std::cout << "caught exception " << typeName<std::remove_reference_t<decltype(e)>>() << std::endl;
        } catch (...) {
            std::cout << "caught unknown exception " << std::endl;
        }

        if (inputObject.string1 != outputObject.string1) {
            // quick check necessary so that the above is not optimised by the Java JIT compiler to NOP
            throw std::exception();
        }
    }
    if (iterations <= 1) {
        // JMH use-case
        return;
    }
    const clock_t stopTime       = clock();
    const double  diffSeconds    = static_cast<double>(stopTime - startTime) / CLOCKS_PER_SEC;
    const double  bytesPerSecond = ((static_cast<double>(iterations * buffer.size()) / diffSeconds));
    std::cout << fmt::format("IO Serializer (POCO) throughput = {}/s for {} per test run (took {:0.1f} ms)\n",
            humanReadableByteCount(static_cast<long>(bytesPerSecond), true),
            humanReadableByteCount(static_cast<long>(buffer.size()), true), 1e3 * diffSeconds);
}

/*
 * for comparison (C++/POCO variant ~ 90% feature-complete: YaS header missing, ...):
 * Java:
 * IO Serializer (custom) throughput = 4.3 GB/s for 29.7 kB per test run (took 698.0 ms)
 * IO Serializer (POJO) throughput = 2.5 GB/s for 29.1 kB per test run (took 1172.0 ms)
 * C++:
 * IO Serializer (POCO) throughput = 6.6 GB/s for 29.5 kB per test run (took 447.2 ms)
 *
 */

int main() {
    using namespace opencmw;
    IoBuffer      buffer;
    TestDataClass data(10, 10, 0);
    TestDataClass data2;
    data2.byte1 = 30;

    std::cout << fmt::format("IoClassSerialiserBenchmark - check identity - nBytes = {}\n", checkSerialiserIdentity(buffer, data, data2));

    TestDataClass testData(1000, 0);    // numeric heavy data <-> equivalent to Java benchmark
    const int     nIterations = 100000; // 100000
    for (int i = 0; i < 10; i++) {
        testPerformancePojo(buffer, testData, data2, nIterations);
    }
}
