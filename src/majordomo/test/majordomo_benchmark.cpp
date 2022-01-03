#include "runinthread.hpp"

#include <majordomo/BasicMdpWorker.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Client.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/Message.hpp>

#include <fmt/format.h>

using URI = opencmw::URI<>;
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

static const auto pollIntervall = std::chrono::milliseconds(20);

class PayloadHandler {
    std::string _payload;

public:
    explicit PayloadHandler(std::string payload)
        : _payload(std::move(payload)) {
    }

    void operator()(RequestContext &context) {
        if (context.request.command() == Command::Get) {
            context.reply.setBody(_payload, opencmw::majordomo::MessageFrame::dynamic_bytes_tag{});
        } else {
            throw std::runtime_error("SET not supported");
        }
    }
};

class TestClient : public Client {
public:
    explicit TestClient(const Context &context)
        : Client(context) {
    }

    void handleResponse(MdpMessage &&) override {
        opencmw::debug() << "Unexpected message not handled by callback\n";
        std::terminate();
    }

    template<typename BodyType>
    void getAndBusyWait(std::string serviceName, BodyType body) {
        bool receivedReply = false;
        get(std::move(serviceName), std::forward<BodyType>(body), [&receivedReply](auto &&) {
            receivedReply = true;
        });

        while (!receivedReply) {
            tryRead(pollIntervall);
        }
    }
};

enum class Get {
    Sync,
    Async
};

struct Result {
    URI                           routerAddress;
    int                           iterations;
    Get                           mode;
    std::size_t                   payloadSize;
    std::chrono::duration<double> duration;
};

Result simpleOneWorkerBenchmark(const URI &routerAddress, Get mode, int iterations, std::size_t payloadSize) {
    Broker broker("benchmarkbroker", benchmarkSettings());
    REQUIRE(broker.bind(routerAddress, Broker::BindOption::Router));

    BasicMdpWorker worker("blob", broker, PayloadHandler(std::string(payloadSize, '\xab')));

    Context        clientContext;
    TestClient     client(routerAddress.scheme() == opencmw::majordomo::SCHEME_INPROC ? broker.context : clientContext);

    RunInThread    brokerRun(broker);
    RunInThread    workerRun(worker);

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
            client.tryRead(pollIntervall);
        }
    } else {
        for (int i = 0; i < iterations; ++i) {
            client.getAndBusyWait("blob", "");
        }
    }

    const auto                          after = std::chrono::system_clock::now();
    const std::chrono::duration<double> diff  = after - before;

    return Result{
        .routerAddress = routerAddress,
        .iterations    = iterations,
        .mode          = mode,
        .payloadSize   = payloadSize,
        .duration      = diff
    };
}

void simpleTwoWorkerBenchmark(const URI &routerAddress, Get mode, int iterations, std::size_t payload1_size, std::size_t payload2_size) {
    Broker broker("benchmarkbroker", benchmarkSettings());
    REQUIRE(broker.bind(routerAddress, Broker::BindOption::Router));
    RunInThread    brokerRun(broker);

    BasicMdpWorker worker1("blob", broker, PayloadHandler(std::string(payload1_size, '\xab')));
    RunInThread    worker1_run(worker1);

    BasicMdpWorker worker2("blob", broker, PayloadHandler(std::string(payload2_size, '\xab')));
    RunInThread    worker2_run(worker2);

    Context        clientContext;
    TestClient     client(routerAddress.scheme() == opencmw::majordomo::SCHEME_INPROC ? broker.context : clientContext);
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
            client.tryRead(pollIntervall);
        }
    } else {
        for (int i = 0; i < iterations; ++i) {
            client.getAndBusyWait(i % 2 == 0 ? "blob1" : "blob2", "");
        }
    }
    const auto                          after = std::chrono::system_clock::now();
    const std::chrono::duration<double> diff  = after - before;

    std::cout << fmt::format("{}: {}. Alternating Payloads {}/{} bytes: {} iterations took {}s ({} messages/s)\n",
            routerAddress.str,
            mode == Get::Async ? "ASYNC" : "SYNC",
            payload1_size,
            payload2_size,
            iterations,
            diff.count(),
            std::round(iterations / diff.count()));
}

int main(int argc, char **argv) {
    const auto          N      = argc > 1 ? std::atoi(argv[1]) : 100000;
    const auto          tcp    = URI("tcp://127.0.0.1:12346");
    const auto          inproc = URI("inproc://benchmark");

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
                result.routerAddress.str,
                result.mode == Get::Async ? "ASYNC" : "SYNC",
                result.payloadSize,
                result.iterations,
                result.duration.count(),
                std::round(result.iterations / result.duration.count()));
    }
}
