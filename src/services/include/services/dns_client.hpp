#ifndef DNS_CLIENT_HPP
#define DNS_CLIENT_HPP

#include "dns_types.hpp"
#include "RestClient.hpp"
#include <atomic>
#include <IoSerialiserYaS.hpp>
#include <URI.hpp>
#include <utility>
#include <QuerySerialiser.hpp>

namespace opencmw::service::dns {

class DnsClient {
    client::ClientContext &_clientContext;
    URI<STRICT>            _endpoint;

public:
    DnsClient(client::ClientContext &clientContext, const URI<STRICT> &endpoint)
        : _clientContext(clientContext)
        , _endpoint(endpoint) {
    }

    void querySignalsAsync(auto callback, const Entry &filter = {}) {
        auto uri = URI<>::factory(_endpoint);
        uri      = std::move(uri).setQuery(query::serialise(filter));

        _clientContext.get(uri.build(), callback);
    }

    std::vector<Entry> querySignals(const Entry &filter = {}) {
        std::atomic_bool received{ false };
        FlatEntryList    resp;

        querySignalsAsync([&received, &resp](const mdp::Message &message) {
            IoBuffer buf{ message.data };
            deserialise<YaS, ProtocolCheck::ALWAYS>(buf, resp);
            received = true;
            received.notify_one();
        },
                filter);

        received.wait(false, std::memory_order_relaxed);

        return resp.toEntries();
    }

    void registerSignalsAsync(auto callback, const std::vector<Entry> &entries) {
        auto          uri = URI<>::factory(_endpoint);

        IoBuffer      buf;
        FlatEntryList entrylist{ entries };
        opencmw::serialise<YaS>(buf, entrylist);

        _clientContext.set(_endpoint, callback, std::move(buf));
    }

    std::vector<Entry> registerSignals(const std::vector<Entry> &entries) {
        std::atomic_bool received{ false };
        FlatEntryList    resp;

        registerSignalsAsync([&received, &resp](const mdp::Message &message) {
            IoBuffer buf{ message.data };
            deserialise<YaS, ProtocolCheck::ALWAYS>(buf, resp);
            received = true;
            received.notify_one();
        },
                entries);

        received.wait(false, std::memory_order_relaxed);

        return resp.toEntries();
    }

    Entry registerSignal(const Entry &entry) {
        return registerSignals({ entry })[0];
    }
};

} // namespace opencmw::service::dns

#endif // DNS_CLIENT_HPP
