
#ifndef OPENCMW_MAJORDOMO_LOADTESTWORKER_H
#define OPENCMW_MAJORDOMO_LOADTESTWORKER_H

#include "QuerySerialiser.hpp"
#include <chrono>
#include <majordomo/Worker.hpp>

// TODO(Frank) move to shared location
namespace opencmw::load_test {

struct Context {
    std::string             topic;
    std::int64_t            intervalMs  = 1000; // must be multiple of 100 (enforce?)
    std::int64_t            payloadSize = 100;
    std::int64_t            nUpdates    = -1; // -1 means infinite
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};

struct Payload {
    std::int64_t  index;
    std::string   data;
    std::int64_t timestampNs = 0;
};

} // namespace opencmw::load_test

ENABLE_REFLECTION_FOR(opencmw::load_test::Context, topic, intervalMs, payloadSize, nUpdates, contentType)
ENABLE_REFLECTION_FOR(opencmw::load_test::Payload, index, data, timestampNs)

namespace opencmw::majordomo::load_test {

using opencmw::load_test::Context;
using opencmw::load_test::Payload;

inline auto timestampNs() { return std::chrono::high_resolution_clock::now().time_since_epoch().count(); }

template<units::basic_fixed_string serviceName = "/loadTest", typename... Meta>
struct Worker : public opencmw::majordomo::Worker<serviceName, Context, Payload, Payload, Meta...> {
    using super_t = opencmw::majordomo::Worker<serviceName, Context, Payload, Payload, Meta...>;
    std::jthread _notifier;

    struct SubscriptionInfo : public Context {
        SubscriptionInfo(const Context &ctx)
            : Context(ctx) {}

        std::int64_t nextIndex   = 0;
        std::int64_t updatesLeft = nUpdates;
    };
    std::unordered_map<mdp::Topic, SubscriptionInfo> _subscriptions;

    Worker(const majordomo::Broker<> &broker)
        : super_t(broker, {}) {
        super_t::setCallback([](opencmw::majordomo::RequestContext & /*rawCtx*/, const Context & /*inCtx*/, const Payload &in, Context & /*outCtx*/, Payload &out) {
            out.data        = in.data;
            out.timestampNs = timestampNs();
        });
        _notifier = std::jthread([this](std::stop_token stopToken) {
            std::int64_t                                    timecounter = 0;

            std::unordered_map<mdp::Topic, SubscriptionInfo> subscriptions;

            while (!stopToken.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                timecounter += 100;
                auto activeSubscriptions = super_t::activeSubscriptions();
                std::erase_if(subscriptions, [&activeSubscriptions](const auto &sub) { return !activeSubscriptions.contains(sub.first); });
                for (const auto &active : activeSubscriptions) {
                    auto it = subscriptions.find(active);
                    if (it == subscriptions.end()) {
                        // TODO protect against unexpected queries
                        auto ctx = query::deserialise<Context>(active.params());
                        it = subscriptions.try_emplace(active, ctx).first;
                    }
                    auto &sub = it->second;

                     if (sub.updatesLeft == 0) {
                        continue;
                    }
                    if (sub.intervalMs > 0 && timecounter % sub.intervalMs != 0) {
                        continue;
                    }
                    Payload payload{ sub.nextIndex, std::string(static_cast<std::size_t>(sub.payloadSize), 'x'), timestampNs() };
                    super_t::notify(sub, std::move(payload));
                    sub.nextIndex++;
                    sub.updatesLeft--;
                }
            }
        });
    }
};

} // namespace opencmw::majordomo::load_test

#endif
