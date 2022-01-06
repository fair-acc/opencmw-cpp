#ifndef OPENCMW_MAJORDOMO_BROKER_IMPL_H
#define OPENCMW_MAJORDOMO_BROKER_IMPL_H

#include <majordomo/Message.hpp>

#include <URI.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <set>
#include <string>

namespace opencmw::majordomo::detail {

using BrokerMessage = BasicMdpMessage<MessageFormat::WithSourceId>;

struct DnsServiceItem {
    std::string                                        address;
    std::string                                        serviceName;
    std::set<URI<RELAXED>>                             uris;
    std::chrono::time_point<std::chrono::steady_clock> expiry;

    explicit DnsServiceItem(std::string address_, std::string serviceName_)
        : address{ std::move(address_) }
        , serviceName{ std::move(serviceName_) } {}
};

struct InternalService {
    virtual ~InternalService()                                    = default;
    virtual BrokerMessage processRequest(BrokerMessage &&request) = 0;
};

template <typename BrokerType>
struct MmiEcho : public InternalService {
    explicit MmiEcho(BrokerType *) {}
    BrokerMessage processRequest(BrokerMessage &&message) override { return message; }
};

template <typename BrokerType>
struct MmiService : public InternalService {
    BrokerType *const parent;

    explicit MmiService(BrokerType *parent_)
        : parent(parent_) {}

    BrokerMessage processRequest(BrokerMessage &&message) override {
        message.setCommand(Command::Final);
        if (message.body().empty()) {
            const auto keyView = std::views::keys(parent->_services);
            auto keys = std::vector<std::string>(keyView.begin(), keyView.end());
            std::ranges::sort(keys);

            message.setBody(fmt::format("{}", fmt::join(keys, ",")), MessageFrame::dynamic_bytes_tag{});
            return message;
        }

        const auto exists = parent->_services.contains(std::string(message.body()));
        message.setBody(exists ? "200" : "404", MessageFrame::static_bytes_tag{});
        return message;
    }
};

template <typename BrokerType>
struct MmiOpenApi : public InternalService {
    BrokerType *const parent;

    explicit MmiOpenApi(BrokerType *parent_)
        : parent(parent_) {}

    BrokerMessage processRequest(BrokerMessage &&message) override {
        message.setCommand(Command::Final);
        const auto serviceName = std::string(message.body());
        const auto serviceIt = parent->_services.find(serviceName);
        if (serviceIt != parent->_services.end()) {
            message.setBody(serviceIt->second.description, MessageFrame::dynamic_bytes_tag{});
            message.setError("", MessageFrame::static_bytes_tag{});
        } else {
            message.setBody("", MessageFrame::static_bytes_tag{});
            message.setError(fmt::format("Requested invalid service '{}'", serviceName), MessageFrame::dynamic_bytes_tag{});
        }
        return message;
    }
};

inline constexpr std::string_view trimmed(std::string_view s) {
    using namespace std::literals;
    constexpr auto whitespace = " \x0c\x0a\x0d\x09\x0b"sv;
    const auto first = s.find_first_not_of(whitespace);
    const auto prefixLength = first != std::string_view::npos ? first : s.size();
    s.remove_prefix(prefixLength);
    if (s.empty()) {
        return s;
    }
    const auto last = s.find_last_not_of(whitespace);
    s.remove_suffix(s.size() - 1 - last);
    return s;
}

inline std::vector<std::string_view> split(std::string_view s, std::string_view delim) {
    std::vector<std::string_view> segments;
    while (true) {
        const auto pos = s.find(delim);
        if (pos == std::string_view::npos) {
            segments.push_back(s);
            return segments;
        }

        segments.push_back(s.substr(0, pos));
        s.remove_prefix(pos + 1);
    }
}

inline std::string_view stripStart(std::string_view s, std::string_view stripChars) {
    const auto pos = s.find_first_not_of(stripChars);
    if (pos == std::string_view::npos) {
        return {};
    }

    s.remove_prefix(pos);
    return s;
}

template<typename Left, typename Right>
inline bool iequal(const Left &left, const Right &right) noexcept {
    return std::equal(std::cbegin(left), std::cend(left), std::cbegin(right), std::cend(right),
            [](auto l, auto r) { return std::tolower(l) == std::tolower(r); });
}

inline std::string uriAsString(const URI<RELAXED> &uri) {
    return uri.str;
}

template <typename BrokerType>
struct MmiDns : public InternalService {
    BrokerType *const parent;

    explicit MmiDns(BrokerType *parent_)
        : parent(parent_) {}

    BrokerMessage processRequest(BrokerMessage &&message) override {
        using namespace std::literals;
        message.setCommand(Command::Final);

        std::string reply;
        if (message.body().empty() || message.body().find_first_of(",:/") == std::string_view::npos) {
            const auto uris = std::views::values(parent->_dnsCache);
            reply = fmt::format("{}", fmt::join(uris, ","));
        } else {
            // TODO std::views::split seems to have issues in GCC 11, maybe switch to views::split/transform
            // once it works with our then supported compilers
            const auto body = message.body();
            auto segments = split(body, ","sv);
            std::vector<std::string> results(segments.size());
            std::transform(segments.begin(), segments.end(), results.begin(), [this](const auto &v) {
                return findDnsEntry(trimmed(v));
            });

            reply = fmt::format("{}", fmt::join(results, ","));
        }

        message.setBody(reply, MessageFrame::dynamic_bytes_tag{});
        return message;
    }

    std::string findDnsEntry(std::string_view s) {
        const auto query = URI<RELAXED>(std::string(s));

        const auto queryScheme = query.scheme();
        const auto queryPath = query.path().value_or("");
        const auto strippedQueryPath = stripStart(queryPath, "/");
        const auto stripStartFromSearchPath = strippedQueryPath.starts_with("mmi.") ? fmt::format("/{}", parent->brokerName) : "/"; // crop initial broker name for broker-specific MMI services

        const auto entryMatches = [&queryScheme, &strippedQueryPath, &stripStartFromSearchPath](const auto &dnsEntry) {
            if (queryScheme && !iequal(dnsEntry.scheme().value_or(""), *queryScheme)) {
                return false;
            }

            const auto entryPath = dnsEntry.path().value_or("");
            return stripStart(entryPath, stripStartFromSearchPath).starts_with(strippedQueryPath);
        };

        std::string result;

        for (const auto &cacheEntry : parent->_dnsCache) {
            using namespace std::views;
            auto matching = cacheEntry.second.uris | filter(entryMatches) | transform(uriAsString);
            if (!matching.empty()) {
                result += fmt::format("[{}: {}]", s, fmt::join(matching, ", "));
            }
        }

        return !result.empty() ? result : fmt::format("[{}: null]", s);
    }
};

} // namespace opencmw::majordomo::detail

template<>
struct fmt::formatter<opencmw::majordomo::detail::DnsServiceItem> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const opencmw::majordomo::detail::DnsServiceItem &v, FormatContext &ctx) {
        return fmt::format_to(ctx.out(), "[{}: {}]", v.serviceName, fmt::join(v.uris | std::views::transform(opencmw::majordomo::detail::uriAsString), ","));
    }
};

#endif
