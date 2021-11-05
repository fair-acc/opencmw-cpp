#ifndef MAJORDOMO_OPENCMW_MESSAGE_H
#define MAJORDOMO_OPENCMW_MESSAGE_H

#include <yaz/kill/Message.hpp>
#include <yaz/yaz.hpp>

#include <cassert>
#include <optional>
#include <vector>

namespace Majordomo::OpenCMW {
using yaz::Bytes;
using yaz::MessagePart;

enum class MessageFormat {
    WithSourceId,   ///< 9-frame format, contains the source ID as frame 0, used with ROUTER sockets (broker)
    WithoutSourceId ///< 8-frame format, does not contain the source ID frame
};

template<MessageFormat Format>
class BasicMdpMessage;

using MdpMessage = BasicMdpMessage<MessageFormat::WithoutSourceId>;

template<MessageFormat Format>
class BasicMdpMessage : public yaz::Message {
private:
    static constexpr auto clientProtocol = "MDPC03";
    static constexpr auto workerProtocol = "MDPW03";

    static constexpr auto FrameCount     = Format == MessageFormat::WithSourceId ? 9 : 8;

    enum class Frame : std::size_t {
        SourceId = 0,
        Protocol = Format == MessageFormat::WithSourceId ? 1 : 0,
        Command,
        ServiceName,
        ClientSourceId = ServiceName,
        ClientRequestId,
        Topic,
        Body,
        Error,
        RBAC
    };

    template<typename T>
    static constexpr auto index(T value) {
        return static_cast<std::underlying_type_t<T>>(value);
    }

    template<typename T>
    MessagePart &frameAt(T value) {
        return operator[](index(value));
    }

    template<typename T>
    const MessagePart &frameAt(T value) const {
        return operator[](index(value));
    }

    BasicMdpMessage() {
        resize(FrameCount);
    }

    explicit BasicMdpMessage(char command) {
        resize(FrameCount);
        setCommand(command);
        assert(this->command() == command);
    }

    void setCommand(char command) {
        setFrameData(Frame::Command, new std::string(1, command), MessagePart::dynamic_bytes_tag{});
    }

    BasicMdpMessage(const BasicMdpMessage &) = default;
    BasicMdpMessage   &operator=(const BasicMdpMessage &) = default;

    [[nodiscard]] char command() const {
        assert(frameAt(Frame::Command).data().length() == 1);
        return frameAt(Frame::Command).data()[0];
    }

    // TODO better error handling
    template<typename Message>
    static bool isMessageValid(const Message &ymsg) {
        // TODO better error reporting
        if (ymsg.parts_count() != FrameCount) {
            return false;
        }

        const auto &commandStr = ymsg[index(Frame::Command)];

        if (commandStr.size() != 1) {
            return false;
        }

        const auto &protocol = ymsg[index(Frame::Protocol)].data();
        const auto  command  = static_cast<unsigned char>(commandStr.data()[0]);
        if (protocol == clientProtocol) {
            if (command == 0 || command > static_cast<unsigned char>(ClientCommand::Final)) {
                return false;
            }

        } else if (protocol == workerProtocol) {
            if (command == 0 || command > static_cast<unsigned char>(WorkerCommand::Heartbeat)) {
                return false;
            }
        } else {
            return false;
        }

        return true;
    }

public:
    enum class ClientCommand {
        Get         = 0x01,
        Set         = 0x02,
        Subscribe   = 0x03,
        Unsubscribe = 0x04,
        Partial     = 0x05,
        Final       = 0x06
    };

    enum class WorkerCommand {
        Get        = 0x01,
        Set        = 0x02,
        Partial    = 0x03,
        Final      = 0x04,
        Notify     = 0x05,
        Ready      = 0x06,
        Disconnect = 0x07,
        Heartbeat  = 0x08
    };

    enum class Protocol {
        Client,
        Worker
    };

    explicit BasicMdpMessage(std::vector<yaz::MessagePart> &&parts)
        : yaz::Message(std::move(parts)) {
    }

    ~BasicMdpMessage()                       = default;
    BasicMdpMessage(BasicMdpMessage &&other) = default;
    BasicMdpMessage       &operator=(BasicMdpMessage &&other) = default;

    static BasicMdpMessage createClientMessage(ClientCommand cmd) {
        BasicMdpMessage msg{ static_cast<char>(cmd) };
        msg.setFrameData(Frame::Protocol, clientProtocol, MessagePart::static_bytes_tag{});
        return msg;
    }

    static BasicMdpMessage createWorkerMessage(WorkerCommand cmd) {
        BasicMdpMessage msg{ static_cast<char>(cmd) };
        msg.setFrameData(Frame::Protocol, workerProtocol, MessagePart::static_bytes_tag{});
        return msg;
    }

    BasicMdpMessage clone() const {
        // TODO make this nicer...
        BasicMdpMessage tmp;
        assert(parts_count() == FrameCount);
        for (size_t i = 0; i < FrameCount; ++i)
            tmp.add_part(std::make_unique<std::string>((*this)[i].data()));
        return tmp;
    }

    [[nodiscard]] bool isValid() const {
        return isMessageValid(*this);
    }

    void setProtocol(Protocol protocol) {
        setFrameData(Frame::Protocol,
                protocol == Protocol::Client ? clientProtocol : workerProtocol,
                MessagePart::static_bytes_tag{});
    }

    [[nodiscard]] Protocol protocol() const {
        const auto &protocol = frameAt(Frame::Protocol);
        assert(protocol.data() == clientProtocol || protocol.data() == workerProtocol);
        return protocol.data() == clientProtocol ? Protocol::Client : Protocol::Worker;
    }

    [[nodiscard]] bool isClientMessage() const {
        return protocol() == Protocol::Client;
    }

    [[nodiscard]] bool isWorkerMessage() const {
        return protocol() == Protocol::Worker;
    }

    void setClientCommand(ClientCommand cmd) {
        setCommand(static_cast<char>(cmd));
    }

    [[nodiscard]] ClientCommand clientCommand() const {
        assert(isClientMessage());
        return static_cast<ClientCommand>(command());
    }

    void setWorkerCommand(WorkerCommand cmd) {
        setCommand(static_cast<char>(cmd));
    }

    [[nodiscard]] WorkerCommand workerCommand() const {
        assert(isWorkerMessage());
        return static_cast<WorkerCommand>(command());
    }

    template<typename Field, typename T, typename Tag>
    void setFrameData(Field field, T &&value, Tag tag) {
        frameAt(field) = MessagePart(YAZ_FWD(value), tag);
    }

    template<typename T, typename Tag>
    void setSourceId(T &&sourceId, Tag tag) {
        static_assert(Format == MessageFormat::WithSourceId, "not available for WithoutSourceId format");
        setFrameData(Frame::SourceId, YAZ_FWD(sourceId), tag);
    }

    [[nodiscard]] std::string_view sourceId() const {
        static_assert(Format == MessageFormat::WithSourceId, "not available for WithoutSourceId format");
        return frameAt(Frame::SourceId).data();
    }

    template<typename T, typename Tag>
    void setServiceName(T &&serviceName, Tag tag) {
        setFrameData(Frame::ServiceName, YAZ_FWD(serviceName), tag);
    }

    [[nodiscard]] std::string_view serviceName() const {
        return frameAt(Frame::ServiceName).data();
    }

    template<typename T, typename Tag>
    void setClientSourceId(T &&clientSourceId, Tag tag) {
        setFrameData(Frame::ClientSourceId, YAZ_FWD(clientSourceId), tag);
    }

    [[nodiscard]] std::string_view clientSourceId() const {
        return frameAt(Frame::ClientSourceId).data();
    }

    template<typename T, typename Tag>
    void setClientRequestId(T &&clientRequestId, Tag tag) {
        setFrameData(Frame::ClientRequestId, YAZ_FWD(clientRequestId), tag);
    }

    [[nodiscard]] std::string_view clientRequestId() const {
        return frameAt(Frame::ClientRequestId).data();
    }

    template<typename T, typename Tag>
    void setTopic(T &&topic, Tag tag) {
        setFrameData(Frame::Topic, YAZ_FWD(topic), tag);
    }

    [[nodiscard]] std::string_view topic() const {
        return frameAt(Frame::Topic).data();
    }

    template<typename T, typename Tag>
    void setBody(T &&body, Tag tag) {
        setFrameData(Frame::Body, YAZ_FWD(body), tag);
    }

    [[nodiscard]] std::string_view body() const {
        return frameAt(Frame::Body).data();
    }

    template<typename T, typename Tag>
    void setError(T &&error, Tag tag) {
        setFrameData(Frame::Error, YAZ_FWD(error), tag);
    }

    [[nodiscard]] std::string_view error() const {
        return frameAt(Frame::Error).data();
    }

    template<typename T, typename Tag>
    void setRbac(T &&rbac, Tag tag) {
        setFrameData(Frame::RBAC, YAZ_FWD(rbac), tag);
    }

    [[nodiscard]] std::string_view rbac() const {
        return frameAt(Frame::RBAC).data();
    }
};

static_assert(std::is_nothrow_move_constructible<BasicMdpMessage<MessageFormat::WithSourceId>>::value, "MdpMessage should be noexcept MoveConstructible");
static_assert(std::is_nothrow_move_constructible<BasicMdpMessage<MessageFormat::WithoutSourceId>>::value, "MdpMessage should be noexcept MoveConstructible");
} // namespace Majordomo::OpenCMW

#endif
