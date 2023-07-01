#ifndef __DNS_CLIENT_HPP__
#define __DNS_CLIENT_HPP__

#include "dns_types.hpp"
#include "RestClient.hpp"
#include <IoSerialiserYaS.hpp>
#include <QuerySerialiser.hpp>
#include <URI.hpp>

namespace opencmw {
namespace service {
namespace dns {

class DnsClient : public opencmw::client::RestClient {
    URI<STRICT> endpoint;

public:
    DnsClient(std::string uri = "http://localhost:8080/dns")
        : opencmw::client::RestClient(opencmw::client::DefaultContentTypeHeader(MIME::BINARY)), endpoint(uri) {
    }

    std::vector<Entry> queryServices(const Entry &filter = {}) {
        auto uri = URI<>::factory();

        auto queryPara = opencmw::query::serialise(filter);
        uri = std::move(uri).setQuery(queryPara);

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

        QueryResponse res;
        opencmw::deserialise<YaS, ProtocolCheck::ALWAYS>(answer.data, res);
        return res.toEntries();
    }

    Entry registerService(const Entry &entry) {
        IoBuffer outBuffer;
        opencmw::serialise<opencmw::YaS>(outBuffer, entry);
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

        QueryResponse res;
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