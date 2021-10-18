
#ifndef YAZ_MESSAGE_PART_P_H
#define YAZ_MESSAGE_PART_P_H

#include <zmq.h>

#include "Context.hpp"
#include "Message.hpp"

namespace yaz::detail {

class MessagePart {
public:
    MessagePart() {
        zmq_msg_init(&_message);
    }

    explicit MessagePart(const Bytes &data) {
        auto *buf = new Bytes(data);
        zmq_msg_init_data(
                &_message, buf->data(), buf->size(),
                [](void *, void *buf_owned) {
                    delete static_cast<Bytes *>(buf_owned);
                },
                buf);
    }

    ~MessagePart() {
        zmq_msg_close(&_message);
    }

    MessagePart(const MessagePart &other) = delete;
    MessagePart &operator=(const MessagePart &other) = delete;
    MessagePart(MessagePart &&other)                 = delete;
    MessagePart &operator=(MessagePart &&other) = delete;

    // Reads a message from the socket
    // Returns the number of received bytes
    positive_or_errno<int> receive(void *socket, int flags) {
        // TODO: hide 0mq flags
        return positive_or_errno<int>{ zmq_msg_recv(&_message, socket, flags) };
    }

    // Sending is not const as 0mq nullifies the message
    // See: http://api.zeromq.org/3-2:zmq-msg-send
    positive_or_errno<int> send(void *socket, int flags) {
        return positive_or_errno<int>{ zmq_msg_send(&_message, socket, flags) };
    }

    size_t size() const {
        return zmq_msg_size(&_message);
    }

    auto data() const {
        return Bytes(
                static_cast<char *>(zmq_msg_data(&_message)), size());
    }

private:
    // Marked as mutable as this is our bridge to C API
    // that doesn't care about constness
    mutable zmq_msg_t _message{};
};

} // namespace yaz::detail

#endif // include guard
