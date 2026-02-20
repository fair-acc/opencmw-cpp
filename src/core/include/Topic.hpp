#ifndef OPENCMW_CORE_TOPIC_H
#define OPENCMW_CORE_TOPIC_H

#include <Formatter.hpp>
#include <TimingCtx.hpp> // hash_combine
#include <URI.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <format>

namespace opencmw::mdp {

constexpr bool isValidServiceName(std::string_view str) {
    auto is_allowed = [](char ch) {
        // std::isalnum is not constexpr
        return ch == '/' || ch == '.' || ch == '_' || (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
    };
    return str.starts_with("/") && str.size() > 1 && !str.ends_with("/") && std::ranges::all_of(str, is_allowed);
}

/**
 * A topic is the combination of a service name (mandatory) and query parameters (optional).
 *
 * Examples:
 *   - /DeviceName/Acquisition - service name only, no params
 *   - /DeviceName/Acquisition?signal=test - service name (/DeviceName/Acquisition) and query
 *   - /dashboards - service name (/dashboards)
 *   - /dashboards/dashboard1?what=header - service name (/dashboards/dashboard1) and query
 *
 * When encoded as mdp/mds or http/https URLs, the service name is the URL's path (with leading slash), with the
 * topic's query parameters being the URI's query parameters (minus e.g. REST-specific parameters like "LongPollingIdx"
 * that are not forwarded).
 *
 * Examples:
 *   - http://localhost:8080/DeviceName/Acquisition => service: /DeviceName/Acquisition
 *   - http://localhost:8080/DeviceName/Acquisition?LongPollingIdx=Next&signal=test => service: /DeviceName/Acquisition, query: signal=test
 *   - mds://localhost:12345/DeviceName/Acquisition?signal=test => service: /DeviceName/Acquisition, query: signal=test
 *   - mdp://localhost:12345/dashboards/dashboard1?what=header => serviceName: /dashboards/dashboard1, query: what=header
 *
 * Note that the whole path is considered the service name, and that there's no additional path component denoting
 * different entities, objects etc. neither for subscriptions nor GET/SET requests. Requesting specific objects like
 * in the "/dashboards/dashboard1" example are handled via the service-matching for requests in the broker, where a
 * service name "/dashboard/dashboard1" would match a worker "/dashboard", which then can extract the "dashboard1" component
 * from the topic frame (see below).
 *
 * For subscriptions, only the worker's service name is to be used, any filtering for specific messages is
 * done via the query parameters.
 *
 * On the protocol level, the topic is used in two contexts:
 *
 *  - for ZMQ PUB/SUB subscriptions: ZMQ uses a string-based subscription mechanism, where topics are simple strings
 *    (allowing trailing wildcards) that must match exactly (or via prefix, if using wildcards). For that purpose
 *    Topic::toZmqTopic() ensures that the params in e.g. /service?b&a are always ordered alphabetically by key
 *    (/service?a&b), as /service?a&b and service?b&a are supposed to be equivalent, but wouldn't match with the ZMQ
 *    mechanism.
 *  - The OpenCMW MDP topic frame (frame 5), used by the commands GET, SET, SUBSCRIBE, UNSUBSCRIBE, FINAL, PARTIAL and
 *    NOTIFY: Here the frame contains a URI (with unspecified ordering of query parameters).
 **/
struct Topic {
    using Params = std::map<std::string, std::optional<std::string>>;

private:
    std::string _service;
    Params      _params;

public:
    Topic()                                = default;
    Topic(const Topic &other)              = default;
    Topic &operator=(const Topic &)        = default;
    Topic(Topic &&) noexcept               = default;
    Topic &operator=(Topic &&) noexcept    = default;

    bool   operator==(const Topic &) const = default;

    /**
     * Parses subscription from a "service" or "service?param" string
     *
     * @param str A string where the first path segment is the service name, e.g. "/service/" or "/service?param"
     * @param params Optional query parameters, if non-empty, @p str must not contain query parameters
     */
    static Topic fromString(std::string_view str, Params params = {}) {
        return Topic(str, std::move(params));
    }

    template<uri_check CHECK>
    static Topic fromMdpTopic(const URI<CHECK> &topic) {
        const auto &queryMap = topic.queryParamMap();
        return Topic(topic.path().value_or("/"), { queryMap.begin(), queryMap.end() });
    }

    static Topic fromZmqTopic(std::string_view topic) {
        if (topic.ends_with("#")) {
            topic.remove_suffix(1);
        }
        return fromString(topic, {});
    }

    opencmw::URI<STRICT> toMdpTopic() const {
        return opencmw::URI<STRICT>::factory().path(_service).setQuery({ _params.begin(), _params.end() }).build();
    }

    std::string toZmqTopic(const bool delimitWithPound = true) const {
        using namespace std::string_literals;
        std::string zmqTopic = _service;
        if (_params.empty()) {
            if (delimitWithPound) {
                zmqTopic += "#"s;
            }
            return zmqTopic;
        }
        zmqTopic += "?"s;
        bool isFirst = true;
        // relies on std::map being sorted by key
        for (const auto &[key, value] : _params) {
            if (!isFirst) {
                zmqTopic += "&"s;
            }
            zmqTopic += key;
            if (value) {
                zmqTopic += "="s + opencmw::URI<>::encode(*value);
            }
            isFirst = false;
        }
        if (delimitWithPound) {
            zmqTopic += "#"s;
        }
        return zmqTopic;
    }

    [[nodiscard]] std::size_t hash() const noexcept {
        std::size_t seed = 0;
        opencmw::detail::hash_combine(seed, _service);
        for (const auto &[key, value] : _params) {
            opencmw::detail::hash_combine(seed, key);
            opencmw::detail::hash_combine(seed, value);
        }

        return seed;
    }

    std::string_view service() const { return _service; }
    const auto      &params() const { return _params; }

    void             addParam(std::string_view key, std::string_view value) {
        _params[std::string(key)] = std::string(value);
    }

private:
    static std::string parseService(std::string_view str) {
        if (const auto queryPos = str.find_first_of("?"); queryPos != std::string::npos) {
            str = str.substr(0, queryPos);
        }

        while (str.ends_with("/")) {
            str.remove_suffix(1);
        }

        auto r = std::string(str);

        if (!r.starts_with("/")) {
            return "/" + r;
        }

        return r;
    }

    Topic(std::string_view serviceOrServiceAndQuery, Params params)
        : _service(parseService(serviceOrServiceAndQuery))
        , _params(std::move(params)) {
        if (serviceOrServiceAndQuery.find("?") != std::string::npos) {
            if (!_params.empty()) {
                throw std::invalid_argument(std::format("Parameters are not empty ({}), and there are more in the service string ({})", _params, serviceOrServiceAndQuery));
            }
            const auto  parsed   = opencmw::URI<RELAXED>(std::string(serviceOrServiceAndQuery));
            const auto &queryMap = parsed.queryParamMap();
            _params.insert(queryMap.begin(), queryMap.end());
        }

        if (!isValidServiceName(_service)) {
            throw std::invalid_argument(std::format("Invalid service name '{}'", _service));
        }
    }
};

} // namespace opencmw::mdp

namespace std {
template<>
struct hash<opencmw::mdp::Topic> {
    std::size_t operator()(const opencmw::mdp::Topic &k) const {
        return k.hash();
    }
};

} // namespace std

#endif
