#include "helpers.hpp"

#include <majordomo/BasicMdpWorker.hpp>
#include <majordomo/broker.hpp>
#include <majordomo/client.hpp>
#include <majordomo/Message.hpp>

#include <fmt/format.h>

using Majordomo::OpenCMW::BasicMdpWorker;
using Majordomo::OpenCMW::Broker;
using Majordomo::OpenCMW::Client;
using Majordomo::OpenCMW::MdpMessage;

#define REQUIRE(expression) { if (!expression) { std::cerr << fmt::format("'{}' failed\n", #expression); std::terminate(); } }

class Worker : public BasicMdpWorker {
    std::string _payload;
public:
    explicit Worker(std::string payload, yaz::Context &context, std::string service_name)
        : BasicMdpWorker(context, std::move(service_name))
        , _payload(std::move(payload)) {
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

class TestClient : public Client {
public:
    explicit TestClient(yaz::Context &context)
        : Client(context) {
    }

    void handle_response(MdpMessage &&) override {
        debug() << "Unexpected message not handled by callback\n";
        std::terminate();
    }

    template <typename BodyType>
    void get_and_busy_wait(std::string service_name, BodyType body) {
        bool received_reply = false;
        get(std::move(service_name), YAZ_FWD(body), [&received_reply](auto &&) {
            received_reply = true;
        });

        while (!received_reply) {
            try_read();
        }
    }
};

enum class Get {
    Sync,
    Async
};

void simple_one_worker_benchmark(std::string router_address, Get mode, int iterations, std::size_t payload_size) {
    yaz::Context context;
    Broker broker("benchmarkbroker", "", context);
    REQUIRE(broker.bind(router_address, Broker::BindOption::Router));
    RunInThread broker_run(broker);

    Worker worker(std::string(payload_size, '\xab'), context, "blob");
    REQUIRE(worker.connect(router_address));
    RunInThread worker_run(worker);

    TestClient client(context);
    REQUIRE(client.connect(router_address));

    const auto before = std::chrono::system_clock::now();

    if (mode == Get::Async) {
        int counter = 0;
        for (int i = 0; i < iterations; ++i) {
            client.get("blob", "", [&counter](auto &&) {
                ++counter;
            });
        }

        while (counter < iterations) {
            client.try_read();
        }
    } else {
        for (int i = 0; i < iterations; ++i) {
            client.get_and_busy_wait("blob", "");
        }
    }

    const auto after = std::chrono::system_clock::now();
    const std::chrono::duration<double> diff = after - before;

    std::cout << fmt::format("{}: {}. Payload {} bytes: {} iterations took {}s ({} messages/s)\n",
                             router_address,
                             mode == Get::Async ? "ASYNC" : "SYNC",
                             payload_size,
                             iterations,
                             diff.count(),
                             std::round(iterations / diff.count()));
}

void simple_two_worker_benchmark(std::string router_address, Get mode, int iterations, std::size_t payload1_size, std::size_t payload2_size) {
    yaz::Context context;
    Broker broker("benchmarkbroker", "", context);
    REQUIRE(broker.bind(router_address, Broker::BindOption::Router));
    RunInThread broker_run(broker);

    Worker worker1(std::string(payload1_size, '\xab'), context, "blob1");
    REQUIRE(worker1.connect(router_address));
    RunInThread worker1_run(worker1);

    Worker worker2(std::string(payload2_size, '\xab'), context, "blob2");
    REQUIRE(worker2.connect(router_address));
    RunInThread worker2_run(worker2);

    TestClient client(context);
    REQUIRE(client.connect(router_address));

    const auto before = std::chrono::system_clock::now();

    if (mode == Get::Async) {
        int counter = 0;
        for (int i = 0; i < iterations; ++i) {
            client.get(i % 2 == 0 ? "blob1" : "blob2", "", [&counter](auto &&) {
                ++counter;
            });
        }

        while (counter < iterations) {
            client.try_read();
        }
    } else {
        for (int i = 0; i < iterations; ++i) {
            client.get_and_busy_wait(i % 2 == 0 ? "blob1" : "blob2", "");
        }
    }
    const auto after = std::chrono::system_clock::now();
    const std::chrono::duration<double> diff = after - before;

    std::cout << fmt::format("{}: {}. Alternating Payloads {}/{} bytes: {} iterations took {}s ({} messages/s)\n",
                             router_address,
                             mode == Get::Async ? "ASYNC" : "SYNC",
                             payload1_size,
                             payload2_size,
                             iterations,
                             diff.count(),
                             std::round(iterations / diff.count()));
}

int main(int argc, char **argv) {

    const auto N = argc > 1 ? std::atoi(argv[1]) : 10000;
    const auto tcp = std::string("tcp://127.0.0.1:12346");
    const auto inproc = std::string("inproc://benchmark");

    simple_one_worker_benchmark(tcp, Get::Async, N, 10);
    simple_one_worker_benchmark(inproc, Get::Async, N, 10);
    simple_one_worker_benchmark(tcp, Get::Sync, N, 10);
    simple_one_worker_benchmark(inproc, Get::Sync, N, 10);
    //simple_one_worker_benchmark(tcp, Get::Async, 3000, 1024);
    //simple_one_worker_benchmark(tcp, Get::Async, 3000, 1024 * 1024);
    simple_two_worker_benchmark(inproc, Get::Async, 10000, 10, 10);
}
