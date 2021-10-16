#ifndef YAZ_MESSAGE_H
#define YAZ_MESSAGE_H

#include <iostream>
#include <string>
#include <vector>

#include "SizedString.hpp"

namespace yaz {

using Bytes = std::string;
using Byte  = std::string::value_type;

class MessagePrivate;

class Message {
public:
    Message() = default;
    explicit Message(Bytes message)
        : _parts{ std::move(message) } {}

    ~Message()               = default;
    Message(const Message &) = delete;
    Message &operator=(const Message &) = delete;
    Message(Message &&other)            = default;
    Message &operator=(Message &&other) = default;

    // Adds a new part to the message
    void add_part(const Bytes &part) {
        _parts.emplace_back(part);
    }
    void add_part(Bytes &&part) {
        _parts.emplace_back(std::move(part));
    }
    template<std::size_t Length>
    void add_part(const SizedString<Length> &part) {
        _parts.emplace_back(static_cast<std::string_view>(part));
    }
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

    struct const_iterator {
        using difference_type   = int;
        using iterator_category = std::forward_iterator_tag;
        using pointer           = const Bytes *;
        using reference         = const Bytes &;
        using value_type        = Bytes;

        inline const_iterator(const Message &message, std::size_t index)
            : m_index(index)
            , m_message(message) {
        }

        inline reference operator*() const {
            return m_message[m_index];
        }

        inline const_iterator &operator++() {
            m_index++;
            return *this;
        }

        inline auto operator++(int) & {
            const_iterator tmp(m_message, m_index);
            m_index++;
            return tmp;
        }

        inline pointer operator->() const {
            return &m_message[m_index];
        }

        inline bool operator==(const const_iterator &other) const {
            return m_index == other.m_index;
        }

        inline bool operator!=(const const_iterator &other) const {
            return m_index != other.m_index;
        }

    private:
        std::size_t    m_index;
        const Message &m_message;
    } __attribute__((aligned(2 * sizeof(std::size_t))));

    [[nodiscard]] inline auto begin() const {
        return const_iterator(*this, 0);
    }

    [[nodiscard]] inline auto end() const {
        return const_iterator(*this, parts_count());
    }

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
};

} // namespace yaz

#endif // include guard

