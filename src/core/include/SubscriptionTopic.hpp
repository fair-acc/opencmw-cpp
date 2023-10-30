#ifndef OPENCMW_CORE_SUBSCRIPTIONTOPIC_H
#define OPENCMW_CORE_SUBSCRIPTIONTOPIC_H

#include <TimingCtx.hpp> // hash_combine
#include <URI.hpp>

#include <optional>
#include <string>
#include <unordered_map>

#include <fmt/format.h>

namespace opencmw::mdp {

using namespace std::string_literals;

//
// Using strings (even URI-formatted) opens up users to
// create invalid service-topic-parameters combinations.
// SubscriptionTopic serves to make sure the information
// about subscription is passable in a unified manner
// (serializable and deserializable to a string/URI).
//
struct SubscriptionTopic {
    using map = std::unordered_map<std::string, std::optional<std::string>>;

private:
    std::string        _service;
    std::string        _path;
    map                _params;

    static std::string stripSlashes(std::string_view str, bool addLeadingSlash) {
        auto firstNonSlash = str.find_first_not_of("/ ");
        if (firstNonSlash == std::string::npos) {
            return "";
        }

        auto lastNonSlash = str.find_last_not_of("/ ");

        if (addLeadingSlash) {
            return "/"s + std::string(str.data() + firstNonSlash, lastNonSlash - firstNonSlash + 1);
        } else {
            return std::string(str.data() + firstNonSlash, lastNonSlash - firstNonSlash + 1);
        }
    }

public:
    SubscriptionTopic() = default;

    template<typename PathString>
        requires(!std::same_as<SubscriptionTopic, std::remove_cvref_t<PathString>>)
    explicit SubscriptionTopic(PathString &&path)
        : SubscriptionTopic(""s, std::forward<PathString>(path), {}) {}

    SubscriptionTopic(const SubscriptionTopic &other)               = default;
    SubscriptionTopic &operator=(const SubscriptionTopic &)         = default;
    SubscriptionTopic(SubscriptionTopic &&) noexcept                = default;
    SubscriptionTopic &operator=(SubscriptionTopic &&) noexcept     = default;

    auto               operator<=>(const SubscriptionTopic &) const = default;

    template<typename TURI>
    static SubscriptionTopic fromURI(const TURI &uri) {
        return SubscriptionTopic(""s, uri.path().value_or(""s), uri.queryParamMap());
    }

    template<typename TURI, typename ServiceString>
    static SubscriptionTopic fromURIAndServiceName(const TURI &uri, ServiceString &&serviceName) {
        return SubscriptionTopic(std::forward<ServiceString>(serviceName), uri.path().value_or(""s), uri.queryParamMap());
    }

    opencmw::URI<STRICT> toEndpoint() const {
        return opencmw::URI<STRICT>::factory().path(_path).setQuery(_params).build();
    }

    bool empty() const {
        return _service.empty() && _path.empty() && _params.empty();
    }

    std::string toZmqTopic() const {
        std::string topic = _path;
        if (_params.empty()) {
            return topic;
        }
        topic += "?"s;
        bool isFirst = true;
        // sort params
        for (const auto &[key, value] : std::map{ _params.begin(), _params.end() }) {
            if (!isFirst) {
                topic += "&"s;
            }
            topic += key;
            if (value) {
                topic += "="s + opencmw::URI<>::encode(*value);
            }
            isFirst = false;
        }
        return topic;
    }

    [[nodiscard]] std::size_t hash() const noexcept {
        std::size_t seed = 0;
        opencmw::detail::hash_combine(seed, _service);
        opencmw::detail::hash_combine(seed, _path);
        for (const auto &[key, value] : _params) {
            opencmw::detail::hash_combine(seed, key);
            opencmw::detail::hash_combine(seed, value);
        }

        return seed;
    }

    std::string_view path() const { return _path; }
    std::string_view service() const { return _service; }
    const auto      &params() const { return _params; }

private:
    template<typename ServiceString, typename PathString>
    SubscriptionTopic(ServiceString &&service, PathString &&path, std::unordered_map<std::string, std::optional<std::string>> params)
        : _service(stripSlashes(std::forward<ServiceString>(service), false))
        , _path(stripSlashes(std::forward<PathString>(path), true))
        , _params(std::move(params)) {
        if (_path.find("?") != std::string::npos) {
            if (_params.size() != 0) {
                throw fmt::format("Parameters are not empty, and there are more in the path {} {}\n", _params, _path);
            }
            auto parsed = opencmw::URI<RELAXED>(_path);
            _path       = parsed.path().value_or("/");
            _params     = parsed.queryParamMap();
        }

        auto is_char_valid = [](char c) {
            return std::isalnum(c) || c == '.';
        };

        if (!std::all_of(_service.cbegin(), _service.cbegin(), is_char_valid)) {
            throw fmt::format("Invalid service name {}\n", _service);
        }
        if (!std::all_of(_path.cbegin(), _path.cbegin(), is_char_valid)) {
            throw fmt::format("Invalid path {}\n", _path);
        }
    }
};

} // namespace opencmw::mdp

template<>
struct fmt::formatter<opencmw::mdp::SubscriptionTopic> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const opencmw::mdp::SubscriptionTopic &v, FormatContext &ctx) const {
        return fmt::format_to(ctx.out(), "[service:{}, path:{}, params:{}]", v.service(), v.path(), v.params());
    }
};

namespace std {
template<>
struct hash<opencmw::mdp::SubscriptionTopic> {
    std::size_t operator()(const opencmw::mdp::SubscriptionTopic &k) const {
        return k.hash();
    }
};

} // namespace std

#endif
