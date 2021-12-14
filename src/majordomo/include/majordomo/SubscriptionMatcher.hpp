#ifndef OPENCMW_MAJORDOMO_SUBSCRIPTIONMATCHER_H
#define OPENCMW_MAJORDOMO_SUBSCRIPTIONMATCHER_H

#include <majordomo/Debug.hpp>

#include <URI.hpp>

#include <concepts>

namespace opencmw {

class SubscriptionMatcher {
public:
    using URI = const opencmw::URI<RELAXED>; // relaxed because we need "*"

    bool operator()(const URI &notified, const URI &subscriber) const noexcept {
        if (!testPathOnly(notified, subscriber)) {
            return false;
        }

        return true;
    }

private:
    bool testPathOnly(const URI &notified, const URI &subscriber) const noexcept {
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

} // namespace opencmw

#endif
