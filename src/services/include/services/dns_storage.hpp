#ifndef DNS_STORAGE_HPP
#define DNS_STORAGE_HPP

#include "dns_types.hpp"
#include <filesystem>
#include <fstream>
#include <IoSerialiserYaS.hpp>

namespace opencmw::service {

// ttl cannot be serialised, that's why it lives in this subclass
template<typename T>
struct TimeToLive : T {
    using clock = std::chrono::system_clock;
    std::chrono::time_point<clock> ttl{ clock::now() + std::chrono::hours{ 1 } }; // kill entry if it has not been renewed before this point in time
};

template<class EntryType, class EntryFilterType, class EntrySerialiseType>
class DataStorage {
public:
    using StorageEntryType = TimeToLive<EntryType>;
    using FilterType       = EntryFilterType;
    using SerialiseType    = EntrySerialiseType;

    std::vector<EntryType> addEntries(const std::vector<EntryType> &entries) {
        std::vector<EntryType> addedEntries;

        std::transform(entries.begin(), entries.end(), std::back_inserter(addedEntries), [this](const EntryType &entry) {
            return addEntry(entry);
        });

        return addedEntries;
    }
    StorageEntryType addEntry(const EntryType &entry) {
        // check if we already have this entry
        auto now = StorageEntryType::clock::now();

        // invalidate previous entries of this signal
        std::for_each(_entries.begin(), _entries.end(), [&entry, &now](auto &e) { if(e.ttl > now && e == entry) e.ttl = StorageEntryType::clock::time_point::min(); });
        // find an expired entry and replace it   or push_back
        auto             expired = std::find_if(_entries.begin(), _entries.end(), [&now](const auto &e) { return e.ttl < now; });
        StorageEntryType newEntry{ entry };
        if (expired != _entries.end()) {
            *expired = newEntry;
        } else {
            _entries.push_back(newEntry);
        }

        return newEntry;
    }

    template<class ReturnEntryType = EntryType, class FilterT = FilterType>
    std::vector<ReturnEntryType> queryEntries(FilterT filter = {}) const {
        std::vector<ReturnEntryType> result;
        auto                         now = StorageEntryType::clock::now();
        std::copy_if(_entries.begin(), _entries.end(), std::back_inserter(result),
                [&filter, &now](const StorageEntryType &entry) {
                    return filter == entry && entry.ttl > now;
                });
        return result;
    }

    const std::vector<StorageEntryType> &getEntries() const {
        return _entries;
    }

    [[nodiscard]] long getActiveEntriesCount() const {
        auto now = StorageEntryType::clock::now();
        auto c   = std::count_if(_entries.begin(), _entries.end(), [&now](auto &e) { return e.ttl > now; });
        return c;
    }

    DataStorage() {
        loadDataFromFile(filePath);
    };
    virtual ~DataStorage() {
        saveDataToFile(filePath);
    }

    bool                          loadDataFromFile(const char *filePath);
    bool                          saveDataToFile(const char *filePath);

    std::vector<StorageEntryType> _entries;
    const char                   *filePath = "./dns_data_storage.yas";
};

template<class EntryType, class FilterType, class SerialiseType>
bool DataStorage<EntryType, FilterType, SerialiseType>::loadDataFromFile(const char *filepath) {
    if (!std::filesystem::exists(filepath)) return false;

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<unsigned long>(fileSize));
    file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
    file.close();

    IoBuffer      ioBuffer{ buffer.data(), buffer.size() };
    SerialiseType resp;
    opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::ALWAYS>(ioBuffer, resp);
    auto newEntries = resp.toEntries();

    std::for_each(newEntries.begin(), newEntries.end(), [&](const auto &entry) {
        _entries.push_back({ entry });
    });
    return true;
}

template<class EntryType, class FilterType, class SerialiseType>
bool DataStorage<EntryType, FilterType, SerialiseType>::saveDataToFile(const char *filepath) {
    std::vector<EntryType> entries;
    std::for_each(_entries.begin(), _entries.end(), [&entries](auto &e) { entries.push_back({ e }); });
    SerialiseType k{ entries };
    IoBuffer      outBuffer;
    opencmw::serialise<opencmw::YaS>(outBuffer, k);

    std::ofstream os{ filepath, std::ios::binary };
    if (!os.is_open()) return false;

    os.write(reinterpret_cast<const char *>(outBuffer.data()), static_cast<long>(outBuffer.size()));

    return true;
}

namespace dns {
using StorageEntry = TimeToLive<Entry>;
using DataStorage  = opencmw::service::DataStorage<Entry, opencmw::service::dns::QueryEntry, FlatEntryList>;
} // namespace dns

} // namespace opencmw::service
#endif // DNS_STORAGE_HPP