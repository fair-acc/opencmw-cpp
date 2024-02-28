#ifndef DNS_TYPES_HPP
#define DNS_TYPES_HPP

#include <MIME.hpp>
#include <opencmw.hpp>

namespace opencmw::service::dns {

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
    // don't need a virtual here, as this type is explicitly used and we don't need the overhead
    bool operator==(const Entry &entry) const noexcept {
#define check_string(name) \
    if (!name.empty() && name != entry.name) return false;

        check_string(protocol)
                check_string(hostname)
                        check_string(service_name)
                                check_string(service_type)
                                        check_string(signal_name)
                                                check_string(signal_type)
#undef check_string
                                                        if (port != -1 && port != entry.port) return false;
        if (!std::isnan(signal_rate) && signal_rate != entry.signal_rate) return false;

        return true;
    }
};

struct Context : QueryEntry {
    [[maybe_unused]] MIME::MimeType contextType{ MIME::BINARY };
    bool                            doDelete{ false };
};

struct FlatEntryList {
    std::vector<std::string> protocol;
    std::vector<std::string> hostname;
    std::vector<int>         port;
    std::vector<std::string> service_name;
    std::vector<std::string> service_type;

    std::vector<std::string> signal_name;
    std::vector<std::string> signal_unit;
    std::vector<float>       signal_rate;
    std::vector<std::string> signal_type;

    FlatEntryList() = default;
    FlatEntryList(const std::vector<Entry> &entries) { // NOLINT(google-explicit-constructor, google-runtime-int)
#define insert_into(field) \
    field.reserve(entries.size()); \
    std::transform(entries.begin(), entries.end(), std::back_inserter(field), [](const Entry &entry) { \
        return entry.field; \
    });
        insert_into(protocol)
                insert_into(hostname)
                        insert_into(port)
                                insert_into(service_name)
                                        insert_into(service_type)
                                                insert_into(signal_name)
                                                        insert_into(signal_unit)
                                                                insert_into(signal_rate)
                                                                        insert_into(signal_type)
#undef insert_into
    }

    template<typename EntryType = Entry>
    [[nodiscard]] std::vector<EntryType> toEntries() const {
        const std::size_t size = protocol.size();
        assert(hostname.size() == size
                && port.size() == size
                && service_name.size() == size
                && service_type.size() == size
                && signal_name.size() == size
                && signal_unit.size() == size
                && signal_rate.size() == size
                && signal_type.size() == size);

        std::vector<EntryType> res;
        res.reserve(size);

        for (std::size_t i = 0; i < size; ++i) {
            res.emplace_back(EntryType{ protocol[i],
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

} // namespace opencmw::service::dns
#define ENTRY_FIELDS protocol, hostname, port, service_name, service_type, signal_name, signal_unit, signal_rate, signal_type
ENABLE_REFLECTION_FOR(opencmw::service::dns::Entry, ENTRY_FIELDS)
ENABLE_REFLECTION_FOR(opencmw::service::dns::QueryEntry, ENTRY_FIELDS)
ENABLE_REFLECTION_FOR(opencmw::service::dns::Context, contextType, doDelete, ENTRY_FIELDS)
ENABLE_REFLECTION_FOR(opencmw::service::dns::FlatEntryList, ENTRY_FIELDS) // everything is a vector<T> here
#undef ENTRY_FIELDS

#endif // DNS_TYPES_HPP
