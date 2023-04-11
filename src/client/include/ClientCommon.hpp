#ifndef OPENCMW_CLIENT_COMMON_HPP
#define OPENCMW_CLIENT_COMMON_HPP

#include <chrono>
#include <functional>

#include <MdpMessage.hpp>
#include <MIME.hpp>
#include <URI.hpp>

namespace opencmw::client {

using opencmw::uri_check::STRICT;
using timePoint = std::chrono::time_point<std::chrono::system_clock>;
using namespace std::chrono_literals;

struct Request {
    URI<STRICT>                         uri;
    std::function<void(mdp::Message &)> callback;
    timePoint                           timestamp_received = std::chrono::system_clock::now();
};

struct Subscription {
    URI<STRICT>                         uri;
    std::function<void(mdp::Message &)> callback;
    timePoint                           timestamp_received = std::chrono::system_clock::now();
};

struct Command : public mdp::Message {
    std::function<void(const mdp::Message &)> callback; // callback or target ring buffer
};

namespace detail {
template<bool exactMatch, typename RequiredType, typename Item>
constexpr auto find_argument_value_helper(Item &item) {
    if constexpr (std::is_same_v<Item, RequiredType>) {
        return std::tuple<RequiredType>(item);
    } else if constexpr (std::is_convertible_v<Item, RequiredType> && !exactMatch) {
        return std::tuple<RequiredType>(RequiredType(item));
    } else {
        return std::tuple<>();
    }
}

template<bool exactMatch, typename RequiredType, typename Func, typename... Items>
requires std::is_invocable_r_v<RequiredType, Func>
constexpr RequiredType find_argument_value(Func defaultGenerator, Items... args) {
    auto ret = std::tuple_cat(find_argument_value_helper<exactMatch, RequiredType>(args)...);
    if constexpr (std::tuple_size_v<decltype(ret)> == 0) {
        return defaultGenerator();
    } else {
        return std::get<0>(ret);
    }
}

constexpr const char *ACCEPT_HEADER       = "accept";
constexpr const char *CONTENT_TYPE_HEADER = "content-type";
} // namespace detail

class DefaultContentTypeHeader {
    const MIME::MimeType _mimeType;

public:
    DefaultContentTypeHeader(const MIME::MimeType &type) noexcept
        : _mimeType(type){};
    DefaultContentTypeHeader(const std::string_view type_str) noexcept
        : _mimeType(MIME::getType(type_str)){};
    constexpr operator const MIME::MimeType() const noexcept { return _mimeType; };
};

} // namespace opencmw::client

#endif // include guard
