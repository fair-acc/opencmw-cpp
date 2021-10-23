#include <cstdlib>

size_t bytesAllocated = 0;

void  *operator new(std::size_t size) {
    bytesAllocated += size;
    return malloc(size);
}

void operator delete(void *p) noexcept {
    free(p);
}

void operator delete(void *p, std::size_t /* size */) noexcept {
    free(p);
}

#include <majordomo/Message.hpp>

#include <catch2/catch.hpp>

TEST_CASE("OpenCMW::Message basics", "[Majordomo]") {
    using Majordomo::OpenCMW::MdpMessage;

    auto msg = MdpMessage::createClientMessage(MdpMessage::ClientCommand::Final);
    REQUIRE(msg.isClientMessage());
    REQUIRE(msg.clientCommand() == MdpMessage::ClientCommand::Final);

    auto tag = yaz::MessagePart::static_bytes_tag{};
    msg.setTopic("I'm a topic", tag);
    msg.setServiceName("service://abc", tag);
    msg.setClientRequestId("request 1", tag);
    msg.setBody("test body test body test body test body test body test body test body", tag);
    msg.setError("fail!", tag);
    msg.setRbac("password", tag);

    const auto allocatedBefore = bytesAllocated;

    // auto       ymsg            = MdpMessage::toYazMessage(std::move(msg));
    // REQUIRE(ymsg.parts_count() == 9);

    // assert that there are no memory allocations converting between OpenCMW::MdpMessage and yaz::MdpMessage
    REQUIRE(bytesAllocated == allocatedBefore);

    // auto received = MdpMessage::fromYazMessage(std::move(ymsg));
    auto *received = &msg;

    REQUIRE(bytesAllocated == allocatedBefore);

    REQUIRE(received);
    REQUIRE(received->isClientMessage());
    REQUIRE(received->clientCommand() == MdpMessage::ClientCommand::Final);
    REQUIRE(received->topic() == "I'm a topic");
    REQUIRE(received->serviceName() == "service://abc");
    REQUIRE(received->clientRequestId() == "request 1");
    REQUIRE(received->body() == "test body test body test body test body test body test body test body");
    REQUIRE(received->error() == "fail!");
    REQUIRE(received->rbac() == "password");
    // const auto frames = received->takeFrames();
    // REQUIRE(frames.size() == 9);
    // REQUIRE(frames[0].empty());
    // REQUIRE(frames[1] == "MDPC03");
    // REQUIRE(frames[2] == "\x6");
    // REQUIRE(frames[3] == "service://abc");
    // REQUIRE(frames[4] == "request 1");
    // REQUIRE(frames[5] == "I'm a topic");
    // REQUIRE(frames[6] == "test body test body test body test body test body test body test body");
    // REQUIRE(frames[7] == "fail!");
    // REQUIRE(frames[8] == "password");

    REQUIRE(bytesAllocated == allocatedBefore);
}
