#include <stdlib.h>

size_t bytesAllocated = 0;

void* operator new(size_t size)
{
    bytesAllocated += size;
    return malloc(size);
}

#define CATCH_CONFIG_MAIN // This tells the catch header to generate a main
#include <catch2/catch.hpp>

#include <yaz/yaz.hpp>

static std::string generate_string(size_t length) {
    std::string s;
    s.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        s += static_cast<char>(i % 255);
    }

    return s;
}

TEST_CASE("Simple pub/sub example using inproc", "[yaz]") {
    using yaz::Context;
    using yaz::Message;
    using yaz::Socket;

    const std::string_view address = "inproc://test";

    std::vector<Message> receivedQueue;
    Context context;
    Socket publisher(context, ZMQ_PUB, [](auto && /*socket*/, auto && /*message*/) {
        FAIL("Unexpected message received (PUB socket)");
    });
    REQUIRE(publisher.bind(address));

    Socket subscriber(context, ZMQ_SUB, [&receivedQueue](auto && /*socket*/, auto &&message) {
        receivedQueue.emplace_back(std::move(message));
    });

    REQUIRE(subscriber.connect(address));

    // test simple message
    {
        Message test{"Hello hello"};
        publisher.send(std::move(test));

        while (receivedQueue.empty())
            subscriber.read();

        std::vector<Message> received;
        std::swap(receivedQueue, received);
        REQUIRE(received.size() == 1);
        REQUIRE(received[0].parts_count() == 1);
        REQUIRE(received[0][0] == "Hello hello");
    }

    // test message with many parts
    {
        Message test;
        constexpr size_t NumParts = 1024;
        for (size_t i = 0; i < NumParts; ++i) {
            test.add_part(std::to_string(i));
        }

        publisher.send(std::move(test));

        while (receivedQueue.empty())
            subscriber.read();

        std::vector<Message> received;
        std::swap(receivedQueue, received);
        REQUIRE(received.size() == 1);
        REQUIRE(received[0].parts_count() == NumParts);
        for(size_t i = 0; i < NumParts; ++i) {
            REQUIRE(received[0][i] == std::to_string(i));
        }
    }

    // send a 10 MB message and test that there's unnecessary copying of data when sending or receiving
    {
        const auto tenMB = generate_string(10 * 1024 * 1024);

        std::vector<std::string> copyToSend;
        copyToSend.emplace_back(tenMB);

        auto allocatedBefore = bytesAllocated;

        Message msg(std::move(copyToSend));

        // assert no allocations in message creation
        REQUIRE(bytesAllocated - allocatedBefore == 0);

        publisher.send(std::move(msg));

        constexpr auto Slack = 512;

        // no copying of data during sending, allowing for some slack for zeromq internals
        REQUIRE(bytesAllocated - allocatedBefore < Slack);

        while (receivedQueue.empty())
            subscriber.read();

        std::vector<Message> received;
        std::swap(receivedQueue, received);

        // no copying of read data
        REQUIRE(bytesAllocated - allocatedBefore < tenMB.size() + 2 * Slack);
        REQUIRE(received.size() == 1);
        REQUIRE(received[0].parts_count() == 1);
        REQUIRE(received[0][0] == tenMB);
    }
}
