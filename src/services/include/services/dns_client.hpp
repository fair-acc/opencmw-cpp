#ifndef DNS_CLIENT_HPP
#define DNS_CLIENT_HPP

#include "Debug.hpp"
#include "dns_types.hpp"
#include "MdpMessage.hpp"
#include "RestClient.hpp"
#include <atomic>
#include <chrono>
#include <IoSerialiserYaS.hpp>
#include <thread>
#include <URI.hpp>
#include <utility>
#include <QuerySerialiser.hpp>

using namespace std::chrono_literals;

namespace opencmw::service::dns {

struct DnsClient {
    client::ClientContext &_clientContext;
    URI<STRICT>            _endpoint;

public:
    DnsClient(client::ClientContext &clientContext, const URI<STRICT> &endpoint)
        : _clientContext(clientContext)
        , _endpoint(endpoint) {
    }
    DnsClient(client::ClientContext &clientContext, const std::string &endpoint)
        : DnsClient(clientContext, URI<STRICT>{ endpoint }) {}

    void querySignalsAsync(std::function<void(std::vector<Entry>)> callback, const Entry &filter = {}) {
        auto uri = URI<>::factory(_endpoint);
        uri      = std::move(uri).setQuery(query::serialise(filter));

        _clientContext.get(uri.build(), [callback = std::move(callback)](const mdp::Message &msg) {
            std::cout << msg.error << std::endl;
            IoBuffer      buf{ msg.data };

            FlatEntryList resp;
            if (!buf.empty()) {
                deserialise<YaS, ProtocolCheck::ALWAYS>(buf, resp);
            }

            callback(resp.toEntries());
        });
    }

    void registerSignalsAsync(std::function<void(std::vector<Entry>)> callback, const std::vector<Entry> &entries) {
        auto          uri = URI<>::factory(_endpoint);

        IoBuffer      buf;
        FlatEntryList entrylist{ entries };
        opencmw::serialise<YaS>(buf, entrylist);

        _clientContext.set(
                _endpoint, [callback = std::move(callback)](auto &msg) {
                    FlatEntryList resp;
                    IoBuffer      buf{ msg.data };
                    if (!buf.empty()) {
                        deserialise<YaS, ProtocolCheck::ALWAYS>(buf, resp);
                    }
                    callback(resp.toEntries());
                },
                std::move(buf));
    }

    template<typename ReturnType>
    auto _doSync(auto function, auto parameter, std::chrono::seconds timeout) {
        std::atomic_bool        received{ false };
        std::mutex              m;
        std::condition_variable cv;
        ReturnType              ret;

        (this->*function)([&received, &cv, &ret](const ReturnType &&r) {
            ret      = std::move(r);
            received = true;
            cv.notify_one();
        },
                parameter);

        std::unique_lock l{ m };
        cv.wait(l, [&received] { return received.load(); });

        return ret;
    }
    std::vector<Entry> querySignals(const Entry &filter = {}, std::chrono::seconds timeout = 20s) {
        return _doSync<std::vector<Entry>>(&DnsClient::querySignalsAsync, filter, timeout);
    }
    std::vector<Entry> registerSignals(const std::vector<Entry> &entries, std::chrono::seconds timeout = 20s) {
        return _doSync<std::vector<Entry>>(&DnsClient::registerSignalsAsync, entries, timeout);
    }
    Entry registerSignal(const Entry &entry) {
        auto s = registerSignals({ entry });
        if (s.size() > 0)
            return s[0];
        return {};
    }
};

} // namespace opencmw::service::dns

#endif // DNS_CLIENT_HPP