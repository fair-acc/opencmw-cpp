#ifndef OPENCMW_MAJORDOMO_SUBSCRIPTIONMATCHER_H
#define OPENCMW_MAJORDOMO_SUBSCRIPTIONMATCHER_H

#include <Filters.hpp>
#include <TimingCtx.hpp>
#include <Topic.hpp>

#include <memory>

namespace opencmw::majordomo {

class SubscriptionMatcher {
private:
    std::unordered_map<std::string, std::unique_ptr<opencmw::AbstractFilter>> _filters;

public:
    template<typename Filter>
    void addFilter(const std::string &key) {
        _filters.emplace(key, std::make_unique<Filter>());
    }

    bool operator()(const mdp::Topic &notified, const mdp::Topic &subscriber) const noexcept {
        return notified.service() == subscriber.service()
            && testQueries(notified, subscriber);
    }

private:
    bool testQueries(const mdp::Topic &notified, const mdp::Topic &subscriber) const noexcept {
        const auto &subscriberQuery = subscriber.params();
        if (subscriberQuery.empty()) {
            return true;
        }

        const auto &notificationQuery = notified.params();

        auto        doesSatisfy       = [this, &notificationQuery](const auto &subscriptionParam) {
            const auto &key   = subscriptionParam.first;
            const auto &value = subscriptionParam.second;

            // assert(!value || !value->empty()); // map never gives us "" (empty strings are returned as nullopt)

            auto filterIt = _filters.find(key);
            if (filterIt == _filters.end()) {
                return true;
            }

            assert(filterIt->second);

            const auto notifyIt = notificationQuery.find(key);
            if (notifyIt == notificationQuery.end()) {
                // specific/required subscription topic but not corresponding filter in notification set
                return false;
            }

            return (*filterIt->second)(notifyIt->second.value_or(""), value.value_or(""));
        };

        return std::all_of(subscriberQuery.begin(), subscriberQuery.end(), doesSatisfy);
    }
};

} // namespace opencmw::majordomo

#endif
