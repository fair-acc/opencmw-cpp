#ifndef OPENCMW_CPP_DNS_HPP
#define OPENCMW_CPP_DNS_HPP

#ifndef __EMSCRIPTEN__
#include "services.hpp"
#include <majordomo/Worker.hpp>
#endif // __EMSCRIPTEN__

#include "RestClient.hpp"

#include <atomic>
#include <MIME.hpp>
#include <opencmw.hpp>
#include <refl.hpp>
#include <URI.hpp>

namespace opencmw {
namespace service {

namespace dns {

struct Entry {
    std::string protocol;
    std::string hostname;
    int         port{ -1 };
    std::string service_name;
    std::string service_type;

    std::string signal_name;
    std::string signal_unit;
    float       signal_rate{ std::numeric_limits<float>::quiet_NaN() };
    std::string signal_type;

    bool        operator==(const Entry &b) const = default;
};

// ttl cannot be serialised, that's why it lives in this subclass
struct StorageEntry : Entry {
    using clock = std::chrono::system_clock;
    std::chrono::time_point<clock> ttl{ clock::now() + std::chrono::hours{ 1 } }; // kill entry if it has not been renewed before this point in time
};

struct QueryEntry;

class DataStorage {
public:
    static DataStorage &getInstance() {
        static DataStorage instance;
        return instance;
    }

    StorageEntry addEntry(const Entry &entry) {
        // check if we already have this entry
        auto            now = StorageEntry::clock::now();
        std::lock_guard lock(_mutex);

        // invalidate previous entries of this signal
        std::for_each(_entries.begin(), _entries.end(), [&entry, &now](auto &e) { if(e.ttl > now && e == entry) e.ttl = StorageEntry::clock::time_point::min(); });
        // find an expired entry and replace it   or push_back
        auto         expired = std::find_if(_entries.begin(), _entries.end(), [&now](const auto &e) { return e.ttl < now; });
        StorageEntry newEntry{ entry };
        if (expired != _entries.end()) {
            *expired = newEntry;
        } else {
            _entries.push_back(newEntry);
        }

        return newEntry;
    }

    template<class EntryType = Entry, class FilterType = QueryEntry>
    std::vector<EntryType> queryEntries(FilterType filter = {}) const {
        std::vector<EntryType> result;
        auto                   now = StorageEntry::clock::now();
        std::copy_if(_entries.begin(), _entries.end(), std::back_inserter(result),
                [&filter, &now](const StorageEntry &entry) {
                    return filter == entry && entry.ttl > now;
                });
        return result;
    }

    const std::vector<StorageEntry> &getEntries() const {
        return _entries;
    }

    int getActiveEntriesCount() const {
        auto now = StorageEntry::clock::now();
        auto c   = std::count_if(_entries.begin(), _entries.end(), [&now](auto &e) { return e.ttl > now; });
        return c;
    }

protected:
    DataStorage() {
        loadDataFromFile(filePath);
    };
    virtual ~DataStorage() {
        saveDataToFile(filePath);
    }

    bool loadDataFromFile(const char *filePath);
    bool saveDataToFile(const char *filePath);
    // Disallow copy construction and assignment
    DataStorage(const DataStorage &)                         = delete;
    DataStorage              &operator=(const DataStorage &) = delete;

    std::mutex                _mutex;
    std::vector<StorageEntry> _entries;
    const char               *filePath = "./dns_data_storage.yas";
};

struct QueryEntry : Entry {
    bool operator==(const Entry &entry) const {
#define _check_string(name) \
    if (name != "" && name != entry.name) return false;

        _check_string(protocol);
        _check_string(hostname);
        _check_string(service_name);
        _check_string(service_type);
        _check_string(signal_name);
        _check_string(signal_type);
#undef _check_string
        if (port != -1 && port != entry.port) return false;
        if (!std::isnan(signal_rate) && signal_rate != entry.signal_rate) return false;

        return true;
    }
};

struct Context : QueryEntry {
    MIME::MimeType contextType{ MIME::BINARY };
};

struct QueryResponse {
    std::vector<std::string> protocol;
    std::vector<std::string> hostname;
    std::vector<int>         port;
    std::vector<std::string> service_name;
    std::vector<std::string> service_type;

    std::vector<std::string> signal_name;
    std::vector<std::string> signal_unit;
    std::vector<float>       signal_rate;
    std::vector<std::string> signal_type;

    QueryResponse() = default;
    QueryResponse(const std::vector<Entry> &entries) {
#define _insert_into(field) \
    field.reserve(entries.size()); \
    std::transform(entries.begin(), entries.end(), std::back_inserter(field), [](const Entry &entry) { \
        return entry.field; \
    });
        _insert_into(protocol);
        _insert_into(hostname);
        _insert_into(port);
        _insert_into(service_name);
        _insert_into(service_type);
        _insert_into(signal_name);
        _insert_into(signal_unit);
        _insert_into(signal_rate);
        _insert_into(signal_type);
#undef _insert_into
    }

    std::vector<Entry> toEntries() const {
        const std::size_t size = protocol.size();
        assert(hostname.size() == size
                && port.size() == size
                && service_name.size() == size
                && service_type.size() == size
                && signal_name.size() == size
                && signal_unit.size() == size
                && signal_rate.size() == size
                && signal_type.size() == size);

        std::vector<Entry> res;
        res.reserve(size);

        for (std::size_t i = 0; i < size; ++i) {
            res.push_back({ protocol[i],
                    hostname[i],
                    port[i],
                    service_name[i],
                    service_type[i],
                    signal_name[i],
                    signal_unit[i],
                    signal_rate[i],
                    signal_type[i] });
        }

        return res;
    }
};

}
}
} // namespace opencmw::service::dns
#define _ENTRY_FIELDS_ protocol, hostname, port, service_name, service_type, signal_name, signal_unit, signal_rate, signal_type
ENABLE_REFLECTION_FOR(opencmw::service::dns::Entry, _ENTRY_FIELDS_);
ENABLE_REFLECTION_FOR(opencmw::service::dns::QueryEntry, _ENTRY_FIELDS_);
ENABLE_REFLECTION_FOR(opencmw::service::dns::Context, contextType, _ENTRY_FIELDS_);
ENABLE_REFLECTION_FOR(opencmw::service::dns::QueryResponse, _ENTRY_FIELDS_); // everything is a vector<T> here

namespace opencmw {
namespace service {
namespace dns {

bool DataStorage::loadDataFromFile(const char *filePath) {
    if (!std::filesystem::exists(filePath)) return false;

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    std::string   s{ buffer.str() };

    IoBuffer      ioBuffer{ s.data(), s.size() };
    QueryResponse resp;
    opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::ALWAYS>(ioBuffer, resp);
    auto            newEntries = resp.toEntries();

    std::lock_guard lock(_mutex);
    std::for_each(newEntries.begin(), newEntries.end(), [&](const auto &entry) {
        _entries.push_back({ entry });
    });
    return true;
}

bool DataStorage::saveDataToFile(const char *filePath) {
    std::lock_guard    lock(_mutex);
    std::vector<Entry> entries;
    std::for_each(_entries.begin(), _entries.end(), [&entries](auto &e) { entries.push_back({ e }); });
    QueryResponse k{ entries };
    IoBuffer      outBuffer;
    opencmw::serialise<opencmw::YaS>(outBuffer, k);

    std::ofstream os{ filePath, std::ios::binary };
    if (!os.is_open()) return false;

    os.write((const char *) outBuffer.data(), outBuffer.size());
    return true;
}

#ifndef __EMSCRIPTEN__
using dnsWorker = majordomo::Worker<"dns", Context, Entry, QueryResponse, majordomo::description<"Register and Query Signals">>;

class DnsWorker {
public:
    void operator()(majordomo::RequestContext &rawCtx, const Context &ctx, const Entry &in, Context &replyContext, QueryResponse &response) {
        if (rawCtx.request.command == mdp::Command::Set) {
            auto out = DataStorage::getInstance().addEntry(in);
            response = { { out } };
        } else if (rawCtx.request.command == mdp::Command::Get) {
            auto &entries = DataStorage::getInstance().getEntries();
            auto  result  = DataStorage::getInstance().queryEntries(ctx);
            response      = { result };
        }
    }
};

#endif // __EMSCRIPTEN__

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

#endif // OPENCMW_CPP_DNS_HPP
