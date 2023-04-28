#ifndef OPENCMW_MAJORDOMO_MESSAGE_H
#define OPENCMW_MAJORDOMO_MESSAGE_H

#include <array>
#include <cassert>
#include <memory>
#include <optional>
#include <string>

#include <zmq.h>

#include "Debug.hpp"

// core
#include <ZmqPtr.hpp>

namespace opencmw::majordomo { // TODO: move to opencmw and header to new 'core' package since these are needed by 'broker','worker' and 'client'
using Bytes = std::string;
using Byte  = std::string::value_type;

class MessageFrame { // TODO: N.B. eventually remove debug statement for non-conforming messages (hot-spot candidates)
private:
    bool _owning = true;

    // mutable as 0mq API knows no const
    mutable zmq_msg_t _message;

public:
    struct static_bytes_tag {};
    struct dynamic_bytes_tag {};

    MessageFrame()
        : _message() { zmq_msg_init(&_message); }
    explicit MessageFrame(Bytes *buf, dynamic_bytes_tag /*tag*/ = {})
        : _message() {
        zmq_msg_init_data(
                &_message, buf->data(), buf->size(),
                [](void * /*unused*/, void *bufOwned) {
                    delete static_cast<Bytes *>(bufOwned);
                },
                buf);
    }

    explicit MessageFrame(std::string_view view, dynamic_bytes_tag tag)
        : MessageFrame(new std::string(view), tag) {}

    explicit MessageFrame(std::string_view buf, static_bytes_tag)
        : _message() {
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
    Result<int> receive(const Socket &socket, int flags) {
        auto result = zmq_invoke(zmq_msg_recv, &_message, socket, flags);
        _owning     = result.isValid();
        return result;
    }

    // Sending is not const as 0mq nullifies the message
    // See: http://api.zeromq.org/3-2:zmq-msg-send
    [[nodiscard]] auto send(const Socket &socket, int flags) {
        auto result = zmq_invoke(zmq_msg_send, &_message, socket, flags);
        assert(result.isValid() || result.error() == EAGAIN);
        _owning = !result.isValid();
        return result;
    }

    [[nodiscard]] std::size_t
    size() const {
        // assert(_owning);
        return zmq_msg_size(&_message);
    }

    std::string_view data() const {
        return { static_cast<char *>(zmq_msg_data(&_message)), size() };
    }
};

enum class MessageFormat {
    WithSourceId,   ///< 9-frame format, contains the source ID as frame 0, used with ROUTER sockets (broker)
    WithoutSourceId ///< 8-frame format, does not contain the source ID frame
};

enum class Command : unsigned char {
    Invalid     = 0x00,
    Get         = 0x01,
    Set         = 0x02,
    Partial     = 0x03,
    Final       = 0x04,
    Ready       = 0x05, ///< optional for client
    Disconnect  = 0x06, ///< optional for client
    Subscribe   = 0x07, ///< client-only
    Unsubscribe = 0x08, ///< client-only
    Notify      = 0x09, ///< worker-only
    Heartbeat   = 0x0a  ///< optional for client
};

enum class Protocol {
    Client,
    Worker
};

template<MessageFormat Format>
class BasicMdpMessage {
private:
    using this_t                             = BasicMdpMessage<Format>;

    static constexpr auto clientProtocol     = std::string_view{ "MDPC03" };
    static constexpr auto workerProtocol     = std::string_view{ "MDPW03" };
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
    std::array<MessageFrame, RequiredFrameCount> _frames;

    // Helper function to print out the current message
    // TODO: Remove as we don't want to depend on <iostream> -> move to existing Debug.h reflection-based helper/printout functions
    friend std::ostream &operator<<(std::ostream &out, const BasicMdpMessage &message) {
        out << '{';
        for (const auto &frame : message._frames) {
            for (const char c : frame.data()) {
                if (c < 0 || !::isprint(c)) {
                    out << '<' << (c < 0 ? 256 - c : c) << '>';
                } else {
                    out << c;
                }
            }
            out << ';';
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

    template<typename T>
    static constexpr auto index(T value) {
        if constexpr (std::numeric_limits<T>::is_integer) {
            return value;
        } else {
            return static_cast<std::underlying_type_t<T>>(value);
        }
    }

    // Just a workaround for items in initializer lists not being
    // movable, and we need to move unique ptrs in setFrames.
    struct MovableBytesPtrWrapper {
        mutable BytesPtr ptr;

        // Getting the unique_ptr from the wrapper
        operator BytesPtr() const && { return std::move(ptr); } // NOLINT(google-explicit-constructor)
        MovableBytesPtrWrapper(BytesPtr &&_ptr)                 // NOLINT(google-explicit-constructor)
            : ptr{ std::move(_ptr) } {}
    };

public:
    BasicMdpMessage() = default;

    explicit BasicMdpMessage(Command command) {
        setCommand(command);
        assert(this->command() == command);
    }

    static BasicMdpMessage createClientMessage(Command cmd) {
        BasicMdpMessage msg{ cmd };
        msg.setFrameData(Frame::Protocol, clientProtocol, MessageFrame::static_bytes_tag{});
        return msg;
    }

    static BasicMdpMessage createWorkerMessage(Command cmd) {
        BasicMdpMessage msg{ cmd };
        msg.setFrameData(Frame::Protocol, workerProtocol, MessageFrame::static_bytes_tag{});
        return msg;
    }

    BasicMdpMessage clone() const {
        // TODO make this nicer...
        BasicMdpMessage tmp{};
        assert(_frames.size() == RequiredFrameCount);
        for (std::size_t i = 0; i < _frames.size(); ++i) {
            tmp._frames[i] = _frames[i].clone();
        }
        return tmp;
    }

    [[nodiscard]] MessageFrame       &frameAt(int index) { return _frames[static_cast<std::size_t>(index)]; }
    [[nodiscard]] const MessageFrame &frameAt(int index) const { return _frames[static_cast<std::size_t>(index)]; }

    template<typename T>
    [[nodiscard]] MessageFrame &frameAt(T value) { return _frames[index(value)]; } // TODO: N.B. unused
    template<typename T>
    [[nodiscard]] const MessageFrame &frameAt(T value) const { return _frames[index(value)]; }

    void                              setFrames(std::array<MovableBytesPtrWrapper, RequiredFrameCount> &&data) {
        for (std::size_t i = 0; i < RequiredFrameCount; ++i) {
            _frames[i] = MessageFrame(data[i].ptr.release(), MessageFrame::dynamic_bytes_tag{});
        }
    }

    [[nodiscard]] auto sendFrame(const Socket &socket, std::size_t index, int flags) {
        assert(flags & ZMQ_DONTWAIT);
        while (true) { // TODO -Q: could become a infinite busy-loop?!?
            const auto result = _frames[index].send(socket, flags);
            if (result) {
                return result;
            }
            if (result.error() != EAGAIN) {
                return result;
            }
        }
    }

    [[nodiscard]] auto sendFrame(const Socket &socket, std::size_t index) {
        const auto flags = index + 1 == RequiredFrameCount ? ZMQ_DONTWAIT : ZMQ_DONTWAIT | ZMQ_SNDMORE;
        return sendFrame(socket, index, flags);
    }

    [[nodiscard]] auto send(const Socket &socket) {
        decltype(sendFrame(socket, 0)) result{ 0 };
        for (std::size_t i = 0; i < RequiredFrameCount; ++i) {
            result = sendFrame(socket, i);
            if (!result) {
                return result;
            }
        }

        return result;
    }

    static std::optional<this_t> receive(const Socket &socket) {
        std::optional<this_t> r;
        std::size_t           framesReceived = 0;

        while (true) {
            MessageFrame frame;
            const auto   byteCountResult = frame.receive(socket, ZMQ_DONTWAIT);

            if (byteCountResult) {
                if (framesReceived == 0) {
                    r = this_t();
                }
                if (framesReceived < r->_frames.size()) {
                    r->_frames[framesReceived] = std::move(frame);
                }
                ++framesReceived;
            } else {
                return {};
            }

            int64_t more;
            size_t  moreSize = sizeof(more);
            if (!zmq_invoke(zmq_getsockopt, socket, ZMQ_RCVMORE, &more, &moreSize)) {
                // Can not check rcvmore
                return {};

            } else if (more != 0) {
                // Multi-part message
                continue;

            } else {
                break;
            }
        }

        if (framesReceived != RequiredFrameCount) {
            return {};
        }

        return r;
    }

    [[nodiscard]] bool isValid() const {
        // TODO better error reporting
        const auto &commandStr = _frames[index(Frame::Command)];

        if (commandStr.size() != 1) {
            return false;
        }

        const auto &protocol = _frames[index(Frame::Protocol)].data();
        const auto  command  = static_cast<unsigned char>(commandStr.data()[0]);

        if (command < static_cast<unsigned char>(Command::Invalid) || command > static_cast<unsigned char>(Command::Heartbeat)) {
            return false;
        }

        if (protocol == clientProtocol) {
            if (command == static_cast<unsigned char>(Command::Notify)) {
                return false;
            }
        } else if (protocol == workerProtocol) {
            if (command == static_cast<unsigned char>(Command::Subscribe) || command == static_cast<unsigned char>(Command::Unsubscribe)) {
                return false;
            }
        } else {
            return false;
        }

        return true;
    }

    void                   setProtocol(Protocol protocol) { setFrameData(Frame::Protocol, protocol == Protocol::Client ? clientProtocol : workerProtocol, MessageFrame::static_bytes_tag{}); }
    [[nodiscard]] Protocol protocol() const {
        const auto &protocol = frameAt(Frame::Protocol);
        assert(protocol.data() == clientProtocol || protocol.data() == workerProtocol);
        return protocol.data() == clientProtocol ? Protocol::Client : Protocol::Worker;
    }

    [[nodiscard]] bool isClientMessage() const { return protocol() == Protocol::Client; }
    [[nodiscard]] bool isWorkerMessage() const { return protocol() == Protocol::Worker; }

    void               setCommand(Command command) {
        assert(command != Command::Invalid);
        static constexpr auto commandStrings = std::array<std::string_view, 11>{
            "\x0", "\x1", "\x2", "\x3", "\x4", "\x5", "\x6", "\x7", "\x8", "\x9", "\xa"
        };
        setFrameData(Frame::Command, commandStrings[static_cast<std::size_t>(command)], MessageFrame::static_bytes_tag{});
    }

    [[nodiscard]] Command command() const {
        assert(frameAt(Frame::Command).data().length() == 1);
        assert(static_cast<unsigned char>(frameAt(Frame::Command).data()[0]) <= static_cast<unsigned char>(Command::Heartbeat));
        return static_cast<Command>(frameAt(Frame::Command).data()[0]);
    }

    [[nodiscard]] std::size_t availableFrameCount() const { return _frames.size(); }
    [[nodiscard]] std::size_t requiredFrameCount() const { return RequiredFrameCount; } // TODO: make field public?

    template<typename Field, typename T, typename Tag>
    void setFrameData(Field field, T &&value, Tag tag) { frameAt(field) = MessageFrame(std::forward<T>(value), tag); }

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
    void                           setServiceName(T &&serviceName, Tag tag) { setFrameData(Frame::ServiceName, std::forward<T>(serviceName), tag); }
    [[nodiscard]] std::string_view serviceName() const { return frameAt(Frame::ServiceName).data(); }

    template<typename T, typename Tag>
    void                           setClientSourceId(T &&clientSourceId, Tag tag) { setFrameData(Frame::ClientSourceId, std::forward<T>(clientSourceId), tag); }
    [[nodiscard]] std::string_view clientSourceId() const { return frameAt(Frame::ClientSourceId).data(); }

    template<typename T, typename Tag>
    void                           setClientRequestId(T &&clientRequestId, Tag tag) { setFrameData(Frame::ClientRequestId, std::forward<T>(clientRequestId), tag); }
    [[nodiscard]] std::string_view clientRequestId() const { return frameAt(Frame::ClientRequestId).data(); }

    template<typename T, typename Tag>
    void                           setTopic(T &&topic, Tag tag) { setFrameData(Frame::Topic, std::forward<T>(topic), tag); }
    [[nodiscard]] std::string_view topic() const { return frameAt(Frame::Topic).data(); }

    template<typename T, typename Tag>
    void                           setBody(T &&body, Tag tag) { setFrameData(Frame::Body, std::forward<T>(body), tag); }
    [[nodiscard]] std::string_view body() const { return frameAt(Frame::Body).data(); }

    template<typename T, typename Tag>
    void setError(T &&error, Tag tag) {
        setFrameData(Frame::Error, std::forward<T>(error), tag);
    }
    [[nodiscard]] std::string_view error() const { return frameAt(Frame::Error).data(); }

    template<typename T, typename Tag>
    void                           setRbacToken(T &&rbac, Tag tag) { setFrameData(Frame::RBAC, std::forward<T>(rbac), tag); }
    [[nodiscard]] std::string_view rbacToken() const { return frameAt(Frame::RBAC).data(); }
};

using MdpMessage = BasicMdpMessage<MessageFormat::WithoutSourceId>;

static_assert(std::is_nothrow_move_constructible_v<MdpMessage>, "MdpMessage should be noexcept MoveConstructible");
static_assert(std::is_nothrow_move_constructible_v<MdpMessage>, "MdpMessage should be noexcept MoveConstructible");
} // namespace opencmw::majordomo

#endif
