#ifndef YAZ_MESSAGE_H
#define YAZ_MESSAGE_H

#include <iostream>
#include <string>
#include <vector>

// These classes are temporary for demonstration purposes only

namespace yaz {

using Bytes = std::string;
using Byte  = std::string::value_type;

namespace detail {

class MessagePart {
private:
    // Marked as mutable as this is our bridge to C API
    // that doesn't care about constness
    mutable zmq_msg_t _message{};

public:
    MessagePart() {
        zmq_msg_init(&_message);
    }

    explicit MessagePart(Bytes &&data) {
        auto *buf = new Bytes(std::move(data));
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
};

} // namespace detail

class Message {
private:
    std::vector<Bytes>   _parts;

    friend std::ostream &operator<<(std::ostream &out, const Message &message) {
        out << '{';
        for (const auto &part : message._parts) {
            out << part;
        }
        out << '}';
        return out;
    }

public:
    Message() = default;
    explicit Message(Bytes message) {
        _parts.emplace_back(std::move(message));
    }
    explicit Message(std::vector<Bytes> &&parts)
        : _parts{std::move(parts)} {}

    ~Message()               = default;
    Message(const Message &) = delete;
    Message &operator=(const Message &) = delete;
    Message(Message &&other)            = default;
    Message &operator=(Message &&other) = default;

    std::vector<Bytes> take_parts() {
        return std::move(_parts);
    }

    // Adds a new part to the message
    void add_part(const Bytes &part) {
        _parts.emplace_back(part);
    }
    void add_part(Bytes &&part) {
        _parts.emplace_back(std::move(part));
    }

    // template<std::size_t Length>
    // void add_part(const SizedString<Length> &part) {
    //     _parts.emplace_back(static_cast<std::string_view>(part));
    // }

    void add_part(std::initializer_list<char> part) {
        _parts.emplace_back(part);
    }

    const Bytes &operator[](std::size_t index) const {
        return _parts[index];
    }

    [[nodiscard]] std::size_t parts_count() const {
        return _parts.size();
    }

    void clear() {
        _parts.clear();
    }
};

} // namespace yaz

#endif // include guard
