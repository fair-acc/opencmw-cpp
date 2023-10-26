#ifndef OPENCMW_MAJORDOMO_SUBSCRIPTIONMATCHER_H
#define OPENCMW_MAJORDOMO_SUBSCRIPTIONMATCHER_H

#include <Filters.hpp>
#include <SubscriptionTopic.hpp>
#include <TimingCtx.hpp>

#include <concepts>
#include <memory>

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

    bool operator()(const mdp::SubscriptionTopic &notified, const mdp::SubscriptionTopic &subscriber) const noexcept {
        return testPathOnly(notified.service(), subscriber.service())
            && testPathOnly(notified.path(), subscriber.path())
            && testQueries(notified, subscriber);
    }

private:
    bool testQueries(const mdp::SubscriptionTopic &notified, const mdp::SubscriptionTopic &subscriber) const noexcept {
        const auto subscriberQuery = subscriber.params();
        if (subscriberQuery.empty()) {
            return true;
        }

        const auto notificationQuery = notified.params();

        auto       doesSatisfy       = [this, &notificationQuery](const auto &subscriptionParam) {
            const auto &key   = subscriptionParam.first;
            const auto &value = subscriptionParam.second;

            // assert(!value || !value->empty()); // map never gives us "" (empty strings are returned as nullopt)

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
