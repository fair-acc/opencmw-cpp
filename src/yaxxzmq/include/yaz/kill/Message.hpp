#ifndef YAZ_MESSAGE_H
#define YAZ_MESSAGE_H

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <zmq.h>

#include "../Result.hpp"

// These classes are temporary for demonstration purposes only

namespace yaz {

using Bytes = std::string;
using Byte  = std::string::value_type;

// TODO: Allocators
class MessagePart {
private:
    bool _owning = true;

    // Marked as mutable as this is our bridge to C API
    // that doesn't care about constness
    mutable zmq_msg_t _message{};

public:
    struct static_bytes_tag {};
    struct dynamic_bytes_tag {};

    MessagePart() {
        zmq_msg_init(&_message);
    }

    explicit MessagePart(Bytes *buf, dynamic_bytes_tag /*tag*/ = {}) {
        zmq_msg_init_data(
                &_message, buf->data(), buf->size(),
                [](void *, void *buf_owned) {
                    delete static_cast<Bytes *>(buf_owned);
                },
                buf);
    }

    explicit MessagePart(std::string_view buf, static_bytes_tag) {
        zmq_msg_init_data(
                &_message, const_cast<char *>(buf.data()), buf.size(),
                [](void *, void *) {
                    // We don't want to delete a static RO string literal
                },
                nullptr);
    }

    ~MessagePart() {
        if (_owning) {
            zmq_msg_close(&_message);
        }
    }

    MessagePart(const MessagePart &other) = delete;
    MessagePart &operator=(const MessagePart &other) = delete;
    MessagePart(MessagePart &&other)
        : _owning(other._owning) {
        zmq_msg_init(&_message);
        zmq_msg_move(&_message, &other._message);

        other._owning = false;
        zmq_msg_close(&other._message);
    }
    MessagePart &operator=(MessagePart &&other) {
        auto temp = std::move(other);
        swap(other);
        return *this;
    }

    void swap(MessagePart &other) {
        std::swap(_owning, other._owning);
        zmq_msg_t temp;
        zmq_msg_init(&temp);
        zmq_msg_move(&temp, &_message);
        zmq_msg_move(&_message, &other._message);
        zmq_msg_move(&other._message, &temp);
        zmq_msg_close(&temp);
    }

    // Reads a message from the socket
    // Returns the number of received bytes
    positive_or_errno<int> receive(void *socket, int flags) {
        // TODO: hide 0mq flags
        return positive_or_errno<int>{ zmq_msg_recv(&_message, socket, flags) };
    }

    // Sending is not const as 0mq nullifies the message
    // See: http://api.zeromq.org/3-2:zmq-msg-send
    positive_or_errno<int> send(void *socket, int flags) {
        auto result = positive_or_errno<int>{ zmq_msg_send(&_message, socket, flags) };
        if (result) {
            _owning = false;
        }
        return result;
    }

    size_t size() const {
        return zmq_msg_size(&_message);
    }

    std::string_view data() const {
        return std::string_view(
                static_cast<char *>(zmq_msg_data(&_message)), size());
    }
};

class Message {
private:
    // In order to have a zero-copy sending API, we allow API clients
    // to give us their internal data that will be freed automatically
    // when the message is sent.
    //
    // The non-nullptr items in the vector have not been sent yet
    //
    // TODO: Investigate whether we want unique_ptrs with custom
    // deleters (that is, do we want a generic smart pointer support)
    using BytesPtr = std::unique_ptr<Bytes>;
    std::vector<MessagePart> _parts;

    // Helper function to print out the current message
    // TODO: Remove as we don't want to depend on <iostream>
    friend std::ostream &operator<<(std::ostream &out, const Message &message) {
        out << '{';
        for (const auto &part : message._parts) {
            out << part.data();
        }
        out << '}';
        return out;
    }

public:
    Message() = default;
    explicit Message(BytesPtr &&part) {
        // zmq_msg_t takes ownership of the data
        _parts.emplace_back(part.release());
    }
    explicit Message(std::vector<BytesPtr> &parts) {
        _parts.reserve(parts.size());
        std::transform(
                std::make_move_iterator(parts.begin()),
                std::make_move_iterator(parts.end()),
                std::back_inserter(_parts),
                [](BytesPtr &&ptr) {
                    return MessagePart(ptr.release());
                });
    }

    ~Message()               = default;
    Message(const Message &) = delete;
    Message &operator=(const Message &) = delete;
    Message(Message &&other)            = default;
    Message &operator=(Message &&other) = default;

    // std::vector<BytesPtr> take_parts() {
    //     return std::move(_parts);
    // }

    // Adds a new part to the message
    auto &add_part(BytesPtr &&part) {
        return _parts.emplace_back(part.release());
    }

    auto &add_part() {
        return _parts.emplace_back();
    }

    const auto &operator[](std::size_t index) const {
        return _parts[index];
    }

    auto &operator[](std::size_t index) {
        return _parts[index];
    }

    [[nodiscard]] std::size_t parts_count() const {
        return _parts.size();
    }

    [[nodiscard]] auto &back() const {
        return _parts.back();
    }

    void clear() {
        _parts.clear();
    }

    void resize(std::size_t size) {
        _parts.resize(size);
    }
};

} // namespace yaz

#endif // include guard
