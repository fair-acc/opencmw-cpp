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

    void querySignalsAsync(std::function<void(std::vector<Entry>)> callback, const Entry &filter = {}) {
        auto uri = URI<>::factory(_endpoint);
        uri      = std::move(uri).setQuery(query::serialise(filter));

        _clientContext.get(uri.build(), [&callback](auto &msg) {
            IoBuffer buf{ msg.data };
            FlatEntryList    resp;
            deserialise<YaS, ProtocolCheck::ALWAYS>(buf, resp);
            callback(resp.toEntries()); });
    }

    std::vector<Entry> querySignals(const Entry &filter = {}) {
        std::atomic_bool   received{ false };
        std::vector<Entry> resp;

        querySignalsAsync([&received, &resp](const std::vector<Entry> &response_entries) {
            resp     = response_entries;
            received = true;
            received.notify_one();
        },
                filter);

        received.wait(false, std::memory_order_relaxed);

        return resp;
    }

    void registerSignalsAsync(std::function<void(std::vector<Entry>)> callback, const std::vector<Entry> &entries) {
        auto          uri = URI<>::factory(_endpoint);

        IoBuffer      buf;
        FlatEntryList entrylist{ entries };
        opencmw::serialise<YaS>(buf, entrylist);

        _clientContext.set(
                _endpoint, [&callback](auto &msg) {
                    FlatEntryList resp;
                    IoBuffer      buf{ msg.data };
                    deserialise<YaS, ProtocolCheck::ALWAYS>(buf, resp);
                    callback(resp.toEntries());
                },
                std::move(buf));
    }

    std::vector<Entry> registerSignals(const std::vector<Entry> &entries) {
        std::atomic_bool   received{ false };
        std::vector<Entry> resp;

        registerSignalsAsync([&received, &resp](const std::vector<Entry> &response_entries) {
            resp     = response_entries;
            received = true;
            received.notify_one();
        },
                entries);

        received.wait(false, std::memory_order_relaxed);

        return resp;
    }

    Entry registerSignal(const Entry &entry) {
        return registerSignals({ entry })[0];
    }
};

// class DnsRestClient : public opencmw::client::RestClient {
//     URI<STRICT> endpoint;
//
// public:
//     DnsRestClient(const std::string& uri = "http://localhost:8080/dns")
//         : opencmw::client::RestClient(opencmw::client::DefaultContentTypeHeader(MIME::BINARY)), endpoint(uri) {
//     }
//
//     std::vector<Entry> querySignals(const Entry &filter = {}) {
//         auto uri       = URI<>::factory();
//
//         auto queryPara = opencmw::query::serialise(filter);
//         uri            = std::move(uri).setQuery(queryPara);
//
//         client::Command cmd;
//         cmd.command  = mdp::Command::Get;
//         cmd.endpoint = endpoint;
//
//         std::atomic<bool> done;
//         mdp::Message      answer;
//         cmd.callback = [&done, &answer](const mdp::Message &reply) {
//             answer = reply;
//             done.store(true, std::memory_order_release);
//             done.notify_all();
//         };
//         request(cmd);
//
//         done.wait(false);
//         if (!done.load(std::memory_order_acquire)) {
//             throw std::runtime_error("error acquiring answer");
//         }
//         if (!answer.error.empty()) {
//             throw std::runtime_error{ answer.error };
//         }
//
//         FlatEntryList res;
//         opencmw::deserialise<YaS, ProtocolCheck::ALWAYS>(answer.data, res);
//         return res.toEntries();
//     }
//
//     std::vector<Entry> registerSignals(const std::vector<Entry> &entries) {
//         IoBuffer outBuffer;
//         FlatEntryList entrylist{ entries };
//         opencmw::serialise<YaS>(outBuffer, entrylist);
//
//         client::Command cmd;
//         cmd.command  = mdp::Command::Set;
//         cmd.endpoint = endpoint;
//         cmd.data     = outBuffer;
//
//         std::atomic<bool> done;
//         mdp::Message      answer;
//         cmd.callback = [&done, &answer](const mdp::Message &reply) {
//             answer = reply;
//             done.store(true, std::memory_order_release);
//             done.notify_all();
//         };
//         request(cmd);
//
//         done.wait(false);
//         if (!done.load(std::memory_order_acquire)) {
//             throw std::runtime_error("error acquiring answer");
//         }
//         if (!answer.error.empty()) {
//             throw std::runtime_error{ answer.error };
//         }
//
//         FlatEntryList res;
//         if (!answer.data.empty()) {
//             opencmw::deserialise<YaS, ProtocolCheck::ALWAYS>(answer.data, res);
//         }
//         return res.toEntries();
//     }
//
//     Entry registerSignal(const Entry &entry) {
//         return registerSignals({ entry })[0];
//     }
// };

} // namespace opencmw::service::dns

#endif // DNS_CLIENT_HPP