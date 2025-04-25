#ifndef OPENCMW_CPP_DNS_HPP
#define OPENCMW_CPP_DNS_HPP

#include "ClientCommon.hpp"
#include "dns_storage.hpp"
#include "dns_types.hpp"
#include "MdpMessage.hpp"
#include "RestClient.hpp"

#ifndef __EMSCRIPTEN__

#include <majordomo/Worker.hpp>
#include <MIME.hpp>
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

inline Entry registerSignals(const std::vector<Entry> &entries, std::string scheme_host_port = "http://localhost:8080") {
    client::Command cmd;
    cmd.command     = mdp::Command::Set;
    cmd.serviceName = "/dns";
    cmd.topic       = URI<>::UriFactory{ URI<>(std::move(scheme_host_port)) }.path("/dns").build();
    opencmw::serialise<opencmw::YaS>(cmd.data, FlatEntryList{ entries });

    client::RestClient  client{ client::DefaultContentTypeHeader(MIME::BINARY) };
    auto                reply = client.blockingRequest(std::move(cmd));

    if (!reply.error.empty()) {
        throw std::runtime_error{ reply.error };
    }

    FlatEntryList res;
    try {
        opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::ALWAYS>(reply.data, res);
    } catch (const ProtocolException &exc) {
        throw std::runtime_error{ exc.what() }; // rethrowing, because ProtocolException behaves weird
    }
    return res.toEntries().front();
}

inline std::vector<Entry> querySignals(const Entry &a = {}, std::string scheme_host_port = "http://localhost:8080") {
    client::Command cmd;
    cmd.command     = mdp::Command::Get;
    cmd.serviceName = "/dns";
    cmd.topic       = URI<>::UriFactory{ URI<>(std::move(scheme_host_port)) }.path("/dns").addQueryParameter("service_name", a.service_name).addQueryParameter("signal_name", a.signal_name).addQueryParameter("signal_unit", a.signal_unit).addQueryParameter("signal_rate", std::to_string(a.signal_rate)).addQueryParameter("signal_type", a.signal_type).build();

    client::RestClient  client{ client::DefaultContentTypeHeader(MIME::BINARY) };
    auto                reply = client.blockingRequest(std::move(cmd));

    if (!reply.error.empty()) {
        throw std::runtime_error{ reply.error };
    }

    FlatEntryList res;
    try {
        opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::ALWAYS>(reply.data, res);
    } catch (const ProtocolException &exc) {
        throw std::runtime_error{ exc.what() }; // rethrowing, because ProtocolException behaves weird
    }

    return res.toEntries();
}

} // namespace opencmw::service::dns

#endif // __EMSCRIPTEN__

#endif // OPENCMW_CPP_DNS_HPP
