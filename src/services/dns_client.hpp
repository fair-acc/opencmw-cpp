#ifndef __DNS_CLIENT_HPP__
#define __DNS_CLIENT_HPP__

#include "dns_types.hpp"
#include "RestClient.hpp"
#include <IoSerialiserYaS.hpp>
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
        uri      = std::move(uri).addQueryParameter("protocol", filter.protocol);
        uri      = std::move(uri).addQueryParameter("hostname", filter.hostname);
        uri      = std::move(uri).addQueryParameter("service_type", filter.service_type);
        uri      = std::move(uri).addQueryParameter("service_name", filter.service_name);
        uri      = std::move(uri).addQueryParameter("signal_name", filter.signal_name);
        uri      = std::move(uri).addQueryParameter("signal_type", filter.signal_type);
        uri      = std::move(uri).addQueryParameter("signal_unit", filter.signal_unit);
        uri      = std::move(uri).addQueryParameter("signal_rate", std::to_string(filter.signal_rate));

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