#ifndef OPENCMW_ZMQ_MESSAGE_HPP
#define OPENCMW_ZMQ_MESSAGE_HPP

#include <MdpMessage.hpp>

#include "Ptr.hpp"

#include <optional>

namespace opencmw::zmq {

inline Result<int> initializeSocket(const Socket &sock, const mdp::Settings &settings = {}) {
    const int heartbeatInterval = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(settings.heartbeatInterval).count());
    const int ttl               = heartbeatInterval * settings.heartbeatLiveness;
    const int hb_timeout        = heartbeatInterval * settings.heartbeatLiveness;
    return invoke(zmq_setsockopt, sock, ZMQ_SNDHWM, &settings.highWaterMark, sizeof(settings.highWaterMark))
        && invoke(zmq_setsockopt, sock, ZMQ_RCVHWM, &settings.highWaterMark, sizeof(settings.highWaterMark))
        && invoke(zmq_setsockopt, sock, ZMQ_HEARTBEAT_TTL, &ttl, sizeof(ttl))
        && invoke(zmq_setsockopt, sock, ZMQ_HEARTBEAT_TIMEOUT, &hb_timeout, sizeof(hb_timeout))
        && invoke(zmq_setsockopt, sock, ZMQ_HEARTBEAT_IVL, &heartbeatInterval, sizeof(heartbeatInterval))
        && invoke(zmq_setsockopt, sock, ZMQ_LINGER, &heartbeatInterval, sizeof(heartbeatInterval));
}

class MessageFrame {
private:
    bool _owning = true;

    // mutable as 0mq API knows no const
    mutable zmq_msg_t _message;

public:
    MessageFrame()
        : _message() { zmq_msg_init(&_message); }

    explicit MessageFrame(IoBuffer &&buf) {
        auto copy = new IoBuffer(std::move(buf));
        zmq_msg_init_data(
                &_message, copy->data(), copy->size(),
                [](void * /*unused*/, void *bufOwned) {
                    delete static_cast<IoBuffer *>(bufOwned);
                },
                copy);
    }

    explicit MessageFrame(std::string &&buf) {
        auto copy = new std::string(std::move(buf));
        zmq_msg_init_data(
                &_message, copy->data(), copy->size(),
                [](void * /*unused*/, void *bufOwned) {
                    delete static_cast<std::string *>(bufOwned);
                },
                copy);
    }

    static MessageFrame fromStaticData(std::string_view buf) {
        MessageFrame mf;
        zmq_msg_init_data(
                &mf._message, const_cast<char *>(buf.data()), buf.size(),
                [](void *, void *) {
                    // We don't want to delete a static RO string literal
                },
                nullptr);
        return mf;
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

    void swap(MessageFrame &other) {
        std::swap(_owning, other._owning);
        std::swap(_message, other._message);
    }

    // Reads a message from the socket
    // Returns the number of received bytes
    Result<int> receive(const Socket &socket, int flags) {
        auto result = zmq::invoke(zmq_msg_recv, &_message, socket, flags);
        _owning     = result.isValid();
        return result;
    }

    // Sending is not const as 0mq nullifies the message
    // See: http://api.zeromq.org/3-2:zmq-msg-send
    [[nodiscard]] auto send(const Socket &socket, int flags) {
        auto result = zmq::invoke(zmq_msg_send, &_message, socket, flags);
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

template<mdp::MessageFormat Format>
struct ZmqMessage {
    static constexpr std::size_t RequiredFrameCount = Format == mdp::MessageFormat::WithSourceId ? 9 : 8;

    enum class Frame : std::size_t {
        SourceId = 0,
        Protocol = Format == mdp::MessageFormat::WithSourceId ? 1 : 0,
        Command,
        ServiceName,
        ClientRequestId,
        Topic,
        Body,
        Error,
        RBAC
    };

    std::array<MessageFrame, RequiredFrameCount> frames;

    [[nodiscard]] auto                           sendFrame(const Socket &socket, std::size_t index, int flags) {
        assert(flags & ZMQ_DONTWAIT);
        while (true) { // TODO -Q: could become a infinite busy-loop?!?
            const auto result = frames[index].send(socket, flags);
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
        for (std::size_t i = 0; i < frames.size(); ++i) {
            result = sendFrame(socket, i);
            if (!result) {
                return result;
            }
        }

        return result;
    }

    MessageFrame &frame(Frame frame) {
        return frames[static_cast<std::size_t>(frame)];
    }
};

template<mdp::MessageFormat Format>
[[nodiscard]] inline auto send(mdp::BasicMessage<Format> &&message, const Socket &socket) {
    using namespace std::literals;

    ZmqMessage<Format> zmsg;
    if constexpr (Format == mdp::MessageFormat::WithSourceId) {
        zmsg.frame(ZmqMessage<Format>::Frame::SourceId) = MessageFrame{ std::move(message.sourceId) };
    }

    // use sv to avoid "\x0" being truncated
    static constexpr auto commandStrings = std::array{
        "\x0"sv, "\x1"sv, "\x2"sv, "\x3"sv, "\x4"sv, "\x5"sv, "\x6"sv, "\x7"sv, "\x8"sv, "\x9"sv, "\xa"sv
    };

    zmsg.frame(ZmqMessage<Format>::Frame::Protocol)        = MessageFrame{ std::move(message.protocolName) };
    zmsg.frame(ZmqMessage<Format>::Frame::Command)         = MessageFrame::fromStaticData(commandStrings[static_cast<std::size_t>(message.command)]);
    zmsg.frame(ZmqMessage<Format>::Frame::ServiceName)     = MessageFrame{ std::move(message.serviceName) };
    zmsg.frame(ZmqMessage<Format>::Frame::ClientRequestId) = MessageFrame{ std::move(message.clientRequestID) };
    zmsg.frame(ZmqMessage<Format>::Frame::Topic)           = MessageFrame{ std::string(message.endpoint.str()) };
    zmsg.frame(ZmqMessage<Format>::Frame::Body)            = MessageFrame{ std::move(message.data) };
    zmsg.frame(ZmqMessage<Format>::Frame::Error)           = MessageFrame{ std::move(message.error) };
    zmsg.frame(ZmqMessage<Format>::Frame::RBAC)            = MessageFrame{ std::move(message.rbac) };

    return zmsg.send(socket);
}

template<mdp::MessageFormat Format>
[[nodiscard]] inline std::optional<mdp::BasicMessage<Format>> receive(const Socket &socket) {
    std::size_t        framesReceived = 0;

    ZmqMessage<Format> zmsg;

    while (true) {
        MessageFrame frame;
        const auto   byteCountResult = frame.receive(socket, ZMQ_DONTWAIT);

        if (byteCountResult) {
            if (framesReceived < zmsg.frames.size()) {
                zmsg.frames[framesReceived] = std::move(frame);
            }
            ++framesReceived;
        } else {
            return {};
        }

        int64_t more;
        size_t  moreSize = sizeof(more);
        if (!zmq::invoke(zmq_getsockopt, socket, ZMQ_RCVMORE, &more, &moreSize)) {
            // Can not check rcvmore
            return {};

        } else if (more != 0) {
            // Multi-part message
            continue;

        } else {
            break;
        }
    }

    if (framesReceived != ZmqMessage<Format>::RequiredFrameCount) {
        return {};
    }

    const auto clientRequestId = zmsg.frame(ZmqMessage<Format>::Frame::ClientRequestId).data();
    const auto data            = zmsg.frame(ZmqMessage<Format>::Frame::Body).data();
    const auto rbac            = zmsg.frame(ZmqMessage<Format>::Frame::RBAC).data();
    const auto commandStr      = zmsg.frame(ZmqMessage<Format>::Frame::Command).data();

    if (commandStr.length() != 1 || static_cast<unsigned char>(commandStr[0]) > static_cast<unsigned char>(mdp::Command::Heartbeat)) {
        return {};
    }

    mdp::BasicMessage<Format> msg;
    if constexpr (Format == mdp::MessageFormat::WithSourceId) {
        msg.sourceId = std::string(zmsg.frame(ZmqMessage<Format>::Frame::SourceId).data());
    }
    msg.protocolName    = std::string(zmsg.frame(ZmqMessage<Format>::Frame::Protocol).data());
    msg.command         = static_cast<mdp::Command>(commandStr[0]);
    msg.serviceName     = std::string(zmsg.frame(ZmqMessage<Format>::Frame::ServiceName).data());
    msg.clientRequestID = IoBuffer(clientRequestId.data(), clientRequestId.size());
    msg.endpoint        = mdp::Message::URI(std::string(zmsg.frame(ZmqMessage<Format>::Frame::Topic).data()));
    msg.data            = IoBuffer(data.data(), data.size());
    msg.error           = std::string(zmsg.frame(ZmqMessage<Format>::Frame::Error).data());
    msg.rbac            = IoBuffer(rbac.data(), rbac.size());

    return msg;
}
} // namespace opencmw::zmq

#endif
