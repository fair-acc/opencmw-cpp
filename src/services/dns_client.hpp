#ifndef __DNS_CLIENT_HPP__
#define __DNS_CLIENT_HPP__

#ifndef __EMSCRIPTEN__
#include "Client.hpp"
#endif

#include "dns_types.hpp"
#include "RestClient.hpp"
#include <IoSerialiserYaS.hpp>
#include <URI.hpp>
#include <QuerySerialiser.hpp>

namespace opencmw {
namespace service {
namespace dns {

#ifndef __EMSCRIPTEN__


class DnsClient {
    client::ClientContext &clientContext;
    URI<STRICT>            endpoint;

public:
    DnsClient(client::ClientContext &clientContext, URI<STRICT> endpoint)
        : clientContext(clientContext)
        , endpoint(endpoint) {
    }

    void querySignalAsync(auto callback, const Entry &filter = {}) {
        auto uri = URI<>::factory(endpoint);
        uri      = std::move(uri).setQuery(query::serialise(filter));

        clientContext.get(uri.build(), callback);
    }

    std::vector<Entry> querySignal(const Entry &filter = {}) {
        std::mutex              mutex;
        std::condition_variable cv;
        std::atomic_bool        received{ false };
        FlatEntryList           resp;

        querySignalAsync([&received, &resp, &cv](const mdp::Message &message) {
            IoBuffer buf{ message.data };
            deserialise<YaS, ProtocolCheck::ALWAYS>(buf, resp);
            received = true;
            cv.notify_one();
        });

        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&received] { return received.load(); });
        }

        return resp.toEntries();
    }

    void registerSignalAsync(auto callback, const std::vector<Entry> &entries) {
        auto          uri = URI<>::factory(endpoint);

        IoBuffer      buf;
        FlatEntryList entrylist{ entries };
        opencmw::serialise<YaS>(buf, entrylist);

        clientContext.set(endpoint, callback, std::move(buf));
    }

    std::vector<Entry> registerSignal(const std::vector<Entry> &entries) {
        std::atomic_bool        received{ false };
        FlatEntryList           resp;
        std::mutex              mutex;
        std::condition_variable cv;

        registerSignalAsync([&received, &resp, &cv](const mdp::Message &message) {
            IoBuffer buf{ message.data };
            deserialise<YaS, ProtocolCheck::ALWAYS>(buf, resp);
            received = true;
            cv.notify_one();
        },
                entries);

        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&received] { return received.load(); });
        }

        return resp.toEntries();
    }
};

#endif


class DnsRestClient : public opencmw::client::RestClient {
    URI<STRICT> endpoint;

public:
    DnsRestClient(std::string uri = "http://localhost:8080/dns")
        : opencmw::client::RestClient(opencmw::client::DefaultContentTypeHeader(MIME::BINARY)), endpoint(uri) {
    }

    std::vector<Entry> querySignal(const Entry &filter = {}) {
        auto uri       = URI<>::factory();

        auto queryPara = opencmw::query::serialise(filter);
        uri            = std::move(uri).setQuery(queryPara);

        client::Command cmd;
        cmd.command  = mdp::Command::Get;
        cmd.endpoint = endpoint;

        std::atomic<bool> done;
        mdp::Message      answer;
        cmd.callback = [&done, &answer](const mdp::Message &reply) {
            answer = reply;
            done.store(true, std::memory_order_release);
            done.notify_all();
        };
        request(cmd);

        done.wait(false);
        if (!done.load(std::memory_order_acquire) == true) {
            throw std::runtime_error("error acquiring answer");
        }
        if (answer.error != "") {
            throw std::runtime_error{ answer.error };
        }

        FlatEntryList res;
        opencmw::deserialise<YaS, ProtocolCheck::ALWAYS>(answer.data, res);
        return res.toEntries();
    }

    Entry registerSignal(const std::vector<Entry> &entry) {
        IoBuffer outBuffer;
        opencmw::serialise<opencmw::YaS>(outBuffer, FlatEntryList{ entry });
        std::string     contentType{ MIME::BINARY.typeName() };

        client::Command cmd;
        cmd.command  = mdp::Command::Set;
        cmd.endpoint = endpoint;
        cmd.data     = outBuffer;

        std::atomic<bool> done;
        mdp::Message      answer;
        cmd.callback = [&done, &answer](const mdp::Message &reply) {
            answer = reply;
            done.store(true, std::memory_order_release);
            done.notify_all();
        };
        request(cmd);

        done.wait(false);
        if (!done.load(std::memory_order_acquire) == true) {
            throw std::runtime_error("error acquiring answer");
        }
        if (answer.error != "") {
            throw std::runtime_error{ answer.error };
        }

        FlatEntryList res;
        if (!answer.data.empty()) {
            opencmw::deserialise<YaS, ProtocolCheck::ALWAYS>(answer.data, res);
        }
        return res.toEntries().front();
    }
};

}
}
} // namespace opencmw::service::dns

#endif // __DNS_CLIENT_HPP__