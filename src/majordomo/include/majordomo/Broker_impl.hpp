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

inline constexpr std::string_view trimmed(std::string_view s) {
    using namespace std::literals;
    constexpr auto whitespace   = " \x0c\x0a\x0d\x09\x0b"sv;
    const auto     first        = s.find_first_not_of(whitespace);
    const auto     prefixLength = first != std::string_view::npos ? first : s.size();
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

inline std::string findDnsEntry(std::string_view brokerName, std::unordered_map<std::string, detail::DnsServiceItem> &dnsCache, std::string_view s) {
    const auto query                    = URI<RELAXED>(std::string(s));

    const auto queryScheme              = query.scheme();
    const auto queryPath                = query.path().value_or("");
    const auto strippedQueryPath        = stripStart(queryPath, "/");
    const auto stripStartFromSearchPath = strippedQueryPath.starts_with("mmi.") ? fmt::format("/{}", brokerName) : "/"; // crop initial broker name for broker-specific MMI services

    const auto entryMatches             = [&queryScheme, &strippedQueryPath, &stripStartFromSearchPath](const auto &dnsEntry) {
        if (queryScheme && !iequal(dnsEntry.scheme().value_or(""), *queryScheme)) {
            return false;
        }

        const auto entryPath = dnsEntry.path().value_or("");
        return stripStart(entryPath, stripStartFromSearchPath).starts_with(strippedQueryPath);
    };

    std::string result;

    for (const auto &cacheEntry : dnsCache) {
        using namespace std::views;
        auto matching = cacheEntry.second.uris | filter(entryMatches) | transform(uriAsString);
        if (!matching.empty()) {
            result += fmt::format("[{}: {}]", s, fmt::join(matching, ", "));
        }
    }

    return !result.empty() ? result : fmt::format("[{}: null]", s);
}

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
