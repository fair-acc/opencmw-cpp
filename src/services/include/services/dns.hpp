#ifndef OPENCMW_CPP_DNS_HPP
#define OPENCMW_CPP_DNS_HPP

#include "dns_client.hpp"
#include "dns_storage.hpp"
#include "dns_types.hpp"

#ifndef __EMSCRIPTEN__

#include <majordomo/Worker.hpp>
#include <MIME.hpp>

#include <atomic>

namespace opencmw::service::dns {

using DnsWorkerType = majordomo::Worker<"/dns", Context, FlatEntryList, FlatEntryList, majordomo::description<"Register and Query Signals">>;

class DnsHandler {
protected:
    DataStorage datastorage;

public:
    void operator()(majordomo::RequestContext &rawCtx, const Context &ctx, const FlatEntryList &in, [[maybe_unused]] Context &replyContext, FlatEntryList &response) {
        if (rawCtx.request.command == mdp::Command::Set) {
            if (ctx.doDelete) {
                response = datastorage.deleteEntries(in.toEntries<QueryEntry>());
            } else {
                auto out = datastorage.addEntries(in.toEntries());
                response = out;
            }
        } else if (rawCtx.request.command == mdp::Command::Get) {
            auto result = datastorage.queryEntries(ctx);
            response    = { result };
        }
    }
};

Entry registerSignals(const std::vector<Entry> &entries, std::string scheme_host_port = "http://localhost:8080") {
    IoBuffer      outBuffer;
    FlatEntryList entrylist{ entries };
    opencmw::serialise<opencmw::YaS>(outBuffer, entrylist);
    std::string contentType{ MIME::BINARY.typeName() };
    std::string body{ outBuffer.asString() };

    // send request to register Signal
    httplib::Client client{
        scheme_host_port
    };

    auto response = client.Post("dns", body, contentType);

    if (response.error() == httplib::Error::Read) throw std::runtime_error{ "Server did not send an answer" };
    if (response.error() != httplib::Error::Success || response->status == 500) throw std::runtime_error{ response->reason };

    // deserialise response
    IoBuffer inBuffer;
    inBuffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(response->body);

    FlatEntryList res;
    try {
        opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::ALWAYS>(inBuffer, res);
    } catch (const ProtocolException &exc) {
        throw std::runtime_error{ exc.what() }; // rethrowing, because ProtocolException behaves weird
    }

    return res.toEntries().front();
}

std::vector<Entry> querySignals(const Entry &a = {}, std::string scheme_host_port = "http://localhost:8080") {
    // send request to register Service
    httplib::Client client{
        scheme_host_port
    };

    auto response = client.Get("dns", httplib::Params{ { "protocol", a.protocol }, { "hostname", a.hostname }, { "port", std::to_string(a.port) }, { "service_name", a.service_name }, { "service_type", a.service_type }, { "signal_name", a.signal_name }, { "signal_unit", a.signal_unit }, { "signal_rate", std::to_string(a.signal_rate) }, { "signal_type", a.signal_type } },
            httplib::Headers{
                    { std::string{ "Content-Type" }, std::string{ MIME::BINARY.typeName() } } });

    if (response.error() == httplib::Error::Read) throw std::runtime_error{ "Server did not send an answer" };
    if (response.error() != httplib::Error::Success || response->status == 500) throw std::runtime_error{ response->reason };

    // deserialise response
    IoBuffer inBuffer;
    inBuffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(response->body);

    FlatEntryList res;
    try {
        opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::ALWAYS>(inBuffer, res);
    } catch (const ProtocolException &exc) {
        throw std::runtime_error{ exc.what() }; // rethrowing, because ProtocolException behaves weird
    }

    return res.toEntries();
}

} // namespace opencmw::service::dns

#endif // __EMSCRIPTEN__

#endif // OPENCMW_CPP_DNS_HPP
