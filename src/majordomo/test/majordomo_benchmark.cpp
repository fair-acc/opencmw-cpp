#define CATCH_CONFIG_ENABLE_BENCHMARKING

#include "helpers.hpp"

#include <majordomo/BasicMdpWorker.hpp>
#include <majordomo/broker.hpp>
#include <majordomo/client.hpp>
#include <majordomo/Message.hpp>

#include <catch2/catch.hpp>
#include <fmt/format.h>

using Majordomo::OpenCMW::BasicMdpWorker;
using Majordomo::OpenCMW::Broker;
using Majordomo::OpenCMW::Client;
using Majordomo::OpenCMW::MdpMessage;

static const auto router_address = std::string_view("tcp://127.0.0.1:12346");

class Worker : public BasicMdpWorker {
    std::string_view _payload;
public:
    // payload must survive the worker!
    explicit Worker(std::string_view payload, yaz::Context &context, std::string service_name)
        : BasicMdpWorker(context, std::move(service_name))
        , _payload(payload) {
    }

    std::optional<MdpMessage> handle_get(MdpMessage &&request) override {
        request.setWorkerCommand(MdpMessage::WorkerCommand::Final);
        request.setBody(_payload, yaz::MessagePart::static_bytes_tag{});
        return std::move(request);
    }

    std::optional<MdpMessage> handle_set(MdpMessage &&) override {
        return {};
    }
};

void simple_one_worker_benchmark(int iterations, std::size_t payload_size) {
    yaz::Context context;
    Broker broker("benchmarkbroker", "", context);
    REQUIRE(broker.bind(router_address));
    RunInThread broker_run(broker);

    std::string payload(payload_size, '\xab');
    Worker worker(payload, context, "blob");
    REQUIRE(worker.connect(router_address));
    RunInThread worker_run(worker);

    Client client(context);
    REQUIRE(client.connect(router_address));

    const auto before = std::chrono::system_clock::now();

    int counter = 0;
    for (int i = 0; i < iterations; ++i) {
        client.get("blob", "", [&counter](auto &&) {
            ++counter;
        });
    }

    while (counter < iterations) {
        client.try_read();
    }

    const auto after = std::chrono::system_clock::now();
    const std::chrono::duration<double> diff = after - before;

    std::cout << fmt::format("Payload {} bytes: {} iterations took {}s ({} messages/s)\n", payload_size, iterations, diff.count(), iterations / diff.count());
}

void simple_two_worker_benchmark(int iterations, std::size_t payload1_size, std::size_t payload2_size) {
    yaz::Context context;
    Broker broker("benchmarkbroker", "", context);
    REQUIRE(broker.bind(router_address));
    RunInThread broker_run(broker);

    std::string payload1(payload1_size, '\xab');
    Worker worker1(payload1, context, "blob1");
    REQUIRE(worker1.connect(router_address));
    RunInThread worker1_run(worker1);


    std::string payload2(payload2_size, '\xab');
    Worker worker2(payload2, context, "blob2");
    REQUIRE(worker2.connect(router_address));
    RunInThread worker2_run(worker2);

    Client client(context);
    REQUIRE(client.connect(router_address));

    const auto before = std::chrono::system_clock::now();

    int counter = 0;
    for (int i = 0; i < iterations; ++i) {
        client.get(i % 2 == 0 ? "blob1" : "blob2", "", [&counter](auto &&) {
            ++counter;
        });
    }

    while (counter < iterations) {
        client.try_read();
    }

    const auto after = std::chrono::system_clock::now();
    const std::chrono::duration<double> diff = after - before;

    std::cout << fmt::format("Alternating Payloads {}/{} bytes: {} iterations took {}s ({} messages/s)\n", payload1_size, payload2_size, iterations, diff.count(), iterations / diff.count());
}

TEST_CASE("GET Benchmarks", "[Broker]") {
    simple_one_worker_benchmark(3000, 1024);
    simple_one_worker_benchmark(3000, 1024 * 1024);
    simple_two_worker_benchmark(3000, 1024 * 1024, 1024 * 1024);
    simple_two_worker_benchmark(3000, 1024, 1024);
    simple_two_worker_benchmark(3000, 1024 * 1024, 1024);
}
