#ifndef OPENCMW_MAJORDOMO_FILTERS_H
#define OPENCMW_MAJORDOMO_FILTERS_H

#include <opencmw.hpp>
#include <TimingCtx.hpp>

#include <majordomo/SubscriptionMatcher.hpp>

#include <charconv>
#include <concepts>
#include <string_view>

namespace opencmw::majordomo {

template<typename T>
concept DomainObject = std::is_constructible_v<T, std::string_view>;

template<DomainObject T, typename MatchFunctor = std::equal_to<>>
class DomainFilter : public AbstractFilter {
    static_assert(std::equality_comparable<T> || !std::is_same_v<MatchFunctor, void>);

public:
    bool operator()(std::string_view notified, std::string_view subscribed) const override {
        if constexpr (std::is_same_v<T, std::string_view>) {
            return MatchFunctor()(notified, subscribed);
        } else {
            try {
                const auto notifiedObj   = T(notified);
                const auto subscribedObj = T(subscribed);
                // TODO here (optional) caching of domain objects, or even (notified, subscribed) results could be implemented
                return MatchFunctor()(notifiedObj, subscribedObj);
            } catch (...) {
                return false;
            }
        }
    }
};

template<Number T>
class NumberFilter : public AbstractFilter {
public:
    bool operator()(std::string_view notified, std::string_view subscribed) const override {
        T          subscribedNumber = {};
        const auto r1               = std::from_chars(subscribed.begin(), subscribed.end(), subscribedNumber);
        if (r1.ec == std::errc::invalid_argument) {
            return false;
        }
        T          notifiedNumber = {};
        const auto r2             = std::from_chars(notified.begin(), notified.end(), notifiedNumber);
        if (r2.ec == std::errc::invalid_argument) {
            return false;
        }

        return subscribedNumber == notifiedNumber;
    }
};

namespace detail {
struct TimingCtxMatches {
    bool operator()(const opencmw::TimingCtx &maybeMatching, const opencmw::TimingCtx &matched) const { return maybeMatching.matches(matched); }
};
} // namespace detail

using ContentTypeFilter = opencmw::majordomo::DomainFilter<std::string_view>;
using TimingCtxFilter   = opencmw::majordomo::DomainFilter<opencmw::TimingCtx, detail::TimingCtxMatches>;

} // namespace opencmw::majordomo

#endif
