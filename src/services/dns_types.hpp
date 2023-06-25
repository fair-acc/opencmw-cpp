#ifndef _DNS_TYPES_HPP_
#define _DNS_TYPES_HPP_

#include <MIME.hpp>
#include <opencmw.hpp>

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

#endif // _DNS_TYPES_HPP_