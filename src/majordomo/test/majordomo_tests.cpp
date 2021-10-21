#include <stdlib.h>

size_t bytesAllocated = 0;

void* operator new(size_t size)
{
    bytesAllocated += size;
    return malloc(size);
}

#include <majordomo/Message.hpp>

#include <catch2/catch.hpp>

TEST_CASE("OpenCMW::Message basics", "[Majordomo]") {
    using Majordomo::OpenCMW::Message;

    auto msg = Message::createClientMessage(Message::ClientCommand::Final);
    REQUIRE(msg.isClientMessage());
    REQUIRE(msg.clientCommand() == Message::ClientCommand::Final);
    msg.setTopic("I'm a topic");
    msg.setServiceName("service://abc");
    msg.setClientRequestId("request 1");
    msg.setBody("test body test body test body test body test body test body test body"); // should be long enough for std::string to allocate
    msg.setError("fail!");
    msg.setRbac("password");

    const auto allocatedBefore = bytesAllocated;

    auto ymsg = Message::toYazMessage(std::move(msg));
    REQUIRE(ymsg.parts_count() == 9);

    // assert that there are no memory allocations converting between OpenCMW::Message and yaz::Message
    REQUIRE(bytesAllocated == allocatedBefore);

    auto received = Message::fromYazMessage(std::move(ymsg));

    REQUIRE(bytesAllocated == allocatedBefore);

    REQUIRE(received.has_value());
    REQUIRE(received->isClientMessage());
    REQUIRE(received->clientCommand() == Message::ClientCommand::Final);
    REQUIRE(received->topic() == "I'm a topic");
    REQUIRE(received->serviceName() == "service://abc");
    REQUIRE(received->clientRequestId() == "request 1");
    REQUIRE(received->body() == "test body test body test body test body test body test body test body");
    REQUIRE(received->error() == "fail!");
    REQUIRE(received->rbac() == "password");
    const auto frames = received->takeFrames();
    REQUIRE(frames.size() == 9);
    REQUIRE(frames[0].empty());
    REQUIRE(frames[1] == "MDPC03");
    REQUIRE(frames[2] == "\x6");
    REQUIRE(frames[3] == "service://abc");
    REQUIRE(frames[4] == "request 1");
    REQUIRE(frames[5] == "I'm a topic");
    REQUIRE(frames[6] == "test body test body test body test body test body test body test body");
    REQUIRE(frames[7] == "fail!");
    REQUIRE(frames[8] == "password");

    REQUIRE(bytesAllocated == allocatedBefore);
}
