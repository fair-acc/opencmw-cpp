#ifndef OPENCMW_MAJORDOMO_SUBSCRIPTIONMATCHER_H
#define OPENCMW_MAJORDOMO_SUBSCRIPTIONMATCHER_H

#include <TimingCtx.hpp>
#include <URI.hpp>

#include <concepts>
#include <memory>
#include <optional>

namespace opencmw::majordomo {

class AbstractFilter {
public:
    virtual ~AbstractFilter()                                                             = default;
    virtual bool operator()(std::string_view notified, std::string_view subscribed) const = 0;
};

class SubscriptionMatcher {
private:
    std::unordered_map<std::string, std::unique_ptr<AbstractFilter>> _filters;

public:
    using URI = const opencmw::URI<RELAXED>; // relaxed because we need "*"

    template<typename Filter>
    void addFilter(const std::string &key) {
        _filters.emplace(key, std::make_unique<Filter>());
    }

    bool operator()(const URI &notified, const URI &subscriber) const noexcept {
        if (!testPathOnly(notified, subscriber)) {
            return false;
        }

        if (!testQueries(notified, subscriber)) {
            return false;
        }

        return true;
    }

private:
    bool testQueries(const URI &notified, const URI &subscriber) const noexcept {
        const auto subscriberQuery = subscriber.queryParamMap();
        if (subscriberQuery.empty()) {
            return true;
        }

        const auto notificationQuery = notified.queryParamMap();

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

    bool testPathOnly(const URI &notified, const URI &subscriber) const {
        const auto pathNotification = notified.path();
        const auto pathSubscriber   = subscriber.path();

        // assert: "" is always returned as nullopt
        assert(!pathNotification || !pathNotification->empty());
        assert(!pathSubscriber || !pathSubscriber->empty());

        if (!pathSubscriber || std::all_of(pathSubscriber->begin(), pathSubscriber->end(), [](char c) { return std::isblank(c); })) {
            return true;
        }

        if (pathSubscriber->find('*') == std::string_view::npos) {
            return pathNotification == pathSubscriber;
        }

        auto pathSubscriberView = std::string_view(*pathSubscriber);

        if (pathSubscriberView.ends_with("*")) {
            pathSubscriberView.remove_suffix(1);
        }

        // match path (leading characters) only - assumes trailing asterisk
        return pathNotification->starts_with(pathSubscriberView);
    }
};

} // namespace opencmw::majordomo

#endif
