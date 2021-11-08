#ifndef OPENCMW_MAJORDOMO_MESSAGE_H
#define OPENCMW_MAJORDOMO_MESSAGE_H

#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <zmq.h>

#include "Debug.hpp"

#include "ZmqPtr.hpp"

namespace opencmw::majordomo {
using Bytes = std::string;
using Byte  = std::string::value_type;

class MessageFrame {
private:
    bool _owning = true;

    // mutable as 0mq API knows no const
    mutable zmq_msg_t _message;

public:
    struct static_bytes_tag {};
    struct dynamic_bytes_tag {};

    MessageFrame() {
        zmq_msg_init(&_message);
    }

    explicit MessageFrame(Bytes *buf, dynamic_bytes_tag /*tag*/ = {}) {
        zmq_msg_init_data(
                &_message, buf->data(), buf->size(),
                [](void * /*unused*/, void *bufOwned) {
                    delete static_cast<Bytes *>(bufOwned);
                },
                buf);
    }

    explicit MessageFrame(std::string_view view, dynamic_bytes_tag tag)
        : MessageFrame(new std::string(view), tag) {}

    explicit MessageFrame(std::string_view buf, static_bytes_tag) {
        zmq_msg_init_data(
                &_message, const_cast<char *>(buf.data()), buf.size(),
                [](void *, void *) {
                    // We don't want to delete a static RO string literal
                },
                nullptr);
    }

    ~MessageFrame() {
        if (_owning) {
            zmq_msg_close(&_message);
        }
    }

    MessageFrame(const MessageFrame &other) = delete;
    MessageFrame &operator=(const MessageFrame &other) = delete;

    MessageFrame(MessageFrame &&other) noexcept
        : MessageFrame() {
        _owning = false;
        std::swap(_owning, other._owning);
        zmq_msg_move(&_message, &other._message);
    }

    MessageFrame &operator=(MessageFrame &&other) noexcept {
        _owning = false;
        std::swap(_owning, other._owning);
        zmq_msg_move(&_message, &other._message);
        return *this;
    }

    [[nodiscard]] MessageFrame clone() const {
        return MessageFrame(std::make_unique<std::string>(data()).release(), dynamic_bytes_tag{});
    }

    void swap(MessageFrame &other) {
        std::swap(_owning, other._owning);
        std::swap(_message, other._message);
    }

    // Reads a message from the socket
    // Returns the number of received bytes
    Result<int> receive(Socket &socket, int flags) {
        auto result = zmq_invoke(zmq_msg_recv, &_message, socket, flags);
        _owning     = result.isValid();
        return result;
    }

    // Sending is not const as 0mq nullifies the message
    // See: http://api.zeromq.org/3-2:zmq-msg-send
    [[nodiscard]] auto send(Socket &socket, int flags) {
        if (data().size() > 0 && data()[0] == '`') {
            throw 32;
        }
        auto result = zmq_invoke(zmq_msg_send, &_message, socket, flags);
        assert(result.isValid());
        _owning = false;
        return result;
    }

    [[nodiscard]] std::size_t
    size() const {
        // assert(_owning);
        return zmq_msg_size(&_message);
    }

    std::string_view data() const {
        return std::string_view(
                static_cast<char *>(zmq_msg_data(&_message)), size());
    }
};

enum class MessageFormat {
    WithSourceId,   ///< 9-frame format, contains the source ID as frame 0, used with ROUTER sockets (broker)
    WithoutSourceId ///< 8-frame format, does not contain the source ID frame
};

template<MessageFormat Format>
class BasicMdpMessage {
private:
    using this_t                             = BasicMdpMessage<Format>;

    static constexpr auto clientProtocol     = std::string_view{ "MDPC03" };
    static constexpr auto workerProtocol     = std::string_view{ "MDPW03" };

    static constexpr auto MessageFormat      = Format;
    static constexpr auto RequiredFrameCount = std::size_t{ Format == MessageFormat::WithSourceId ? 9 : 8 };

    // In order to have a zero-copy sending API, we allow API clients
    // to give us their internal data that will be freed automatically
    // when the message is sent.
    //
    // The non-nullptr items in the vector have not been sent yet
    //
    // TODO: Investigate whether we want unique_ptrs with custom
    // deleters (that is, do we want a generic smart pointer support)
    using BytesPtr = std::unique_ptr<Bytes>;
    std::vector<MessageFrame> _frames;

    // Helper function to print out the current message
    // TODO: Remove as we don't want to depend on <iostream>
    friend std::ostream &operator<<(std::ostream &out, const BasicMdpMessage &message) {
        out << '{';
        for (const auto &frame : message._frames) {
            out << frame.data();
        }
        out << '}';
        return out;
    }

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

    struct empty_tag {};

    BasicMdpMessage(empty_tag) {
    }

    template<typename T>
    static constexpr auto index(T value) {
        return static_cast<std::underlying_type_t<T>>(value);
    }

    // Just a workaround for items in initializer lists not being
    // movable, and we need to move unique ptrs in setFrames.
    struct MovableBytesPtrWrapper {
        mutable BytesPtr ptr;

        // Getting the unique_ptr from the wrapper
        operator BytesPtr() const && { return std::move(ptr); }
        MovableBytesPtrWrapper(BytesPtr &&_ptr)
            : ptr{ std::move(_ptr) } {}
    };

public:
    void setFrames(std::array<MovableBytesPtrWrapper, RequiredFrameCount> &&data) {
        _frames.resize(0);
        _frames.reserve(RequiredFrameCount);
        for (std::size_t i = 0; i < RequiredFrameCount; ++i) {
            _frames.emplace_back(data[i].ptr.release(), MessageFrame::dynamic_bytes_tag{});
        }
        assert(_frames.size() == RequiredFrameCount);
    }

    [[nodiscard]] MessageFrame &frameAt(int index) {
        return _frames[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] const MessageFrame &frameAt(int index) const {
        return _frames[static_cast<std::size_t>(index)];
    }

    template<typename T>
    [[nodiscard]] MessageFrame &frameAt(T value) {
        return _frames[index(value)];
    }

    template<typename T>
    [[nodiscard]] const MessageFrame &frameAt(T value) const {
        return _frames[index(value)];
    }

    BasicMdpMessage() {
        _frames.resize(RequiredFrameCount);
    }

    explicit BasicMdpMessage(char command) {
        _frames.resize(RequiredFrameCount);
        setCommand(command);
        assert(this->command() == command);
    }

    void setCommand(char command) {
        setFrameData(Frame::Command, new std::string(1, command), MessageFrame::dynamic_bytes_tag{});
    }

    BasicMdpMessage(const BasicMdpMessage &) = default;
    BasicMdpMessage   &operator=(const BasicMdpMessage &) = default;

    [[nodiscard]] char command() const {
        assert(frameAt(Frame::Command).data().length() == 1);
        return frameAt(Frame::Command).data()[0];
    }

    [[nodiscard]] auto sendFrame(Socket &socket, std::size_t index, int flags) {
        assert(flags & ZMQ_DONTWAIT);
        while (true) {
            const auto result = _frames[index].send(socket, flags);
            if (result) {
                return result;
            }
            if (result.error() != EAGAIN) {
                return result;
            }
        }
    }

    [[nodiscard]] auto sendFrame(Socket &socket, std::size_t index) {
        const auto flags = index + 1 == RequiredFrameCount ? ZMQ_DONTWAIT : ZMQ_DONTWAIT | ZMQ_SNDMORE;
        return sendFrame(socket, index, flags);
    }

    [[nodiscard]] auto send(Socket &socket) {
        decltype(sendFrame(socket, 0)) result{ 0 };
        for (std::size_t i = 0; i < RequiredFrameCount; ++i) {
            result = sendFrame(socket, i);
            if (!result) {
                return result;
            }
        }

        return result;
    }

    [[nodiscard]] bool receiveInplace(Socket &socket) {
        _frames.clear();

        while (true) {
            MessageFrame frame;
            const auto   byteCountResult = frame.receive(socket, ZMQ_DONTWAIT);

            if (byteCountResult) {
                _frames.emplace_back(std::move(frame));
            } else {
                return false;
            }

            int64_t more;
            size_t  moreSize = sizeof(more);
            if (!zmq_invoke(zmq_getsockopt, socket, ZMQ_RCVMORE, &more, &moreSize)) {
                // Can not check rcvmore
                return false;

            } else if (more != 0) {
                // Multi-part message
                continue;

            } else {
                break;
            }
        }

        return true;
    }

    static std::optional<this_t> receive(Socket &socket) {
        std::optional<this_t> result = this_t();
        if (!result->receiveInplace(socket)) {
            return {};
        }

        return result;
    }

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

    explicit BasicMdpMessage(std::vector<MessageFrame> &&frames)
        : _frames(std::move(frames)) {}

    ~BasicMdpMessage()
            = default;
    BasicMdpMessage(BasicMdpMessage &&other) = default;
    BasicMdpMessage       &operator=(BasicMdpMessage &&other) = default;

    static BasicMdpMessage createClientMessage(ClientCommand cmd) {
        BasicMdpMessage msg{ static_cast<char>(cmd) };
        msg.setFrameData(Frame::Protocol, clientProtocol, MessageFrame::static_bytes_tag{});
        return msg;
    }

    static BasicMdpMessage createWorkerMessage(WorkerCommand cmd) {
        BasicMdpMessage msg{ static_cast<char>(cmd) };
        msg.setFrameData(Frame::Protocol, workerProtocol, MessageFrame::static_bytes_tag{});
        return msg;
    }

    bool isValid() const {
        // TODO better error reporting
        if (_frames.size() != RequiredFrameCount) {
            debugWithLocation() << "Message size is wrong";
            return false;
        }

        const auto &commandStr = _frames[index(Frame::Command)];

        if (commandStr.size() != 1) {
            debugWithLocation() << "Command size is wrong";
            return false;
        }

        const auto &protocol = _frames[index(Frame::Protocol)].data();
        const auto  command  = static_cast<unsigned char>(commandStr.data()[0]);
        if (protocol == clientProtocol) {
            if (command == 0 || command > static_cast<unsigned char>(ClientCommand::Final)) {
                debugWithLocation() << "Message command out of range for client";
                return false;
            }

        } else if (protocol == workerProtocol) {
            if (command == 0 || command > static_cast<unsigned char>(WorkerCommand::Heartbeat)) {
                debugWithLocation() << "Message command out of range for worker";
                return false;
            }
        } else {
            debugWithLocation() << "Message has a wrong protocol" << protocol;
            return false;
        }

        return true;
    }

    BasicMdpMessage clone() const {
        // TODO make this nicer...
        BasicMdpMessage tmp{ empty_tag{} };
        assert(_frames.size() == RequiredFrameCount);
        tmp._frames.reserve(RequiredFrameCount);
        for (const auto &frame : _frames) {
            tmp._frames.emplace_back(frame.clone());
        }
        return tmp;
    }

    void setProtocol(Protocol protocol) {
        setFrameData(Frame::Protocol,
                protocol == Protocol::Client ? clientProtocol : workerProtocol,
                MessageFrame::static_bytes_tag{});
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

    std::size_t availableFrameCount() const {
        return _frames.size();
    }

    std::size_t requiresFrameCount() const {
        return RequiredFrameCount;
    }

    template<typename Field, typename T, typename Tag>
    void setFrameData(Field field, T &&value, Tag tag) {
        frameAt(field) = MessageFrame(std::forward<T>(value), tag);
    }

    template<typename T, typename Tag>
    void setSourceId(T &&sourceId, Tag tag) {
        static_assert(Format == MessageFormat::WithSourceId, "not available for WithoutSourceId format");
        setFrameData(Frame::SourceId, std::forward<T>(sourceId), tag);
    }

    [[nodiscard]] std::string_view sourceId() const {
        static_assert(Format == MessageFormat::WithSourceId, "not available for WithoutSourceId format");
        return frameAt(Frame::SourceId).data();
    }

    template<typename T, typename Tag>
    void setServiceName(T &&serviceName, Tag tag) {
        setFrameData(Frame::ServiceName, std::forward<T>(serviceName), tag);
    }

    [[nodiscard]] std::string_view serviceName() const {
        return frameAt(Frame::ServiceName).data();
    }

    template<typename T, typename Tag>
    void setClientSourceId(T &&clientSourceId, Tag tag) {
        setFrameData(Frame::ClientSourceId, std::forward<T>(clientSourceId), tag);
    }

    [[nodiscard]] std::string_view clientSourceId() const {
        return frameAt(Frame::ClientSourceId).data();
    }

    template<typename T, typename Tag>
    void setClientRequestId(T &&clientRequestId, Tag tag) {
        setFrameData(Frame::ClientRequestId, std::forward<T>(clientRequestId), tag);
    }

    [[nodiscard]] std::string_view clientRequestId() const {
        return frameAt(Frame::ClientRequestId).data();
    }

    template<typename T, typename Tag>
    void setTopic(T &&topic, Tag tag) {
        setFrameData(Frame::Topic, std::forward<T>(topic), tag);
    }

    [[nodiscard]] std::string_view topic() const {
        return frameAt(Frame::Topic).data();
    }

    template<typename T, typename Tag>
    void setBody(T &&body, Tag tag) {
        setFrameData(Frame::Body, std::forward<T>(body), tag);
    }

    [[nodiscard]] std::string_view body() const {
        return frameAt(Frame::Body).data();
    }

    template<typename T, typename Tag>
    void setError(T &&error, Tag tag) {
        setFrameData(Frame::Error, std::forward<T>(error), tag);
    }

    [[nodiscard]] std::string_view error() const {
        return frameAt(Frame::Error).data();
    }

    template<typename T, typename Tag>
    void setRbac(T &&rbac, Tag tag) {
        setFrameData(Frame::RBAC, std::forward<T>(rbac), tag);
    }

    [[nodiscard]] std::string_view rbac() const {
        return frameAt(Frame::RBAC).data();
    }
};

using MdpMessage = BasicMdpMessage<MessageFormat::WithoutSourceId>;

static_assert(std::is_nothrow_move_constructible_v<MdpMessage>, "MdpMessage should be noexcept MoveConstructible");
static_assert(std::is_nothrow_move_constructible_v<MdpMessage>, "MdpMessage should be noexcept MoveConstructible");
} // namespace opencmw::majordomo

#endif
