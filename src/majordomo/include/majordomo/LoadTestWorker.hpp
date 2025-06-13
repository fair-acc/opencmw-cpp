
#ifndef OPENCMW_MAJORDOMO_LOADTESTWORKER_H
#define OPENCMW_MAJORDOMO_LOADTESTWORKER_H

#include "QuerySerialiser.hpp"
#include <LoadTest.hpp>
#include <majordomo/Worker.hpp>

#include <chrono>

namespace opencmw::majordomo::load_test {

using opencmw::load_test::Context;
using opencmw::load_test::Payload;

template<units::basic_fixed_string serviceName = "/loadTest", typename... Meta>
struct Worker : public opencmw::majordomo::Worker<serviceName, Context, Payload, Payload, Meta...> {
    using super_t = opencmw::majordomo::Worker<serviceName, Context, Payload, Payload, Meta...>;
    std::jthread _notifier;

    struct SubscriptionInfo : public Context {
        SubscriptionInfo(const Context &ctx)
            : Context(ctx) {}

        std::int64_t nextIndex          = 0;
        std::int64_t initialDelayLeftMs = initialDelayMs;
        std::int64_t updatesLeft        = nUpdates;
    };
    std::unordered_map<mdp::Topic, SubscriptionInfo> _subscriptions;

    template<typename BrokerType>
    explicit Worker(BrokerType &broker)
        : super_t(broker, {}) {
        opencmw::query::registerTypes(Context(), broker);

        super_t::setCallback([](opencmw::majordomo::RequestContext & /*rawCtx*/, const Context & /*inCtx*/, const Payload &in, Context & /*outCtx*/, Payload &out) {
            out.data        = in.data;
            out.timestampNs = opencmw::load_test::timestamp().count();
        });
        _notifier = std::jthread([this](std::stop_token stopToken) {
            std::int64_t                                     timecounter = 0;

            std::unordered_map<mdp::Topic, SubscriptionInfo> subscriptions;

            while (!stopToken.stop_requested()) {
                const auto stepMs = 10;
                std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
                timecounter += stepMs;
                auto activeSubscriptions = super_t::activeSubscriptions();
                std::erase_if(subscriptions, [&activeSubscriptions](const auto &sub) { return !activeSubscriptions.contains(sub.first); });
                for (const auto &active : activeSubscriptions) {
                    auto it = subscriptions.find(active);
                    if (it == subscriptions.end()) {
                        // TODO protect against unexpected queries
                        auto ctx = query::deserialise<Context>(active.params());
                        it       = subscriptions.try_emplace(active, ctx).first;
                    }
                    auto &sub = it->second;

                    sub.initialDelayLeftMs -= stepMs;

                    if (sub.initialDelayLeftMs > 0) {
                        continue;
                    }
                    if (sub.updatesLeft == 0) {
                        continue;
                    }
                    if (sub.intervalMs > 0 && timecounter % sub.intervalMs != 0) {
                        continue;
                    }
                    Payload payload{ sub.nextIndex, std::string(static_cast<std::size_t>(sub.payloadSize), 'x'), opencmw::load_test::timestamp().count() };
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
