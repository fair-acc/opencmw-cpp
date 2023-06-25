#ifndef OPENCMW_CPP_DNS_HPP
#define OPENCMW_CPP_DNS_HPP

#include "dns_client.hpp"
#include "dns_storage.hpp"
#include "dns_types.hpp"

#ifndef __EMSCRIPTEN__

#include <majordomo/Worker.hpp>
#include <MIME.hpp>

#include <atomic>

namespace opencmw {
namespace service {
namespace dns {

using dnsWorker = majordomo::Worker<"dns", Context, Entry, QueryResponse, majordomo::description<"Register and Query Signals">>;

class DnsWorker {
protected:
    DataStorage datastorage;

public:
    void operator()(majordomo::RequestContext &rawCtx, const Context &ctx, const Entry &in, Context &replyContext, QueryResponse &response) {
        if (rawCtx.request.command == mdp::Command::Set) {
            auto out = datastorage.addEntry(in);
            response = { { out } };
        } else if (rawCtx.request.command == mdp::Command::Get) {
            auto &entries = datastorage.getEntries();
            auto  result  = datastorage.queryEntries(ctx);
            response      = { result };
        }
    }
};

Entry registerService(const Entry &entry) {
    IoBuffer outBuffer;
    opencmw::serialise<opencmw::YaS>(outBuffer, entry);
    std::string contentType{ MIME::BINARY.typeName() };
    std::string body{ outBuffer.asString() };

    // send request to register Service
    httplib::Client client{
        "http://localhost:8080",
    };

    auto response = client.Post("dns", body, contentType);

    if (response.error() == httplib::Error::Read) throw std::runtime_error{ "Server did not send an answer" };
    if (response.error() != httplib::Error::Success || response->status == 500) throw std::runtime_error{ response->reason };

    // deserialise response
    IoBuffer inBuffer;
    inBuffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(response->body);

    QueryResponse res;
    try {
        opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::ALWAYS>(inBuffer, res);
    } catch (const ProtocolException &exc) {
        throw std::runtime_error{ exc.what() }; // rethrowing, because ProtocolException behaves weird
    }

    return res.toEntries().front();
}

auto queryServices = [](const Entry &a = {}) {
    // send request to register Service
    httplib::Client client{
        "http://localhost:8080",
    };

    auto response = client.Get("dns", httplib::Params{ { "protocol", a.protocol }, { "hostname", a.hostname }, { "port", std::to_string(a.port) }, { "service_name", a.service_name }, { "service_type", a.service_type }, { "signal_name", a.signal_name }, { "signal_unit", a.signal_unit }, { "signal_rate", std::to_string(a.signal_rate) }, { "signal_type", a.signal_type } },
            httplib::Headers{
                    { std::string{ "Content-Type" }, std::string{ MIME::BINARY.typeName() } } });

    if (response.error() == httplib::Error::Read) throw std::runtime_error{ "Server did not send an answer" };
    if (response.error() != httplib::Error::Success || response->status == 500) throw std::runtime_error{ response->reason };

    // deserialise response
    IoBuffer inBuffer;
    inBuffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(response->body);

    QueryResponse res;
    try {
        opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::ALWAYS>(inBuffer, res);
    } catch (const ProtocolException &exc) {
        throw std::runtime_error{ exc.what() }; // rethrowing, because ProtocolException behaves weird
    }

    return res.toEntries();
};

}
}
} // namespace opencmw::service::dns

#endif // __EMSCRIPTEN__

#endif // OPENCMW_CPP_DNS_HPP
