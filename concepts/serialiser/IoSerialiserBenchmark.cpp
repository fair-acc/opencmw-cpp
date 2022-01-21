#include "IoSerialiserBenchmark.hpp"
#include <IoSerialiserCmwLight.hpp>
#include <IoSerialiserJson.hpp>
#include <IoSerialiserYAML.hpp>
#include <IoSerialiserYaS.hpp>
#include <tuple>

using namespace opencmw;
using namespace opencmw::utils; // for operator<< and fmt::format overloading

/**
 * for comparison (C++/POCO variant ~ 90% feature-complete: YaS header missing, ...):
 * Java:
 * IO Serializer (custom) throughput = 4.3 GB/s for 29.7 kB per test run (took 698.0 ms)
 * IO Serializer (POJO) throughput = 2.5 GB/s for 29.1 kB per test run (took 1172.0 ms)
 * C++:
 * IO Serializer (POCO) throughput = 6.6 GB/s for 29.5 kB per test run (took 447.2 ms)
 *
 * benchmark exemplary output:
 * [..]
 * IO Serializer (POCO, YaS, 0) throughput = 7.7 GB/s for 29.6 kB per test run (took 385.8 ms)
 * IO Serializer (POCO, YaS, 0) throughput = 7.7 GB/s for 29.6 kB per test run (took 384.2 ms)
* [..]
 * IO Serializer (POCO, CmwLight, 0) throughput = 9.3 GB/s for 29.0 kB per test run (took 312.3 ms)
 * ┌─protocol─┬────────ALWAYS─────────┬────────LENIENT────────┬────────IGNORE─────────┐
 * │   YAML   │ 84.3 MB/s ±   8.1 MB/s│ 83.6 MB/s ± 427.6 kB/s│ 86.3 MB/s ± 539.8 kB/s│
 * │   Json   │283.2 MB/s ±   1.6 MB/s│283.0 MB/s ±   1.8 MB/s│287.0 MB/s ± 854.8 kB/s│
 * │   YaS    │  6.0 GB/s ±  33.0 MB/s│  6.3 GB/s ±  22.6 MB/s│  7.7 GB/s ±  47.9 MB/s│
 * │ CmwLight │  8.0 GB/s ±  39.8 MB/s│  8.2 GB/s ±  24.9 MB/s│  9.3 GB/s ± 104.0 MB/s│
 * └──────────┴───────────────────────┴───────────────────────┴───────────────────────┘
 *
 */

template<SerialiserProtocol protocol>
[[nodiscard]] std::vector<std::tuple<std::string, long, long>> runTests(const std::size_t nIterations) {
    std::vector<std::tuple<std::string, long, long>> results;
    using namespace opencmw;
    IoBuffer      buffer;
    TestDataClass data(10, 10, 0);
    TestDataClass data2;
    data2.byte1 = 30;
    fmt::print("IoSerialiserBenchmark - {} - check identity - nBytes = {}\n", protocol::protocolName(), checkSerialiserIdentity<protocol>(buffer, data, data2));

    constexpr auto mean = [](ArrayOrVector auto const &v) { return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size()); };
    constexpr auto rms  = [](ArrayOrVector auto const &v, double m) { return std::sqrt(std::inner_product(v.begin(), v.end(), v.begin(), 0.0) / static_cast<double>(v.size()) - m * m); };

    TestDataClass  testData(1000, 0); // numeric heavy data <-> equivalent to Java benchmark
    fmt::print("{} performance with strong checks (exceptions if necessary):\n", protocol::protocolName());
    std::vector<double> values;
    for (int i = 0; i < 10; i++) {
        values.emplace_back(testPerformancePoco<protocol, ProtocolCheck::ALWAYS>(buffer, testData, data2, nIterations));
    }
    results.emplace_back(std::tuple(protocol::protocolName(), mean(values), rms(values, mean(values))));

    fmt::print("{} performance with lenient checks (collect exceptions);\n", protocol::protocolName());
    values.clear();
    for (int i = 0; i < 10; i++) {
        values.emplace_back(testPerformancePoco<protocol, ProtocolCheck::LENIENT>(buffer, testData, data2, nIterations));
    }
    results.emplace_back(std::tuple(protocol::protocolName(), mean(values), rms(values, mean(values))));

    fmt::print("{} performance without checks:\n", protocol::protocolName());
    values.clear();
    for (int i = 0; i < 10; i++) {
        values.emplace_back(testPerformancePoco<protocol, ProtocolCheck::IGNORE>(buffer, testData, data2, nIterations));
    }
    results.emplace_back(std::tuple(protocol::protocolName(), mean(values), rms(values, mean(values))));

    return results;
}

int main() {
    std::vector<std::vector<std::tuple<std::string, long, long>>> results;

    results.emplace_back(runTests<YAML>(1'000));
    results.emplace_back(runTests<Json>(1'000));
    results.emplace_back(runTests<YaS>(100'000));
    results.emplace_back(runTests<CmwLight>(100'000));

    constexpr int columWidth = 10;
    fmt::print("┌{2:─^{0}}┬{3:─^{1}}┬{4:─^{1}}┬{5:─^{1}}┐\n", columWidth, 2 * columWidth + 3, "protocol", "ALWAYS", "LENIENT", "IGNORE");
    for (const auto &result : results) {
        fmt::print("│{1: ^{0}}│{2:>{0}} ± {3:>{0}}│{4:>{0}} ± {5:>{0}}│{6:>{0}} ± {7:>{0}}│\n", columWidth, std::get<0>(result[0]),
                humanReadableByteCount(std::get<1>(result[0])) + "/s", humanReadableByteCount(std::get<2>(result[0])) + "/s",
                humanReadableByteCount(std::get<1>(result[1])) + "/s", humanReadableByteCount(std::get<2>(result[1])) + "/s",
                humanReadableByteCount(std::get<1>(result[2])) + "/s", humanReadableByteCount(std::get<2>(result[2])) + "/s");
    }
    fmt::print("└{2:─^{0}}┴{3:─^{1}}┴{4:─^{1}}┴{5:─^{1}}┘\n", columWidth, 2 * columWidth + 3, "", "", "", "");
}
