#include "helpers.hpp"

#include <majordomo/BasicMdpWorker.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Client.hpp>
#include <majordomo/Message.hpp>

#include <fmt/format.h>

using opencmw::majordomo::BasicMdpWorker;
using opencmw::majordomo::Broker;
using opencmw::majordomo::Client;
using opencmw::majordomo::Command;
using opencmw::majordomo::Context;
using opencmw::majordomo::MdpMessage;
using opencmw::majordomo::RequestContext;
using opencmw::majordomo::Settings;

#define REQUIRE(expression) \
    { \
        if (!expression) { \
            std::cerr << fmt::format("'{}' failed\n", #expression); \
            std::terminate(); \
        } \
    }

static Settings benchmarkSettings() {
    return Settings{}; // use defaults
}

class PayloadHandler {
    std::string _payload;

public:
    explicit PayloadHandler(std::string payload)
        : _payload(std::move(payload)) {
    }

    void handleRequest(RequestContext &context) {
        if (context.request.command() == Command::Get) {
            context.reply->setBody(_payload, opencmw::majordomo::MessageFrame::dynamic_bytes_tag{});
        } else {
            context.reply.reset();
        }
    }
};

class TestClient : public Client {
public:
    explicit TestClient(Context &context)
        : Client(context) {
    }

    void handleResponse(MdpMessage &&) override {
        debug() << "Unexpected message not handled by callback\n";
        std::terminate();
    }

    template<typename BodyType>
    void getAndBusyWait(std::string serviceName, BodyType body) {
        bool receivedReply = false;
        get(std::move(serviceName), std::forward<BodyType>(body), [&receivedReply](auto &&) {
            receivedReply = true;
        });

        while (!receivedReply) {
            tryRead();
        }
    }
};

enum class Get {
    Sync,
    Async
};

struct Result {
    std::string                   routerAddress;
    int                           iterations;
    Get                           mode;
    std::size_t                   payloadSize;
    std::chrono::duration<double> duration;
};

Result simpleOneWorkerBenchmark(std::string routerAddress, Get mode, int iterations, std::size_t payloadSize) {
    const auto workerRouter = std::string_view("inproc://for_worker");

    Context    context;
    Broker     broker(benchmarkSettings(), "benchmarkbroker", "", context);
    REQUIRE(broker.bind(routerAddress, Broker::BindOption::Router));
    REQUIRE(broker.bind(workerRouter, Broker::BindOption::Router));
    RunInThread    brokerRun(broker);

    BasicMdpWorker worker(benchmarkSettings(), context, workerRouter, "blob", PayloadHandler(std::string(payloadSize, '\xab')));
    RunInThread    workerRun(worker);

    Context        clientContext;
    TestClient     client(routerAddress.starts_with("inproc") ? context : clientContext);
    REQUIRE(client.connect(routerAddress));

    const auto before = std::chrono::system_clock::now();

    if (mode == Get::Async) {
        int counter = 0;
        for (int i = 0; i < iterations; ++i) {
            client.get("blob", "", [&counter](auto &&) {
                ++counter;
            });
        }

        while (counter < iterations) {
            client.tryRead();
        }
    } else {
        for (int i = 0; i < iterations; ++i) {
            client.getAndBusyWait("blob", "");
        }
    }

    const auto                          after = std::chrono::system_clock::now();
    const std::chrono::duration<double> diff  = after - before;

    Result                              r;
    r.routerAddress = routerAddress;
    r.mode          = mode;
    r.payloadSize   = payloadSize;
    r.iterations    = iterations;
    r.duration      = diff;
    return r;
}

void simpleTwoWorkerBenchmark(std::string routerAddress, Get mode, int iterations, std::size_t payload1_size, std::size_t payload2_size) {
    const auto workerRouter = std::string_view("inproc://for_worker");

    Context    context;
    Broker     broker(benchmarkSettings(), "benchmarkbroker", "", context);
    REQUIRE(broker.bind(routerAddress, Broker::BindOption::Router));
    REQUIRE(broker.bind(workerRouter, Broker::BindOption::Router));
    RunInThread    brokerRun(broker);

    BasicMdpWorker worker1(benchmarkSettings(), context, workerRouter, "blob", PayloadHandler(std::string(payload1_size, '\xab')));
    RunInThread    worker1_run(worker1);

    BasicMdpWorker worker2(benchmarkSettings(), context, workerRouter, "blob", PayloadHandler(std::string(payload2_size, '\xab')));
    RunInThread    worker2_run(worker2);

    Context        clientContext;
    TestClient     client(routerAddress.starts_with("inproc") ? context : clientContext);
    REQUIRE(client.connect(routerAddress));

    const auto before = std::chrono::system_clock::now();

    if (mode == Get::Async) {
        int counter = 0;
        for (int i = 0; i < iterations; ++i) {
            client.get(i % 2 == 0 ? "blob1" : "blob2", "", [&counter](auto &&) {
                ++counter;
            });
        }

        while (counter < iterations) {
            client.tryRead();
        }
    } else {
        for (int i = 0; i < iterations; ++i) {
            client.getAndBusyWait(i % 2 == 0 ? "blob1" : "blob2", "");
        }
    }
    const auto                          after = std::chrono::system_clock::now();
    const std::chrono::duration<double> diff  = after - before;

    std::cout << fmt::format("{}: {}. Alternating Payloads {}/{} bytes: {} iterations took {}s ({} messages/s)\n",
            routerAddress,
            mode == Get::Async ? "ASYNC" : "SYNC",
            payload1_size,
            payload2_size,
            iterations,
            diff.count(),
            std::round(iterations / diff.count()));
}

int main(int argc, char **argv) {
    const auto          N      = argc > 1 ? std::atoi(argv[1]) : 100000;
    const auto          tcp    = std::string("tcp://127.0.0.1:12346");
    const auto          inproc = std::string("inproc://benchmark");

    std::vector<Result> results;

    results.push_back(simpleOneWorkerBenchmark(tcp, Get::Sync, N, 10));
    results.push_back(simpleOneWorkerBenchmark(inproc, Get::Sync, N, 10));
    results.push_back(simpleOneWorkerBenchmark(tcp, Get::Async, N, 10));
    results.push_back(simpleOneWorkerBenchmark(inproc, Get::Async, N, 10));
    // simpleOneWorkerBenchmark(tcp, Get::Async, 3000, 1024);
    // simpleOneWorkerBenchmark(tcp, Get::Async, 3000, 1024 * 1024);
    // simpleTwoWorkerBenchmark(inproc, Get::Async, 10000, 10, 10);

    for (const auto &result : results) {
        std::cout << fmt::format("{}: {}. Payload {} bytes: {} iterations took {}s ({} messages/s)\n",
                result.routerAddress,
                result.mode == Get::Async ? "ASYNC" : "SYNC",
                result.payloadSize,
                result.iterations,
                result.duration.count(),
                std::round(result.iterations / result.duration.count()));
    }
}
