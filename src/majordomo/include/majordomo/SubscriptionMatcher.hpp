#ifndef OPENCMW_MAJORDOMO_SUBSCRIPTIONMATCHER_H
#define OPENCMW_MAJORDOMO_SUBSCRIPTIONMATCHER_H

#include <Filters.hpp>
#include <TimingCtx.hpp>
#include <URI.hpp>

#include <concepts>
#include <memory>
#include <optional>

namespace opencmw::majordomo {

using namespace std::string_literals;

struct SubscriptionData {
    using map = std::unordered_map<std::string, std::optional<std::string>>;

private:
    std::string             _rawService;
    std::string             _rawPath;

    map                     _params;

    std::string        _service;
    std::string        _path;

    static std::string stripSlashes(const std::string &str, bool addLeadingSlash) {
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
    template <typename ServiceString, typename PathString>
    SubscriptionData(ServiceString&& service, PathString&& path, std::unordered_map<std::string, std::optional<std::string>> params)
        : _rawService(std::forward<ServiceString>(service))
        , _rawPath(std::forward<PathString>(path))
        , _params(std::move(params))
        , _service(stripSlashes(_rawService, false))
        , _path(stripSlashes(_rawPath, true)) {

        if (_path.find("?") != std::string::npos) {
            if (_params.size() != 0) {
                throw fmt::format("Parameters are not empty, and there are more in the path {} {}\n", _params, _path);
            }
            auto oldPath = _path;
            auto parsed = URI<RELAXED>(_path);
            _path = parsed.path().value_or("/");
            _params = parsed.queryParamMap();
        }

        auto is_char_valid = [](char c) {
            return std::isalnum(c) || c == '.';
        };

        if (!std::all_of(_service.cbegin(), _service.cbegin(), is_char_valid)) {
            // throw debug::printAndReturn("throw {}", fmt::format("Invalid service name {}\n", _service));
            throw fmt::format("Invalid service name {}\n", _service);
        }
        if (!std::all_of(_path.cbegin(), _path.cbegin(), is_char_valid)) {
            // throw debug::printAndReturn("throw {}", fmt::format("Invalid path {}\n", _path));
            throw fmt::format("Invalid path {}\n", _path);
        }
    }

    SubscriptionData(const SubscriptionData& other)
        : SubscriptionData(other._rawService, other._rawPath, other._params)
    {}

    SubscriptionData(SubscriptionData&& other)
        : SubscriptionData(std::move(other._rawService), std::move(other._rawPath), std::move(other._params))
    {}

    SubscriptionData& operator=(SubscriptionData other) = delete;

    bool operator==(const SubscriptionData& other) const {
        return std::tie(_service, _path, _params) == std::tie(other._service, other._path, other._params);
    }

    bool operator!=(const SubscriptionData& other) const {
        return !operator==(other);
    }

    static SubscriptionData fromURI(const URI<RELAXED> &uri) {
        return SubscriptionData(""s, uri.path().value_or(""s), uri.queryParamMap());
    }

    std::string serialized() const {
        // return URI<RELAXED>::factory().path("/"s + std::string(_service) + "/"s + std::string(_path)).setQuery(_params).build().str();
        return URI<RELAXED>::factory().path(std::string(_path)).setQuery(_params).build().str();
    }

    [[nodiscard]] std::size_t hash() const noexcept {
        std::size_t seed = 0;
        opencmw::detail::hash_combine(seed, _service);
        opencmw::detail::hash_combine(seed, _path);
        for (const auto &paramPair : _params) {
            opencmw::detail::hash_combine(seed, paramPair.first);
            opencmw::detail::hash_combine(seed, paramPair.second);
        }

        return seed;
    }

    std::string_view path() const { return _path; }
    std::string_view service() const { return _service; }
    const auto      &params() const { return _params; }
};

} // namespace opencmw::majordomo

template<>
struct fmt::formatter<opencmw::majordomo::SubscriptionData> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const opencmw::majordomo::SubscriptionData &v, FormatContext &ctx) const {
        return fmt::format_to(ctx.out(), "[service:{}, path:{}, params:{}]", v.service(), v.path(), v.params());
    }
};

namespace std {
template<>
struct hash<opencmw::majordomo::SubscriptionData> {
    std::size_t operator()(const opencmw::majordomo::SubscriptionData &k) const {
        return k.hash();
    }
};

} // namespace std

namespace opencmw::majordomo {

class SubscriptionMatcher {
private:
    std::unordered_map<std::string, std::unique_ptr<opencmw::AbstractFilter>> _filters;

public:
    using URI = const opencmw::URI<RELAXED>; // relaxed because we need "*"

    template<typename Filter>
    void addFilter(const std::string &key) {
        _filters.emplace(key, std::make_unique<Filter>());
    }

    bool operator()(const SubscriptionData &notified, const SubscriptionData &subscriber) const noexcept {
        return testPathOnly(notified.service(), subscriber.service())
            && testPathOnly(notified.path(), subscriber.path())
            && testQueries(notified, subscriber);
    }

private:
    bool testQueries(const SubscriptionData &notified, const SubscriptionData &subscriber) const noexcept {
        const auto subscriberQuery = subscriber.params();
        if (subscriberQuery.empty()) {
            return true;
        }

        const auto notificationQuery = notified.params();

        auto       doesSatisfy       = [this, &notificationQuery](const auto &subscriptionParam) {
            const auto &key   = subscriptionParam.first;
            const auto &value = subscriptionParam.second;

            assert(!value || !value->empty()); // map never gives us "" (empty strings are returned as nullopt)

            auto filterIt = _filters.find(key);
            if (filterIt == _filters.end()) {
                return true;
            }

            assert(filterIt->second);

            const auto notifyIt = notificationQuery.find(key);
            if (notifyIt == notificationQuery.end() && value) {
                // specific/required subscription topic but not corresponding filter in notification set
                return false;
            }

            return (*filterIt->second)(notifyIt->second.value_or(""), value.value_or(""));
        };

        return std::all_of(subscriberQuery.begin(), subscriberQuery.end(), doesSatisfy);
    }

    bool testPathOnly(const std::string_view &notified, const std::string_view &subscriber) const {
        if (subscriber.empty() || std::all_of(subscriber.begin(), subscriber.end(), [](char c) { return std::isblank(c); })) {
            return true;
        }

        if (subscriber.find('*') == std::string_view::npos) {
            return notified == subscriber;
        }

        auto pathSubscriberView = std::string_view(subscriber);

        if (pathSubscriberView.ends_with("*")) {
            pathSubscriberView.remove_suffix(1);
        }

        // match path (leading characters) only - assumes trailing asterisk
        return notified.starts_with(pathSubscriberView);
    }
};

} // namespace opencmw::majordomo

#endif
